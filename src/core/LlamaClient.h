#pragma once

#include <QByteArray>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace llocr {

struct HttpResponse {
    bool success = false;
    QByteArray body;
    QString error;
};

class LlamaClient {
    Q_DISABLE_COPY_MOVE(LlamaClient)

public:
    LlamaClient() = default;

    static QUrl endpointUrl(const QString &baseUrl);

    QFuture<HttpResponse> postJson(const QUrl &url, const QByteArray &body,
                                   const QString &apiKey, int timeoutMs);

    void abort();

private:
    static QString extractServerError(const QByteArray &responseData);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_currentReply;
};

} // namespace llocr
