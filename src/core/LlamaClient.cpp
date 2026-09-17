#include "core/LlamaClient.h"

#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QCoreApplication>
#include <QTimer>

namespace llocr {

QUrl LlamaClient::endpointUrl(const QString &baseUrl)
{
    QString base = baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    return QUrl(base + QStringLiteral("/v1/chat/completions"));
}

QFuture<HttpResponse> LlamaClient::postJson(const QUrl &url, const QByteArray &body,
                                            const QString &apiKey, int timeoutMs)
{
    auto promise = std::make_shared<QPromise<HttpResponse>>();
    promise->start();
    QFuture<HttpResponse> future = promise->future();

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
    timer->start(timeoutMs);

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, promise, timeoutMs]() mutable {
        HttpResponse response;
        if (reply->error() != QNetworkReply::NoError) {
            QString error;
            if (reply->error() == QNetworkReply::OperationCanceledError
                && reply->property("llocrTimedOut").toBool()) {
                error = QCoreApplication::translate("LlamaClient",
                            "Request timed out after %1 ms").arg(timeoutMs);
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

    return future;
}

void LlamaClient::abort()
{
    if (m_currentReply)
        m_currentReply->abort();
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

} // namespace llocr
