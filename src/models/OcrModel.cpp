#include "models/OcrModel.h"

#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"
#include "models/VisionChatModel.h"

#include <atomic>
#include <memory>

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace llocr {

namespace {

// Raw-response debug dump: every model reply is stored verbatim, so a broken
// parse can be diffed against what the model actually emitted. Opt-in only —
// set LLOCR_RAW_DEBUG=1 (any value except 0) before starting the app — because
// the request dump contains the full base64 page image and the files are never
// rotated. Files land in <AppDataDir>/raw-debug/ — one file per request.
bool rawDebugEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("LLOCR_RAW_DEBUG") && qEnvironmentVariable("LLOCR_RAW_DEBUG") != QLatin1String("0");
    return enabled;
}

void dumpRawResponse(const QByteArray &responseData, const QString &content, bool parsed)
{
    if (!rawDebugEnabled())
        return;

    static std::atomic<int> sequence;
    const int n = sequence.fetch_add(1, std::memory_order_relaxed) + 1;

    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/raw-debug"));

    const QString base = QStringLiteral("raw-%1-%2").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))).arg(n, 4, 10, QLatin1Char('0'));
    // Parsed replies store the verbatim model text; anything the JSON layer
    // could not read stores the full envelope for diagnosis.
    QFile file(dir.filePath(base + (parsed ? QStringLiteral(".txt") : QStringLiteral(".invalid.json"))));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[raw-debug] cannot write" << file.fileName();
        return;
    }
    file.write(parsed ? content.toUtf8() : responseData);
    qInfo().noquote() << "[raw-debug] model response saved:" << file.fileName();
}

}  // namespace

OcrModel::OcrModel(QString modelId, QString role) : m_id(std::move(modelId)), m_role(std::move(role)) {}

std::unique_ptr<OcrModel> OcrModel::create(const QString &modelId, const QString &role)
{
    if (ModelProfiles::findBuiltIn(modelId))
        return std::make_unique<OcrModel>(modelId, role);

    const QString fallback = defaultId();
    qWarning() << "OcrModel: unknown model id" << modelId << "— falling back to" << fallback;
    return std::make_unique<OcrModel>(fallback, role);
}

QString OcrModel::defaultId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
}

QStringList OcrModel::registeredIds()
{
    return ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
}

QString OcrModel::displayName() const
{
    if (const ModelProfiles::Profile *profile = ModelProfiles::findBuiltIn(m_id))
        return profile->title;
    return m_id;
}

namespace {
// The adapter serves the page-recognition task the model declares: page OCR
// with layout tokens, or — for models like GLM-OCR — the raw-text variant.
const ModelProfiles::Role *pageRoleFor(const QString &modelId)
{
    if (const ModelProfiles::Role *role = ModelProfiles::roleFor(modelId, QStringLiteral("pageOcr")))
        return role;
    return ModelProfiles::roleFor(modelId, QStringLiteral("rawPageOcr"));
}

// The role the adapter serves: the task's own role when the model has one,
// the page role otherwise. A model that serves the page task and the raw task
// answers each of them with its own prompt — Chandra-OCR-2 has one prompt per
// task — so the task reaches the profile.
const ModelProfiles::Role *roleForTask(const QString &modelId, const QString &taskRole)
{
    if (!taskRole.isEmpty()) {
        if (const ModelProfiles::Role *role = ModelProfiles::roleFor(modelId, taskRole))
            return role;
    }
    return pageRoleFor(modelId);
}
}  // namespace

QList<ModelProfiles::Prompt> OcrModel::promptVariants() const
{
    if (const ModelProfiles::Role *role = roleForTask(m_id, m_role)) {
        if (!role->prompts.isEmpty())
            return role->prompts;
    }
    if (const ModelProfiles::Role *role = pageRoleFor(m_id))
        return role->prompts;
    return {};
}

QString OcrModel::defaultParserId() const
{
    return defaultParserIdFor(m_id, m_role);
}

QString OcrModel::defaultParserIdFor(const QString &modelId, const QString &role)
{
    // An unknown id falls back to the default model, exactly as create() does.
    const QString resolved = ModelProfiles::findBuiltIn(modelId) ? modelId : defaultId();
    if (const ModelProfiles::Role *found = roleForTask(resolved, role))
        return found->parser;
    return QStringLiteral("raw");
}

QByteArray OcrModel::buildRequestBody(const OcrRequest &request, const QByteArray &imageDataUrl, const QString &taskRole)
{
    QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), request.prompt}};
    QJsonObject imageUrl{{QStringLiteral("url"), QString::fromUtf8(imageDataUrl)}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), imageUrl}};

    const ModelProfiles::Role *role = roleForTask(request.modelId, taskRole);
    // An empty prompt is the transcription request itself (LightOnOCR-3 trains
    // "the image only") — a text part with an empty string would be a
    // different input, so the part is dropped rather than sent empty.
    const bool sendPrompt = !request.prompt.isEmpty();
    const bool promptFirst = role && role->promptBeforeImage;
    QJsonArray content;
    if (sendPrompt && promptFirst)
        content.append(textPart);
    content.append(imagePart);
    if (sendPrompt && !promptFirst)
        content.append(textPart);

    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};

    QJsonObject root{{QStringLiteral("model"), request.modelId}, {QStringLiteral("messages"), QJsonArray{message}}};

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(), [](const RequestParameter &a, const RequestParameter &b) { return a.order < b.order; });
    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

OcrResult OcrModel::parseResponse(const QByteArray &responseData)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        dumpRawResponse(responseData, QString(), false);
        return OcrResult::makeError(QCoreApplication::translate("OcrModel", "Invalid JSON response"));
    }

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        dumpRawResponse(responseData, QString(), false);
        return OcrResult::makeError(QCoreApplication::translate("OcrModel", "No choices in response"));
    }

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = VisionChatModel::stripEscapedPunctuation(message.value(QStringLiteral("content")).toString());
    dumpRawResponse(responseData, content, true);

    OcrResult result;
    result.success = true;
    result.text = content;
    return result;
}

namespace {
// The profile's trained page size: a page larger than the edge is shrunk to
// fit before encoding (KeepAspectRatio), smaller pages pass untouched.
QImage scaledToLongestEdge(const QImage &image, int longestEdge)
{
    if (image.isNull() || longestEdge <= 0 || (image.width() <= longestEdge && image.height() <= longestEdge))
        return image;
    const QSize fit = image.size().scaled(QSize(longestEdge, longestEdge), Qt::KeepAspectRatio);
    return image.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
}  // namespace

QFuture<OcrResult> OcrModel::recognize(const OcrRequest &request, const ConnectionConfig &config)
{
    const QString taskRole = m_role;
    return runChatExchange<OcrResult>(
        [request, taskRole]() {
            QImage image = request.image;
            if (const ModelProfiles::Role *role = roleForTask(request.modelId, taskRole))
                image = scaledToLongestEdge(image, role->imageLongestEdge);
            return buildRequestBody(request, encodeImageDataUrl(image), taskRole);
        },
        config,
        m_activeClient,
        [](const QByteArray &body) { return parseResponse(body); },
        QCoreApplication::translate("OcrModel", "Failed to encode the page image"));
}

void OcrModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
