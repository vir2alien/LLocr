#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

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
    explicit OpenAiProvider(QObject *parent = nullptr);

    // ILlmProvider
    QFuture<OcrResult> recognize(const OcrRequest &request, const ProviderConfig &config) override;
    QString name() const override;

    void abort();

private:
    QUrl endpointUrl(const QString &baseUrl) const;

    static QString encodeImageDataUrl(const QImage& image, const QString& format);

    static QByteArray buildRequestBody(const OcrRequest& request,
                                       const QString& imageDataUrl);

    static OcrResult parseResponse(const QByteArray& responseData);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_currentReply;
};

} // namespace llocr
