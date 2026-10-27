#include "models/FixModel.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

#include "core/ModelProfiles.h"
#include "core/RequestProfile.h"
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

// The thinking took the whole budget and the answer is missing: the reply
// carries reasoning and no content. A server that ignores
// reasoning_budget_tokens spends max_tokens on the reasoning (llama.cpp cuts
// the thinking at the budget; omlx does not).
bool thinkingAteTheAnswer(const QByteArray &responseData)
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject())
        return false;
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return false;
    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    if (!message.value(QStringLiteral("content")).toString().isEmpty())
        return false;
    return !message.value(QStringLiteral("reasoning_content")).toString().isEmpty();
}

int parameterNumber(const QList<RequestParameter> &parameters, const QString &name)
{
    for (const RequestParameter &parameter : parameters)
        if (parameter.name == name)
            return parameter.value.toInt();
    return 0;
}

bool hasParameter(const QList<RequestParameter> &parameters, const QString &name)
{
    for (const RequestParameter &parameter : parameters)
        if (parameter.name == name)
            return true;
    return false;
}

void setParameterNumber(QList<RequestParameter> &parameters, const QString &name, int value)
{
    for (RequestParameter &parameter : parameters) {
        if (parameter.name == name) {
            parameter.value = QVariant(double(value));
            return;
        }
    }
}

// The same request without the thinking: the budget of the thinking goes back
// to the answer, which is what the answer had before the thinking was added.
// A request without its own max_tokens keeps the server's default — inventing
// one would cap the answer at a token.
QList<RequestParameter> withoutThinking(const QList<RequestParameter> &parameters)
{
    QList<RequestParameter> out = parameters;
    const int thinking = parameterNumber(out, QLatin1String(kReasoningBudgetParameter));
    setParameterNumber(out, QLatin1String(kReasoningBudgetParameter), 0);
    if (thinking > 0 && hasParameter(out, QStringLiteral("max_tokens"))) {
        const int answer = qMax(1, parameterNumber(out, QStringLiteral("max_tokens")) - thinking);
        setParameterNumber(out, QStringLiteral("max_tokens"), answer);
    }
    return out;
}

}  // namespace

QFuture<CheckResult> FixModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    const int thinkingBudget = parameterNumber(request.parameters, QLatin1String(kReasoningBudgetParameter));
    // The server has already shown that it ignores the reasoning budget: the
    // thinking is left out from the start, and the user is told why.
    const bool thinkingKnownToFail = thinkingBudget > 0 && m_thinkingIgnoredBudget.value(config.baseUrl) == thinkingBudget;

    CheckRequest effective = request;
    if (thinkingKnownToFail)
        effective.parameters = withoutThinking(request.parameters);

    auto promise = std::make_shared<QPromise<CheckResult>>();
    promise->start();

    auto body = std::make_shared<QByteArray>();
    auto *watcher = new QFutureWatcher<CheckResult>();
    QObject::connect(watcher, &QFutureWatcher<CheckResult>::finished, watcher, [this, request, effective, config, promise, watcher, body, thinkingBudget, thinkingKnownToFail]() mutable {
        watcher->deleteLater();
        CheckResult result = watcher->result();

        if (thinkingKnownToFail) {
            result.notes = StatusMessage::join({result.notes, StatusMessage::translate("FixModel", "The thinking was left out: the server ignores the reasoning budget (reasoning_budget_tokens).")},
                                               QStringLiteral(" "));
            promise->addResult(result);
            promise->finish();
            return;
        }

        if (thinkingBudget > 0 && thinkingAteTheAnswer(*body)) {
            // The thinking spent the whole budget and the answer is missing:
            // the answer is requested again, without the thinking.
            m_thinkingIgnoredBudget.insert(config.baseUrl, thinkingBudget);
            CheckRequest retry = request;
            retry.parameters = withoutThinking(request.parameters);
            auto *second = new QFutureWatcher<CheckResult>();
            QObject::connect(second, &QFutureWatcher<CheckResult>::finished, second, [promise, second]() {
                second->deleteLater();
                CheckResult retryResult = second->result();
                retryResult.notes = StatusMessage::join(
                    {retryResult.notes, StatusMessage::translate("FixModel", "The server ignored the reasoning budget — the answer was requested again without the thinking.")}, QStringLiteral(" "));
                promise->addResult(retryResult);
                promise->finish();
            });
            second->setFuture(exchange(retry, config, nullptr));
            return;
        }

        promise->addResult(result);
        promise->finish();
    });
    watcher->setFuture(exchange(effective, config, body));

    return promise->future();
}

QFuture<CheckResult> FixModel::exchange(const CheckRequest &request, const ConnectionConfig &config, const std::shared_ptr<QByteArray> &rawBody)
{
    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        m_activeClient,
                                        [request, rawBody](const QByteArray &body) {
                                            if (rawBody)
                                                *rawBody = body;
                                            return parseResponse(body, request.recognizedText, request.blockType);
                                        },
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
        return CheckResult::makeError(StatusMessage::translate("FixModel", "The model returned no corrected text: %1").arg(missingAnswerReason(responseData)));

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
