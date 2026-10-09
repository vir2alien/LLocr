#pragma once

#include <QByteArray>
#include <QFuture>
#include <QImage>
#include <QString>

#include <memory>

#include "core/ConnectionConfig.h"
#include "core/DecisionRequest.h"
#include "core/DecisionResult.h"
#include "core/LlamaClient.h"

namespace llocr {

// The decision-model client: one /v1/systemone request per block (ADR 147).
// The endpoint is a llama.cpp extension — an OpenAI-compatible server that
// only speaks /v1/chat/completions answers 404 and every judge fails.
class DecisionModel
{
    Q_DISABLE_COPY_MOVE(DecisionModel)

public:
    DecisionModel() = default;
    ~DecisionModel() = default;

    QFuture<DecisionResult> judge(const DecisionRequest &request, const ConnectionConfig &config);

    void abort();

protected:
    static QByteArray buildRequestBody(const DecisionRequest &request, const QByteArray &imageDataUrl);
    static DecisionResult parseResponse(const QByteArray &responseData);

private:
    std::shared_ptr<LlamaClient> m_activeClient;
};

}  // namespace llocr
