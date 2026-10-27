#pragma once

#include <QByteArray>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QPromise>
#include <QString>
#include <QUrl>

#include "core/RequestJournal.h"

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
    //
    // Every call leaves a trace in the request journal: the endpoint, the
    // model the request named, the sizes, the status and the reply's head.
    // The key never reaches the trace.
    QFuture<HttpResponse> postJson(const QUrl &url, const QByteArray &body, const QString &apiKey, const QString &modelId, int connectionTimeoutMs, int responseTimeoutMs);

    void abort();

private:
    void startPost(const std::shared_ptr<QPromise<HttpResponse>> &promise,
                   const QUrl &url,
                   const QByteArray &body,
                   const QString &apiKey,
                   int responseTimeoutMs,
                   const std::shared_ptr<RequestJournal::Record> &trace);

    static QString extractServerError(const QByteArray &responseData);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_currentReply;
    // The request state itself lives in the postJson lambdas; these two only
    // let abort() reach the newest connect phase.
    QPointer<QTcpSocket> m_connectSocket;
    std::weak_ptr<QPromise<HttpResponse>> m_connectPromise;
};

}  // namespace llocr
