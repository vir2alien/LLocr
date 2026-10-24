#pragma once

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"
#include "core/ModelProfiles.h"
#include "core/OcrRequest.h"
#include "core/OcrResult.h"

namespace llocr {

class OcrModel
{
    Q_DISABLE_COPY_MOVE(OcrModel)

public:
    explicit OcrModel(QString modelId);
    ~OcrModel() = default;

    static std::unique_ptr<OcrModel> create(const QString &modelId);
    static QString defaultId();
    static QStringList registeredIds();

    QString id() const { return m_id; }
    QString displayName() const;
    QList<ModelProfiles::Prompt> promptVariants() const;
    QString defaultParserId() const;

    // The parser the profile declares for the page task. It reads the profile
    // only — no model, and no network manager with it, is built for it
    // (ADR 207).
    static QString defaultParserIdFor(const QString &modelId);

    QFuture<OcrResult> recognize(const OcrRequest &request, const ConnectionConfig &config);
    void abort();

protected:
    static QByteArray buildRequestBody(const OcrRequest &request, const QByteArray &imageDataUrl);
    static OcrResult parseResponse(const QByteArray &responseData);

private:
    QString m_id;
    // One client (and its QNAM) for the model's lifetime: the per-request
    // client dropped the HTTP keep-alive, so every page/block opened a fresh
    // TCP connection. Requests on one model are serial (busy-gated).
    std::shared_ptr<LlamaClient> m_activeClient = std::make_shared<LlamaClient>();
};

}  // namespace llocr
