#include "models/VisionChatModel.h"

#include <QJsonDocument>

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

    root.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}});

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(), [](const RequestParameter &a, const RequestParameter &b) { return a.order < b.order; });
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
        *error = CheckResult::makeError(StatusMessage::translate("VisionChatModel", "No choices in response"));
        return false;
    }

    *content = choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    return true;
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

}  // namespace llocr
