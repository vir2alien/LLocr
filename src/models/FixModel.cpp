#include "models/FixModel.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <utility>

#include "core/ModelProfiles.h"
#include "models/ChatExchange.h"
#include "models/FixDiffGate.h"
#include "models/ImageDataUrl.h"

namespace llocr {

namespace {

// The server stopped at the token budget: the reply carries a fragment of the
// block, not a correction (ADR 192).
bool replyCutByBudget(const QByteArray &responseData)
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return false;
    return choices.first().toObject().value(QStringLiteral("finish_reason")).toString() == QLatin1String("length");
}

}  // namespace

QFuture<CheckResult> FixModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        m_activeClient,
                                        [request](const QByteArray &body) { return parseResponse(body, request.recognizedText, request.blockType); },
                                        QCoreApplication::translate("FixModel", "Failed to encode the block image"));
}

QByteArray FixModel::buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl)
{
    // Fix protocol (ADR 163): the candidate travels as its own user-message
    // part, framed by <CANDIDATE>; the system message carries the shared
    // contract; the parts follow the order the model's fix role trains.
    const ModelProfiles::Role *role = ModelProfiles::roleFor(request.modelId, QStringLiteral("fix"));
    const bool promptFirst = role && role->promptBeforeImage;

    const QJsonObject typePromptPart = textPart(request.typePrompt);
    const QJsonObject image = imagePart(imageDataUrl);
    const QJsonObject ocrPart = textPart(QStringLiteral("OCR candidate:\n<CANDIDATE>\n%1\n</CANDIDATE>").arg(request.recognizedText));

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

CheckResult FixModel::parseResponse(const QByteArray &responseData, const QString &candidate, const QString &blockType)
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
        QString body = trimmed.mid(3).trimmed();
        // Accept both "FIX: ..." and "FIX\n..." spellings.
        if (body.startsWith(QLatin1Char(':')))
            body = body.mid(1).trimmed();
        if (body.isEmpty()) {
            // The verdict is explicit but the payload is missing (small models
            // sometimes stop after the keyword): the block needs human eyes,
            // not a protocol error.
            CheckResult review;
            review.status = CheckStatus::Review;
            return review;
        }
        if (replyCutByBudget(responseData)) {
            // The reply was cut mid-block: a fragment is not a correction.
            CheckResult review;
            review.status = CheckStatus::Review;
            review.notes = StatusMessage::translate("FixModel", "The reply was cut by the token budget; the block needs a human look.");
            return review;
        }
        const FixDiffGate::Outcome outcome = FixDiffGate::fixBlock(candidate, body, blockType);
        CheckResult result;
        result.status = outcome.status;
        if (outcome.status == CheckStatus::Fixed)
            result.text = outcome.text;
        result.notes = outcome.downgradeReason;
        return result;
    }

    // Anything outside the OK / FIX / REVIEW scheme is a deviation, not a
    // protocol error: the block goes to human eyes.
    CheckResult review;
    review.status = CheckStatus::Review;
    review.notes = StatusMessage::translate("FixModel", "The reply does not follow the OK/FIX/REVIEW scheme: %1").arg(cleaned.left(120));
    return review;
}

}  // namespace llocr
