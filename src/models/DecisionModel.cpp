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

QByteArray DecisionModel::buildRequestBody(const DecisionRequest &request, const QByteArray &imageDataUrl, const QString &modelId)
{
    return buildRequestBody(request, imageDataUrl, QByteArray(), modelId);
}

QByteArray DecisionModel::buildRequestBody(const DecisionRequest &request, const QByteArray &imageDataUrl, const QByteArray &renderImageDataUrl, const QString &modelId)
{
    if (!renderImageDataUrl.isEmpty()) {
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

    // The state is a JSON object: the candidate text sits in a named field, so
    // the model reads it as content to compare against the crop — a raw string
    // answers "is there text on the image" instead (docs/17, §4). The request
    // carries ONE question: a noul `match` whose instructions name the axes the
    // state describes. The criteria ride along when the profile ships them.
    QJsonObject state{{QStringLiteral("block_type"), request.blockType}, {QStringLiteral("recognized_text"), request.recognizedText}};
    if (!request.notation.isEmpty())
        state.insert(QStringLiteral("notation"), request.notation);

    QJsonObject question{{QStringLiteral("type"), QStringLiteral("noul")}, {QStringLiteral("instructions"), request.question}};
    if (!request.criteriaTrue.isEmpty() || !request.criteriaFalse.isEmpty()) {
        QJsonObject criteria;
        if (!request.criteriaTrue.isEmpty())
            criteria.insert(QStringLiteral("true"), request.criteriaTrue);
        if (!request.criteriaFalse.isEmpty())
            criteria.insert(QStringLiteral("false"), request.criteriaFalse);
        question.insert(QStringLiteral("criteria"), criteria);
    }
    QJsonObject questions;
    questions.insert(QStringLiteral("match"), question);

    QJsonObject root;
    // The System One protocol names the model that answers (llama.cpp: the
    // loaded model's name or its alias). A server that routes by the field —
    // omlx among them — rejects a request without it.
    if (!modelId.isEmpty())
        root.insert(QStringLiteral("model"), modelId);
    root.insert(QStringLiteral("state"), state);
    root.insert(QStringLiteral("images"), QJsonArray{QString::fromUtf8(imageDataUrl)});
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

// A choice answer's class map is spelled differently across builds: the
// measured llama-server shape is a string label next to a probabilities map
// ({"choice": "exact_match", "probabilities": {...}}), the v6 draft put the
// map right under "choice", and a label with no map counts as a certain
// verdict for that class.
bool readChoice(const QJsonValue &answer, QHash<QString, double> &diagnosis)
{
    if (!answer.isObject())
        return false;
    const QJsonObject object = answer.toObject();
    QJsonValue map = object.value(QStringLiteral("probabilities"));
    if (!map.isObject())
        map = object.value(QStringLiteral("probs"));
    if (map.isObject()) {
        const QJsonObject probabilities = map.toObject();
        for (auto it = probabilities.constBegin(); it != probabilities.constEnd(); ++it) {
            if (!it.value().isDouble())
                return false;
            diagnosis.insert(it.key(), std::clamp(it.value().toDouble(), 0.0, 1.0));
        }
        return !diagnosis.isEmpty();
    }
    if (object.value(QStringLiteral("choice")).isString()) {
        diagnosis.insert(object.value(QStringLiteral("choice")).toString(), 1.0);
        return true;
    }
    if (object.value(QStringLiteral("choice")).isObject()) {
        const QJsonObject probabilities = object.value(QStringLiteral("choice")).toObject();
        for (auto it = probabilities.constBegin(); it != probabilities.constEnd(); ++it) {
            if (!it.value().isDouble())
                return false;
            diagnosis.insert(it.key(), std::clamp(it.value().toDouble(), 0.0, 1.0));
        }
        return !diagnosis.isEmpty();
    }
    // A bare map of numbers — only when nothing else is in the answer.
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isDouble())
            return false;
        diagnosis.insert(it.key(), std::clamp(it.value().toDouble(), 0.0, 1.0));
    }
    return !diagnosis.isEmpty();
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

    // The v6 aux questions are optional: a v5 profile or an old server ships
    // match only. An answer that is present but unreadable is an error, never
    // a silent verdict.
    DecisionResult result;
    if (answers.contains(QStringLiteral("has_text")) && !readNoul(answers.value(QStringLiteral("has_text")), result.hasText))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));
    if (answers.contains(QStringLiteral("readable")) && !readNoul(answers.value(QStringLiteral("readable")), result.readable))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));
    if (answers.contains(QStringLiteral("diagnosis")) && !readChoice(answers.value(QStringLiteral("diagnosis")), result.diagnosis))
        return DecisionResult::makeError(StatusMessage::translate("DecisionModel", "Unsupported decision answer format"));

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
    // The render of a visual-diff request is encoded on the worker thread next
    // to the crop — both pictures travel on the wire.
    return runChatExchange<DecisionResult>(
        [request, modelId]() {
            if (request.visualDiff)
                return buildRequestBody(request, encodeImageDataUrl(request.image), encodeImageDataUrl(request.renderImage), modelId);
            return buildRequestBody(request, encodeImageDataUrl(request.image), modelId);
        },
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
