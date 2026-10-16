#include "models/GeneralPurposeModel.h"

#include "core/ModelProfiles.h"
#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include "core/ServiceMarkers.h"

#include <algorithm>
#include <memory>

namespace llocr {

namespace {

QString stripControlTokens(const QString &text)
{
    static const QRegularExpression thinkBlock(QStringLiteral(R"( thinking[\s\S]*? response\s*)"));

    QString out = stripServiceTokens(text);
    out.remove(thinkBlock);

    const int thinkStart = out.indexOf(QStringLiteral(" thinking"));
    if (thinkStart >= 0)
        out.truncate(thinkStart);

    return out.trimmed();
}

// Small VL models wrap whole replies — tables above all — in a Markdown code
// fence. The fence is packaging, not block text: when it is the entire reply,
// unwrap it. A fence that covers only part of the reply is content and stays.
QString stripOuterCodeFence(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QStringLiteral("```")))
        return text;
    const int openEnd = trimmed.indexOf(QLatin1Char('\n'));
    if (openEnd < 0)
        return text;
    const int closeStart = trimmed.lastIndexOf(QLatin1Char('\n'));
    if (closeStart <= openEnd)
        return text;
    if (trimmed.mid(closeStart + 1).trimmed() != QStringLiteral("```"))
        return text;
    if (trimmed.left(openEnd).mid(3).trimmed().contains(QLatin1Char('`')))
        return text;
    return trimmed.mid(openEnd + 1, closeStart - openEnd - 1).trimmed();
}

}  // namespace

QByteArray GeneralPurposeModel::buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl)
{
    // Recognition protocol: the reply itself is the block text. Fix protocol
    // (ADR 163): the model answers with exactly one of
    //   OK
    //   FIX\n<the complete corrected block>
    //   REVIEW
    // and the candidate travels as its own request part, framed by
    // <ocr_candidate>. The system message carries the shared contract; the
    // user message lists the type prompt, the block image and — for fix — the
    // OCR candidate, in the order the model's fix role trains.

    QJsonObject imageUrl{{QStringLiteral("url"), QString::fromUtf8(imageDataUrl)}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), imageUrl}};
    QJsonObject typePromptPart{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), request.typePrompt}};

    const bool fix = request.protocol == CheckRequest::Protocol::Fix;
    const QString roleName = fix ? QStringLiteral("fix") : QStringLiteral("blockRecognition");
    const ModelProfiles::Role *role = ModelProfiles::roleFor(request.modelId, roleName);
    QJsonObject ocrPart{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("OCR candidate:\n<ocr_candidate>\n%1\n</ocr_candidate>").arg(request.recognizedText)}};
    // An empty prompt is the transcription request itself (LightOnOCR-3 trains
    // "the image only") — empty text parts and an empty system message are
    // dropped rather than sent.
    const bool promptFirst = role && role->promptBeforeImage;
    QJsonArray content;
    if (fix) {
        if (promptFirst) {
            if (!request.typePrompt.isEmpty())
                content.append(typePromptPart);
            content.append(ocrPart);
            content.append(imagePart);
        } else {
            if (!request.typePrompt.isEmpty())
                content.append(typePromptPart);
            content.append(imagePart);
            content.append(ocrPart);
        }
    } else if (promptFirst) {
        if (!request.typePrompt.isEmpty())
            content.append(typePromptPart);
        content.append(imagePart);
    } else {
        content.append(imagePart);
        if (!request.typePrompt.isEmpty())
            content.append(typePromptPart);
    }

    QJsonObject userMessage{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};

    QJsonArray messages;
    if (!request.systemPrompt.isEmpty()) {
        QJsonObject systemMessage{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.systemPrompt}};
        messages.append(systemMessage);
    }
    messages.append(userMessage);

    QJsonObject root{{QStringLiteral("model"), request.modelId}, {QStringLiteral("messages"), messages}};

    root.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}});

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(), [](const RequestParameter &a, const RequestParameter &b) { return a.order < b.order; });
    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

CheckResult GeneralPurposeModel::parseResponse(const QByteArray &responseData, CheckRequest::Protocol protocol, bool structuralReply)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return CheckResult::makeError(StatusMessage::translate("GeneralPurposeModel", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return CheckResult::makeError(StatusMessage::translate("GeneralPurposeModel", "No choices in response"));

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString rawContent = message.value(QStringLiteral("content")).toString();
    // A structural reply feeds a parser that reads the <|...|> markup itself;
    // the service-token cleanup would delete exactly the data it needs.
    const QString content = structuralReply ? stripOuterCodeFence(rawContent) : stripOuterCodeFence(stripControlTokens(rawContent));

    if (protocol == CheckRequest::Protocol::Recognition) {
        // The block-recognition contract: the reply IS the block text. An
        // empty reply (nothing but end-of-sentence markers, or the model
        // refusing) means the block could not be transcribed — that is a
        // verdict, not an error.
        CheckResult result;
        if (content.isEmpty()) {
            result.status = CheckStatus::Review;
            return result;
        }
        result.status = CheckStatus::Recognized;
        result.text = content.trimmed();
        return result;
    }

    if (content.isEmpty())
        return CheckResult::makeError(StatusMessage::translate("GeneralPurposeModel",
                                                               "The model returned no corrected text, only end-of-sentence markers. "
                                                               "Check that the selected model can process images."));

    const QString trimmed = content.trimmed();
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

    return CheckResult::makeError(StatusMessage::translate("GeneralPurposeModel", "Unexpected verifier response\u2014expected OK, FIX or REVIEW. Received: %1").arg(content.left(120)));
}

QFuture<CheckResult> GeneralPurposeModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        m_activeClient,
                                        [request](const QByteArray &body) { return parseResponse(body, request.protocol, request.structuralReply); },
                                        QCoreApplication::translate("GeneralPurposeModel", "Failed to encode the block image"));
}

void GeneralPurposeModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
