#include "models/FixModel.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <utility>

#include "core/ModelProfiles.h"
#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

namespace llocr {

QFuture<CheckResult> FixModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        m_activeClient,
                                        [](const QByteArray &body) { return parseResponse(body); },
                                        QCoreApplication::translate("FixModel", "Failed to encode the block image"));
}

QByteArray FixModel::buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl)
{
    // Fix protocol (ADR 163): the candidate travels as its own user-message
    // part, framed by <ocr_candidate>; the system message carries the shared
    // contract; the parts follow the order the model's fix role trains.
    const ModelProfiles::Role *role = ModelProfiles::roleFor(request.modelId, QStringLiteral("fix"));
    const bool promptFirst = role && role->promptBeforeImage;

    const QJsonObject typePromptPart = textPart(request.typePrompt);
    const QJsonObject image = imagePart(imageDataUrl);
    const QJsonObject ocrPart = textPart(QStringLiteral("OCR candidate:\n<ocr_candidate>\n%1\n</ocr_candidate>").arg(request.recognizedText));

    QJsonArray content;
    if (promptFirst) {
        if (!request.typePrompt.isEmpty())
            content.append(typePromptPart);
        content.append(ocrPart);
        content.append(image);
    } else {
        if (!request.typePrompt.isEmpty())
            content.append(typePromptPart);
        content.append(image);
        content.append(ocrPart);
    }

    QJsonArray messages;
    if (!request.systemPrompt.isEmpty())
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.systemPrompt}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), std::move(content)}});

    return QJsonDocument(buildRoot(request, std::move(messages))).toJson(QJsonDocument::Compact);
}

CheckResult FixModel::parseResponse(const QByteArray &responseData)
{
    QString content;
    CheckResult error;
    if (!replyContent(responseData, &content, &error))
        return error;

    if (content.isEmpty())
        return CheckResult::makeError(StatusMessage::translate("FixModel",
                                                               "The model returned no corrected text, only end-of-sentence markers. "
                                                               "Check that the selected model can process images."));

    const QString cleaned = stripOuterCodeFence(stripControlTokens(content));
    const QString trimmed = cleaned.trimmed();
    const QString upper = trimmed.toUpper();

    if (upper.startsWith(QStringLiteral("OK"))) {
        CheckResult ok;
        ok.status = CheckStatus::Ok;
        // Recognition is already correct — do not touch the original text.
        return ok;
    }

    if (upper.startsWith(QStringLiteral("REVIEW"))) {
        CheckResult review;
        review.status = CheckStatus::Review;
        return review;
    }

    if (upper.startsWith(QStringLiteral("FIX"))) {
        QString fixed = trimmed.mid(3).trimmed();
        // Accept both "FIX: ..." and "FIX\n..." spellings.
        if (fixed.startsWith(QLatin1Char(':')))
            fixed = fixed.mid(1).trimmed();
        if (fixed.isEmpty()) {
            // The verdict is explicit but the payload is missing (small models
            // sometimes stop after the keyword): the block needs human eyes,
            // not a protocol error.
            CheckResult review;
            review.status = CheckStatus::Review;
            return review;
        }
        CheckResult fix;
        fix.status = CheckStatus::Fixed;
        fix.text = fixed;
        return fix;
    }

    return CheckResult::makeError(StatusMessage::translate("FixModel", "Unexpected verifier response\u2014expected OK, FIX or REVIEW. Received: %1").arg(cleaned.left(120)));
}

}  // namespace llocr
