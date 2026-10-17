#include "models/BlockRecognitionModel.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <utility>

#include "core/ModelProfiles.h"
#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

namespace llocr {

QFuture<CheckResult> BlockRecognitionModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        m_activeClient,
                                        [](const QByteArray &body) { return parseResponse(body); },
                                        QCoreApplication::translate("BlockRecognitionModel", "Failed to encode the block image"));
}

QByteArray BlockRecognitionModel::buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl)
{
    // The type prompt carries the block's recognition wording (the profile's
    // blockPrompts). An empty prompt is a valid transcription request
    // (LightOnOCR-3 trains "the image only") — empty parts are dropped rather
    // than sent.
    const ModelProfiles::Role *role = ModelProfiles::roleFor(request.modelId, QStringLiteral("blockRecognition"));
    const bool promptFirst = role && role->promptBeforeImage;

    QJsonArray content;
    if (promptFirst && !request.typePrompt.isEmpty())
        content.append(textPart(request.typePrompt));
    content.append(imagePart(imageDataUrl));
    if (!promptFirst && !request.typePrompt.isEmpty())
        content.append(textPart(request.typePrompt));

    QJsonArray messages;
    if (!request.systemPrompt.isEmpty())
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.systemPrompt}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), std::move(content)}});

    return QJsonDocument(buildRoot(request, std::move(messages))).toJson(QJsonDocument::Compact);
}

CheckResult BlockRecognitionModel::parseResponse(const QByteArray &responseData)
{
    QString content;
    CheckResult error;
    if (!replyContent(responseData, &content, &error))
        return error;

    const QString cleaned = stripOuterCodeFence(stripControlTokens(content));

    CheckResult result;
    if (cleaned.isEmpty()) {
        result.status = CheckStatus::Review;
        return result;
    }
    result.status = CheckStatus::Recognized;
    result.text = cleaned.trimmed();
    return result;
}

}  // namespace llocr
