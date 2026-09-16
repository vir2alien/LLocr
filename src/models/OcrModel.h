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
    static QByteArray buildRequestBody(const OcrRequest &request,
                                       const QString &imageDataUrl);
    static OcrResult parseResponse(const QByteArray &responseData);

private:
    static QString encodeImageDataUrl(const QImage &image, const QString &format, int quality = -1);

private:
    std::shared_ptr<LlamaClient> m_activeClient;
};

} // namespace llocr
