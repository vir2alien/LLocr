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

/// The round trip every model adapter shares: build the request body on a worker
/// (the image encoding is the expensive part), POST it to
/// `/v1/chat/completions`, then let the adapter parse the reply (ADR 111).
///
/// What varies per adapter — the body, the reply, the error wording — comes in
/// as parameters, so a new model no longer copies the whole promise plumbing.
template <typename Result, typename BuildBody, typename ParseResponse>
QFuture<Result> runChatExchange(BuildBody buildBody,
                                const ConnectionConfig &config,
                                const std::shared_ptr<LlamaClient> &client,
                                ParseResponse parseResponse,
                                QString encodeError)
{
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    QFuture<Result> future = promise->future();

    auto *encodeWatcher = new QFutureWatcher<QByteArray>();
    QObject::connect(encodeWatcher, &QFutureWatcher<QByteArray>::finished, encodeWatcher,
                     [promise, encodeWatcher, client, config, parseResponse, encodeError]() {
        encodeWatcher->deleteLater();
        const QByteArray body = encodeWatcher->future().resultCount() > 0
                                    ? encodeWatcher->result()
                                    : QByteArray();
        if (body.isEmpty()) {
            // encodeError is already translated by the caller; transport errors
            // from LlamaClient come pre-translated too.
            promise->addResult(Result::makeError(encodeError));
            promise->finish();
            return;
        }
        auto *watcher = new QFutureWatcher<HttpResponse>();
        QObject::connect(watcher, &QFutureWatcher<HttpResponse>::finished, watcher,
                         [client, promise, watcher, parseResponse]() {
            const HttpResponse response = watcher->future().resultCount() > 0
                                              ? watcher->result()
                                              : HttpResponse{};
            promise->addResult(response.success ? parseResponse(response.body)
                                               : Result::makeError(response.error));
            promise->finish();
            watcher->deleteLater();
        });
        watcher->setFuture(client->postJson(LlamaClient::endpointUrl(config.baseUrl), body,
                                            config.apiKey, config.timeoutMs));
    });
    encodeWatcher->setFuture(QtConcurrent::run(std::move(buildBody)));

    return future;
}

}  // namespace llocr
