#include "models/DecisionModel.h"

#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

#include <algorithm>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace llocr {

QByteArray DecisionModel::buildRequestBody(const DecisionRequest &request, const QByteArray &imageDataUrl, const QByteArray &renderImageDataUrl, const QString &modelId)
{
    // The visual-diff decision model (docs/31): the task wording is part
    // of the model's own prompt, so the state is the bare OCR text and the
    // question carries no instructions or criteria — the server rejects a
    // request that sends either. The noul head answers P(the candidate is
    // consistent with the crop) in one pass; the render rides next to the
    // crop, in that order.
    QJsonObject questions;
    questions.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("type"), QStringLiteral("noul")}});
    QJsonObject root;
    if (!modelId.isEmpty())
        root.insert(QStringLiteral("model"), modelId);
    root.insert(QStringLiteral("state"), request.recognizedText);
    root.insert(QStringLiteral("images"), QJsonArray{QString::fromUtf8(imageDataUrl), QString::fromUtf8(renderImageDataUrl)});
    root.insert(QStringLiteral("questions"), questions);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

namespace {

// The endpoint spelled the probability differently across versions: a bare
// number, {"noul": x} or {"probability": x}. Returns false for anything else.
bool readNoul(const QJsonValue &answer, double &probability)
{
    if (answer.isDouble()) {
        probability = answer.toDouble();
        return true;
    }
    if (!answer.isObject())
        return false;
    const QJsonObject object = answer.toObject();
    const QJsonValue value = object.contains(QStringLiteral("noul")) ? object.value(QStringLiteral("noul")) : object.value(QStringLiteral("probability"));
    if (!value.isDouble())
        return false;
    probability = value.toDouble();
    return true;
}

}  // namespace

DecisionResult DecisionModel::parseResponse(const QByteArray &responseData, const QString &answerKey)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("answers")))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "No answers in response"));

    const QJsonObject answers = root.value(QStringLiteral("answers")).toObject();
    const QJsonValue match = answers.value(answerKey);
    if (match.isUndefined())
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "No match answer in response"));

    double probability = 0.0;
    if (!readNoul(match, probability))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));

    DecisionResult result;
    result.ok = true;
    result.probability = std::clamp(probability, 0.0, 1.0);
    return result;
}

QFuture<DecisionResult> DecisionModel::judge(const DecisionRequest &request, const ConnectionConfig &config)
{
    // The answer sits under the question's own name: the verification asks one
    // yes/no question and the answer keeps the name it always had.
    const QString answerKey = QStringLiteral("match");
    const QString modelId = config.modelId;
    Q_ASSERT(request.visualDiff);
    // The render of a visual-diff request is encoded on the worker thread next
    // to the crop — both pictures travel on the wire.
    return runChatExchange<DecisionResult>([request, modelId]() { return buildRequestBody(request, encodeImageDataUrl(request.image), encodeImageDataUrl(request.renderImage), modelId); },
                                           config,
                                           m_activeClient,
                                           [answerKey](const QByteArray &body) { return parseResponse(body, answerKey); },
                                           QCoreApplication::translate("DecisionModel", "Failed to encode the block image"),
                                           QStringLiteral("/v1/systemone"));
}

void DecisionModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
