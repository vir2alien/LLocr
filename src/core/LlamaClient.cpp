#include "core/LlamaClient.h"

#include <memory>

#include <QCoreApplication>
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

QFuture<HttpResponse> LlamaClient::postJson(const QUrl &url, const QByteArray &body, const QString &apiKey, int connectionTimeoutMs, int responseTimeoutMs)
{
    auto promise = std::make_shared<QPromise<HttpResponse>>();
    promise->start();
    QFuture<HttpResponse> future = promise->future();

    auto *socket = new QTcpSocket(&m_network);
    m_connectSocket = socket;
    m_connectPromise = promise;
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    const bool https = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const quint16 port = quint16(url.port(https ? 443 : 80));

    QObject::connect(timer, &QTimer::timeout, socket, [this, url, connectionTimeoutMs]() {
        finishConnectPhase(HttpResponse{false, {}, QCoreApplication::translate("LlamaClient", "Connection to %1 timed out after %2 ms").arg(url.toString()).arg(connectionTimeoutMs)});
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket, [this, url](QAbstractSocket::SocketError) {
        QString error = QCoreApplication::translate("LlamaClient", "Connection to %1 failed: %2").arg(url.toString(), m_connectSocket ? m_connectSocket->errorString() : QString());
        finishConnectPhase(HttpResponse{false, {}, std::move(error)});
    });
    QObject::connect(socket, &QTcpSocket::connected, socket, [this, socket, timer, promise, url, body, apiKey, responseTimeoutMs]() {
        timer->stop();
        m_connectPromise.reset();
        m_connectSocket.clear();
        socket->abort();
        socket->deleteLater();
        startPost(promise, url, body, apiKey, responseTimeoutMs);
    });

    timer->start(connectionTimeoutMs);
    socket->connectToHost(url.host(), port);
    return future;
}

void LlamaClient::finishConnectPhase(const HttpResponse &response)
{
    if (m_connectSocket) {
        m_connectSocket->disconnect();
        m_connectSocket->deleteLater();
    }
    m_connectSocket.clear();
    if (!m_connectPromise)
        return;
    m_connectPromise->addResult(response);
    m_connectPromise->finish();
    m_connectPromise.reset();
}

void LlamaClient::startPost(const std::shared_ptr<QPromise<HttpResponse>> &promise, const QUrl &url, const QByteArray &body, const QString &apiKey, int responseTimeoutMs)
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

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, promise, responseTimeoutMs]() mutable {
        HttpResponse response;
        if (reply->error() != QNetworkReply::NoError) {
            QString error;
            if (reply->error() == QNetworkReply::OperationCanceledError && reply->property("llocrTimedOut").toBool()) {
                error = QCoreApplication::translate("LlamaClient", "Request timed out after %1 ms").arg(responseTimeoutMs);
            } else {
                error = reply->errorString();
                const QString serverError = extractServerError(reply->readAll());
                if (!serverError.isEmpty())
                    error += QStringLiteral("\nServer: ") + serverError;
            }
            response.error = error;
        } else {
            response.success = true;
            response.body = reply->readAll();
        }
        promise->addResult(std::move(response));
        promise->finish();
        reply->deleteLater();
    });
}

void LlamaClient::abort()
{
    if (m_currentReply)
        m_currentReply->abort();
    if (m_connectSocket)
        finishConnectPhase(HttpResponse{false, {}, QStringLiteral("Operation canceled")});
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
