#pragma once

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QString>

#include <memory>

#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"
#include "core/ModelProfiles.h"
#include "core/OcrRequest.h"
#include "core/OcrResult.h"

namespace llocr {

// The OCR transport for every model. What a model says — its prompts, its
// parser — comes from its profile in ":/profiles/models", not from a subclass:
// a model whose reply shape differs gets a sibling adapter instead (ADR 89).
class OcrModel
{
    Q_DISABLE_COPY_MOVE(OcrModel)

public:
    explicit OcrModel(QString modelId);
    ~OcrModel() = default;

    QString id() const { return m_id; }
    QString displayName() const;
    QList<ModelProfiles::Prompt> promptVariants() const;
    QString defaultParserId() const;

    QFuture<OcrResult> recognize(const OcrRequest &request, const ConnectionConfig &config);
    void abort();

protected:
    static QByteArray buildRequestBody(const OcrRequest &request, const QByteArray &imageDataUrl);
    static OcrResult parseResponse(const QByteArray &responseData);

private:
    QString m_id;
    std::shared_ptr<LlamaClient> m_activeClient;
};

}  // namespace llocr
