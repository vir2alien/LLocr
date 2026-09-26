#pragma once

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QString>

#include <memory>

#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"
#include "core/OcrRequest.h"
#include "core/OcrResult.h"

namespace llocr {

struct OcrPromptVariant {
    QString id;
    QString title;
    QString text;
};

class OcrModel {
    Q_DISABLE_COPY_MOVE(OcrModel)

public:
    OcrModel() = default;
    virtual ~OcrModel() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QList<OcrPromptVariant> promptVariants() const = 0;
    virtual QString defaultParserId() const = 0;

    QFuture<OcrResult> recognize(const OcrRequest &request,
                                 const ConnectionConfig &config);
    void abort();

protected:
    // Deliberately static, not virtual: recognize() must complete even after the
    // adapter is destroyed (shutdown with a request in flight), so nothing in the
    // reply path may depend on `this`. A new wire shape is added by overriding
    // recognize() in the subclass, or by introducing a sibling adapter that
    // reuses runChatExchange (ADR 111).
    static QByteArray buildRequestBody(const OcrRequest &request,
                                       const QByteArray &imageDataUrl);
    static OcrResult parseResponse(const QByteArray &responseData);

private:
    std::shared_ptr<LlamaClient> m_activeClient;
};

} // namespace llocr
