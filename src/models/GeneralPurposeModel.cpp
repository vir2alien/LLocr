#include "models/GeneralPurposeModel.h"

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
    // The block-recognition protocol: the model transcribes the crop from
    // scratch and the reply itself is the block text. The system message
    // carries the shared contract; the user message is the type prompt and
    // the block image — the previously recognized text is not sent.

    QJsonObject imageUrl{{QStringLiteral("url"), QString::fromUtf8(imageDataUrl)}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), imageUrl}};
    QJsonObject typePromptPart{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), request.typePrompt}};

    QJsonArray content{typePromptPart, imagePart};

    QJsonObject systemMessage{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.systemPrompt}};
    QJsonObject userMessage{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};

    QJsonObject root{{QStringLiteral("model"), request.modelId}, {QStringLiteral("messages"), QJsonArray{systemMessage, userMessage}}};

    root.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}});

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(), [](const RequestParameter &a, const RequestParameter &b) { return a.order < b.order; });
    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

CheckResult GeneralPurposeModel::parseResponse(const QByteArray &responseData)
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
    const QString content = stripOuterCodeFence(stripControlTokens(message.value(QStringLiteral("content")).toString()));

    // The block-recognition contract: the reply IS the block text. An empty
    // reply (nothing but end-of-sentence markers, or the model refusing) means
    // the block could not be transcribed — that is a verdict, not an error.
    CheckResult result;
    if (content.isEmpty()) {
        result.status = CheckStatus::Review;
        return result;
    }
    result.status = CheckStatus::Fixed;
    result.text = content.trimmed();
    return result;
}

QFuture<CheckResult> GeneralPurposeModel::check(const CheckRequest &request, const ConnectionConfig &config)
{
    auto client = std::make_shared<LlamaClient>();
    m_activeClient = client;

    return runChatExchange<CheckResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                        config,
                                        client,
                                        [](const QByteArray &body) { return parseResponse(body); },
                                        QCoreApplication::translate("GeneralPurposeModel", "Failed to encode the block image"));
}

void GeneralPurposeModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
