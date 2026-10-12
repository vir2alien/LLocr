#pragma once

#include <QByteArray>
#include <QFuture>
#include <QImage>
#include <QList>
#include <QString>

#include <memory>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"

namespace llocr {

class GeneralPurposeModel
{
    Q_DISABLE_COPY_MOVE(GeneralPurposeModel)

public:
    GeneralPurposeModel() = default;
    ~GeneralPurposeModel() = default;

    QFuture<CheckResult> check(const CheckRequest &request, const ConnectionConfig &config);

    void abort();

protected:
    static QByteArray buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);

private:
    // One client (and its QNAM) for the model's lifetime: the per-request
    // client dropped the HTTP keep-alive, so every block opened a fresh
    // TCP connection. Requests on one model are serial (busy-gated).
    std::shared_ptr<LlamaClient> m_activeClient = std::make_shared<LlamaClient>();
};

}  // namespace llocr
