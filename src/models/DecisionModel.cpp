#include "models/DecisionModel.h"

#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace llocr {

QByteArray DecisionModel::buildRequestBody(const DecisionRequest &request, const QByteArray &imageDataUrl)
{
    // The state is the OCR candidate — data, never an instruction. LLocr asks
    // exactly one question, "match", of the yes/no kind; its wording travels
    // in the model profile's decision role.
    QJsonObject question{{QStringLiteral("type"), QStringLiteral("noul")}, {QStringLiteral("instructions"), request.question}};
    QJsonObject questions{{QStringLiteral("match"), question}};

    QJsonObject root;
    root.insert(QStringLiteral("state"), request.stateText);
    root.insert(QStringLiteral("images"), QJsonArray{QString::fromUtf8(imageDataUrl)});
    root.insert(QStringLiteral("questions"), questions);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

DecisionResult DecisionModel::parseResponse(const QByteArray &responseData)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("answers")))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "No answers in response"));

    const QJsonValue match = root.value(QStringLiteral("answers")).toObject().value(QStringLiteral("match"));
    if (match.isUndefined())
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "No match answer in response"));

    // The endpoint spelled the probability differently across versions: a bare
    // number, {"noul": x} or {"probability": x}. Anything else is an error,
    // never a silent verdict.
    double probability = 0.0;
    if (match.isDouble()) {
        probability = match.toDouble();
    } else if (match.isObject()) {
        const QJsonObject answer = match.toObject();
        const QJsonValue value = answer.contains(QStringLiteral("noul")) ? answer.value(QStringLiteral("noul")) : answer.value(QStringLiteral("probability"));
        if (!value.isDouble())
            return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));
        probability = value.toDouble();
    } else {
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));
    }

    DecisionResult result;
    result.ok = true;
    result.probability = qBound(0.0, probability, 1.0);
    return result;
}

QFuture<DecisionResult> DecisionModel::judge(const DecisionRequest &request, const ConnectionConfig &config)
{
    auto client = std::make_shared<LlamaClient>();
    m_activeClient = client;

    return runChatExchange<DecisionResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                           config,
                                           client,
                                           [](const QByteArray &body) { return parseResponse(body); },
                                           QCoreApplication::translate("DecisionModel", "Failed to encode the block image"),
                                           QStringLiteral("/v1/systemone"));
}

void DecisionModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
