#pragma once

#include <QCoreApplication>
#include <QFuture>
#include <QFutureWatcher>
#include <QPromise>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"

#include <memory>
#include <utility>

namespace llocr {

template <typename Result, typename BuildBody, typename ParseResponse>
QFuture<Result> runChatExchange(BuildBody buildBody,
                                const ConnectionConfig &config,
                                const std::shared_ptr<LlamaClient> &client,
                                ParseResponse parseResponse,
                                QString encodeError,
                                const QString &endpointPath = QStringLiteral("/v1/chat/completions"))
{
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    QFuture<Result> future = promise->future();

    auto *encodeWatcher = new QFutureWatcher<QByteArray>();
    QObject::connect(encodeWatcher, &QFutureWatcher<QByteArray>::finished, encodeWatcher, [promise, encodeWatcher, client, config, parseResponse, encodeError, endpointPath]() {
        encodeWatcher->deleteLater();
        const QByteArray body = encodeWatcher->future().resultCount() > 0 ? encodeWatcher->result() : QByteArray();
        if (body.isEmpty()) {
            promise->addResult(Result::makeError(encodeError));
            promise->finish();
            return;
        }
        auto *watcher = new QFutureWatcher<HttpResponse>();
        QObject::connect(watcher, &QFutureWatcher<HttpResponse>::finished, watcher, [client, promise, watcher, parseResponse]() {
            const HttpResponse response = watcher->future().resultCount() > 0 ? watcher->result() : HttpResponse{};
            promise->addResult(response.success ? parseResponse(response.body) : Result::makeError(response.error));
            promise->finish();
            watcher->deleteLater();
        });
        watcher->setFuture(client->postJson(LlamaClient::endpointUrl(config.baseUrl, endpointPath), body, config.apiKey, config.timeoutMs));
    });
    encodeWatcher->setFuture(QtConcurrent::run(std::move(buildBody)));

    return future;
}

}  // namespace llocr
