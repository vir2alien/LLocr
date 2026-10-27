#include "models/VisionChatModel.h"

#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

#include "core/LlamaClient.h"
#include "core/RequestProfile.h"
#include "core/ServiceMarkers.h"

namespace llocr {

void VisionChatModel::abort()
{
    m_activeClient->abort();
}

QJsonObject VisionChatModel::buildRoot(const CheckRequest &request, QJsonArray messages)
{
    QJsonObject root{{QStringLiteral("model"), request.modelId}, {QStringLiteral("messages"), std::move(messages)}};

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(), [](const RequestParameter &a, const RequestParameter &b) { return a.order < b.order; });

    // The reasoning budget is also the switch: a budget above zero asks the
    // model to think first. The task that offers the mode owns the budget
    // itself — it adds the thinking to the answer's room and keeps the whole
    // generation inside the server's context window (ADR 193).
    double reasoningBudget = 0.0;
    for (const RequestParameter &parameter : parameters) {
        if (parameter.name == QLatin1String(kReasoningBudgetParameter))
            reasoningBudget = parameter.value.toDouble();
    }
    root.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), reasoningBudget > 0.0}});

    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return root;
}

QJsonObject VisionChatModel::textPart(const QString &text)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
}

QJsonObject VisionChatModel::imagePart(const QByteArray &imageDataUrl)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), QString::fromUtf8(imageDataUrl)}}}};
}

bool VisionChatModel::replyContent(const QByteArray &responseData, QString *content, CheckResult *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = CheckResult::makeError(StatusMessage::translate("VisionChatModel", "Invalid JSON response"));
        return false;
    }

    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        *error = CheckResult::makeError(StatusMessage::translate("VisionChatModel", "No answer in the reply: %1").arg(missingAnswerReason(responseData)));
        return false;
    }

    *content = choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    return true;
}

QString VisionChatModel::missingAnswerReason(const QByteArray &responseData)
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject())
        return StatusMessage::translate("VisionChatModel", "the reply is not a JSON object").text();

    const QJsonObject root = doc.object();
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (error.isObject()) {
        const QString message = error.toObject().value(QStringLiteral("message")).toString();
        return StatusMessage::translate("VisionChatModel", "the server answered with an error: %1").arg(message).text();
    }
    if (error.isString())
        return StatusMessage::translate("VisionChatModel", "the server answered with an error: %1").arg(error.toString()).text();

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        QString body = QString::fromUtf8(responseData).simplified();
        if (body.size() > 200)
            body = body.left(200) + QStringLiteral("…");
        return StatusMessage::translate("VisionChatModel", "the reply has no choices (%1)").arg(body).text();
    }

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    if (!message.value(QStringLiteral("reasoning_content")).toString().isEmpty())
        return StatusMessage::translate("VisionChatModel", "the model spent the token budget on reasoning and returned no answer").text();
    return StatusMessage::translate("VisionChatModel", "the reply carries an empty answer").text();
}

QString VisionChatModel::stripControlTokens(const QString &text)
{
    static const QRegularExpression thinkBlock(QStringLiteral(R"( thinking[\s\S]*? response\s*)"));

    QString out = stripServiceTokens(text);
    out.remove(thinkBlock);

    const int thinkStart = out.indexOf(QStringLiteral(" thinking"));
    if (thinkStart >= 0)
        out.truncate(thinkStart);

    return out.trimmed();
}

QString VisionChatModel::stripOuterCodeFence(const QString &text)
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

QString VisionChatModel::stripEscapedPunctuation(const QString &text)
{
    static const QRegularExpression escaped(QStringLiteral(R"(\$\\([.,;:!?]))"));
    QString out = text;
    out.replace(escaped, QStringLiteral("$\\1"));
    return out;
}

}  // namespace llocr
