#pragma once

#include <QByteArray>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QPromise>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QTcpSocket;

namespace llocr {

struct HttpResponse {
    bool success = false;
    QByteArray body;
    QString error;
};

class LlamaClient
{
    Q_DISABLE_COPY_MOVE(LlamaClient)

public:
    LlamaClient() = default;

    static QUrl endpointUrl(const QString &baseUrl, const QString &endpointPath = QStringLiteral("/v1/chat/completions"));

    // connectionTimeoutMs caps the TCP connect (a dead/wrong URL fails fast);
    // responseTimeoutMs caps the whole request — llama.cpp sends nothing until
    // generation finishes, so it must cover a full slow decode.
    QFuture<HttpResponse> postJson(const QUrl &url, const QByteArray &body, const QString &apiKey, int connectionTimeoutMs, int responseTimeoutMs);

    void abort();

private:
    void startPost(const std::shared_ptr<QPromise<HttpResponse>> &promise, const QUrl &url, const QByteArray &body, const QString &apiKey, int responseTimeoutMs);
    void finishConnectPhase(const HttpResponse &response);

    static QString extractServerError(const QByteArray &responseData);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_currentReply;
    QPointer<QTcpSocket> m_connectSocket;
    std::shared_ptr<QPromise<HttpResponse>> m_connectPromise;
};

}  // namespace llocr
