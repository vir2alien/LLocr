#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include "core/ProviderConfig.h"
#include "providers/ILlmProvider.h"

class QNetworkReply;

namespace llocr {

/**
 * @brief LLM provider for any OpenAI-compatible chat completions endpoint.
 *
 * This provider is transport-only.
 */
class OpenAiProvider : public QObject, public ILlmProvider
{
    Q_OBJECT

public:
    explicit OpenAiProvider(ProviderConfig config, QObject* parent = nullptr);

    // ILlmProvider
    QFuture<OcrResult> recognize(const OcrRequest& request) override;
    QString name() const override;

    void setConfig(ProviderConfig config);
    const ProviderConfig& config() const;

    void abort();

private:
    QUrl endpointUrl() const;

    static QString encodeImageDataUrl(const QImage& image, const QString& format);

    static QByteArray buildRequestBody(const OcrRequest& request,
                                       const QString& imageDataUrl);

    static OcrResult parseResponse(const QByteArray& responseData);

    ProviderConfig m_config;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_currentReply;
};

} // namespace llocr
