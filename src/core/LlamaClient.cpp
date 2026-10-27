#include "core/LlamaClient.h"

#include <memory>

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QTcpSocket>
#include <QTimer>

namespace llocr {

QUrl LlamaClient::endpointUrl(const QString &baseUrl, const QString &endpointPath)
{
    QString base = baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    return QUrl(base + endpointPath);
}

QFuture<HttpResponse> LlamaClient::postJson(const QUrl &url, const QByteArray &body, const QString &apiKey, const QString &modelId, int connectionTimeoutMs, int responseTimeoutMs)
{
    auto promise = std::make_shared<QPromise<HttpResponse>>();
    promise->start();
    QFuture<HttpResponse> future = promise->future();

    // The trace of this request: the endpoint, the model it named, the sizes.
    // The API key lives in the header and never reaches it.
    auto trace = std::make_shared<RequestJournal::Record>();
    trace->at = QDateTime::currentDateTime();
    trace->endpoint = url.toString();
    trace->model = modelId;
    trace->requestBytes = body.size();

    auto *socket = new QTcpSocket(&m_network);
    m_connectSocket = socket;
    m_connectPromise = promise;
    // Per-request completion: the promise and socket travel with the lambdas,
    // so an overlapping second postJson cannot strand this future (C++-I4).
    const auto finish = [this, socket, promise, trace](HttpResponse response) {
        if (m_connectSocket == socket)
            m_connectPromise.reset();
        socket->disconnect();
        socket->deleteLater();
        trace->durationMs = trace->at.msecsTo(QDateTime::currentDateTime());
        trace->error = response.error;
        RequestJournal::instance().record(*trace);
        promise->addResult(std::move(response));
        promise->finish();
    };
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    const bool https = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const quint16 port = quint16(url.port(https ? 443 : 80));

    QObject::connect(timer, &QTimer::timeout, socket, [finish, url, connectionTimeoutMs]() {
        finish(HttpResponse{false, {}, QCoreApplication::translate("LlamaClient", "Connection to %1 timed out after %2 ms").arg(url.toString()).arg(connectionTimeoutMs)});
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket, [finish, socket, url](QAbstractSocket::SocketError) {
        finish(HttpResponse{false, {}, QCoreApplication::translate("LlamaClient", "Connection to %1 failed: %2").arg(url.toString(), socket->errorString())});
    });
    QObject::connect(socket, &QTcpSocket::connected, socket, [this, socket, timer, promise, url, body, apiKey, responseTimeoutMs, trace]() {
        timer->stop();
        socket->disconnect();  // the connect-phase handlers must not see abort()'s error
        if (m_connectSocket == socket) {
            m_connectSocket.clear();
            m_connectPromise.reset();
        }
        socket->abort();
        socket->deleteLater();
        startPost(promise, url, body, apiKey, responseTimeoutMs, trace);
    });

    timer->start(connectionTimeoutMs);
    socket->connectToHost(url.host(), port);
    return future;
}

void LlamaClient::startPost(
    const std::shared_ptr<QPromise<HttpResponse>> &promise, const QUrl &url, const QByteArray &body, const QString &apiKey, int responseTimeoutMs, const std::shared_ptr<RequestJournal::Record> &trace)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());

    QNetworkReply *reply = m_network.post(request, body);
    m_currentReply = reply;

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("llocrTimedOut", true);
        reply->abort();
    });
    if (responseTimeoutMs > 0)
        timer->start(responseTimeoutMs);

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, promise, responseTimeoutMs, trace]() mutable {
        HttpResponse response;
        const QByteArray replyBody = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        trace->status = status;
        trace->responseBytes = replyBody.size();
        trace->durationMs = trace->at.msecsTo(QDateTime::currentDateTime());
        if (reply->error() != QNetworkReply::NoError) {
            QString error;
            if (reply->error() == QNetworkReply::OperationCanceledError && reply->property("llocrTimedOut").toBool()) {
                error = QCoreApplication::translate("LlamaClient", "Request timed out after %1 ms").arg(responseTimeoutMs);
            } else {
                error = reply->errorString();
                const QString serverError = extractServerError(replyBody);
                if (!serverError.isEmpty())
                    error += QStringLiteral("\nServer: ") + serverError;
            }
            response.error = error;
            trace->error = error.section(QLatin1Char('\n'), 0, 0);
        } else {
            response.success = true;
            response.body = replyBody;
        }
        trace->excerpt = RequestJournal::excerptOf(replyBody);
        RequestJournal::instance().record(*trace);
        promise->addResult(std::move(response));
        promise->finish();
        reply->deleteLater();
    });
}

void LlamaClient::abort()
{
    if (m_currentReply)
        m_currentReply->abort();
    if (m_connectSocket) {
        m_connectSocket->disconnect();
        m_connectSocket->deleteLater();
    }
    m_connectSocket.clear();
    if (auto promise = m_connectPromise.lock()) {
        promise->addResult(HttpResponse{false, {}, QStringLiteral("Operation canceled")});
        promise->finish();
    }
    m_connectPromise.reset();
}

QString LlamaClient::extractServerError(const QByteArray &responseData)
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject())
        return QString();
    const QJsonValue error = doc.object().value(QStringLiteral("error"));
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    if (error.isString())
        return error.toString();
    return QString();
}

}  // namespace llocr
