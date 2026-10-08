#include "models/OcrModel.h"

#include "models/ChatExchange.h"
#include "models/ImageDataUrl.h"

#include <memory>

#include <QAtomicInteger>
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

QDir rawDebugDir()
{
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/raw-debug"));
    dir.mkpath(QStringLiteral("."));
    return dir;
}

void dumpRawRequest(const QByteArray &requestBody)
{
    if (!rawDebugEnabled())
        return;

    static QAtomicInt sequence;
    const int n = sequence.fetchAndAddRelaxed(1) + 1;

    const QString base = QStringLiteral("raw-%1-%2-request").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))).arg(n, 4, 10, QLatin1Char('0'));
    QFile file(rawDebugDir().filePath(base + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[raw-debug] cannot write" << file.fileName();
        return;
    }
    file.write(requestBody);
    qInfo().noquote() << "[raw-debug] request saved:" << file.fileName();
}

void dumpRawResponse(const QByteArray &responseData, const QString &content, bool parsed)
{
    if (!rawDebugEnabled())
        return;

    static QAtomicInt sequence;
    const int n = sequence.fetchAndAddRelaxed(1) + 1;

    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/raw-debug"));

    const QString base = QStringLiteral("raw-%1-%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))).arg(n, 4, 10, QLatin1Char('0'));
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

OcrModel::OcrModel(QString modelId) : m_id(std::move(modelId)) {}

std::unique_ptr<OcrModel> OcrModel::create(const QString &modelId)
{
    if (ModelProfiles::find(ModelProfiles::instance(), modelId))
        return std::make_unique<OcrModel>(modelId);

    const QString fallback = defaultId();
    qWarning() << "OcrModel: unknown model id" << modelId << "— falling back to" << fallback;
    return std::make_unique<OcrModel>(fallback);
}

QString OcrModel::defaultId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("ocr"));
}

QStringList OcrModel::registeredIds()
{
    return ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("ocr"));
}

QString OcrModel::displayName() const
{
    if (const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), m_id))
        return profile->title;
    return m_id;
}

QList<ModelProfiles::Prompt> OcrModel::promptVariants() const
{
    return ModelProfiles::promptsFor(ModelProfiles::instance(), m_id, QStringLiteral("ocr"));
}

QString OcrModel::defaultParserId() const
{
    if (const ModelProfiles::Role *role = ModelProfiles::roleFor(m_id, QStringLiteral("ocr")))
        return role->parser;
    return QStringLiteral("raw");
}

QByteArray OcrModel::buildRequestBody(const OcrRequest &request, const QByteArray &imageDataUrl)
{
    QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), request.prompt}};
    QJsonObject imageUrl{{QStringLiteral("url"), QString::fromUtf8(imageDataUrl)}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), imageUrl}};

    const ModelProfiles::Role *role = ModelProfiles::roleFor(request.modelId, QStringLiteral("ocr"));
    QJsonArray content = role && role->promptBeforeImage ? QJsonArray{textPart, imagePart} : QJsonArray{imagePart, textPart};

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
    const QString content = message.value(QStringLiteral("content")).toString();
    dumpRawResponse(responseData, content, true);

    OcrResult result;
    result.success = true;
    result.text = content;
    return result;
}

QFuture<OcrResult> OcrModel::recognize(const OcrRequest &request, const ConnectionConfig &config)
{
    auto client = std::make_shared<LlamaClient>();
    m_activeClient = client;

    return runChatExchange<OcrResult>([request]() { return buildRequestBody(request, encodeImageDataUrl(request.image)); },
                                      config,
                                      client,
                                      [](const QByteArray &body) { return parseResponse(body); },
                                      QCoreApplication::translate("OcrModel", "Failed to encode the page image"));
}

void OcrModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

}  // namespace llocr
