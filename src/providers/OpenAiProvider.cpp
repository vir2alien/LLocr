#include "providers/OpenAiProvider.h"

#include <memory>

#include <QBuffer>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QTimer>

#include <algorithm>

namespace llocr {

OpenAiProvider::OpenAiProvider(QObject *parent) : QObject(parent) {}

QString OpenAiProvider::name() const
{
    return QStringLiteral("OpenAI-compatible");
}

QUrl OpenAiProvider::endpointUrl(const QString &baseUrl) const
{
    QString base = baseUrl;
    while (base.endsWith('/'))
        base.chop(1);
    return QUrl(base + QStringLiteral("/v1/chat/completions"));
}

QString OpenAiProvider::encodeImageDataUrl(const QImage& image, const QString& format)
{
    const QString fmt = format.isEmpty() ? QStringLiteral("png") : format.toLower();

    QByteArray raw;
    QBuffer buffer(&raw);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, fmt.toUpper().toLatin1().constData());
    buffer.close();

    return QStringLiteral("data:image/%1;base64,%2")
        .arg(fmt, QString::fromLatin1(raw.toBase64()));
}

QByteArray OpenAiProvider::buildRequestBody(const OcrRequest& request,
                                            const QString& imageDataUrl)
{
    QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), request.prompt}};
    QJsonObject imageUrl{{QStringLiteral("url"), imageDataUrl}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), imageUrl}};

    QJsonArray content{imagePart, textPart};

    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}};

    QJsonObject root{
        {QStringLiteral("model"), request.modelId},
        {QStringLiteral("messages"), QJsonArray{message}}
    };

    // The request profile drives the body parameters. The parameters carry
    // their position (order) from the profile; QJsonObject itself re-sorts
    // keys alphabetically during serialization, which llama.cpp treats as
    // irrelevant — the order governs the profile file and the settings table.
    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(),
                     [](const RequestParameter &a, const RequestParameter &b) {
                         return a.order < b.order;
                     });
    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

OcrResult OpenAiProvider::parseResponse(const QByteArray& responseData)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return OcrResult::makeError(QCoreApplication::translate("OpenAiProvider", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return OcrResult::makeError(QCoreApplication::translate("OpenAiProvider", "No choices in response"));

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();

    OcrResult result;
    result.success = true;
    result.text = content;
    return result;
}

// llama.cpp / OpenAI-compatible servers report the exact reason in the HTTP
// error body: {"error": {"message": "..."}}. Surfacing it turns a bare
// "Bad Request" into an actionable message (e.g. which sampling parameter the
// server rejected).
QString OpenAiProvider::extractServerError(const QByteArray& responseData)
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject())
        return QString();
    const QJsonValue error = doc.object().value(QStringLiteral("error"));
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    if (error.isString())
        return error.toString();
    return QString();
}

QFuture<OcrResult> OpenAiProvider::recognize(const OcrRequest &request, const ProviderConfig &config)
{
    auto promise = std::make_shared<QPromise<OcrResult>>();
    promise->start();
    QFuture<OcrResult> future = promise->future();

    const QString dataUrl = encodeImageDataUrl(request.image, QStringLiteral("png"));
    const QByteArray body = buildRequestBody(request, dataUrl);

    QNetworkRequest req(endpointUrl(config.baseUrl));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!config.apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + config.apiKey.toUtf8());

    QNetworkReply* reply = m_network.post(req, body);
    m_currentReply = reply;

    // Guard against a hung endpoint.
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    timer->start(config.timeoutMs);

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, promise]() mutable {
                         if (reply->error() != QNetworkReply::NoError) {
                             // Prefer the server's own error message (llama.cpp
                             // sends {"error":{"message": ...}} with the exact
                             // reason, e.g. which sampling parameter was rejected);
                             // fall back to the Qt-level description.
                             QString error = reply->errorString();
                             const QString serverError =
                                 extractServerError(reply->readAll());
                             if (!serverError.isEmpty())
                                 error += QStringLiteral("\nServer: ") + serverError;
                             promise->addResult(OcrResult::makeError(error));
                         } else {
                             promise->addResult(parseResponse(reply->readAll()));
                         }
                         promise->finish();
                         reply->deleteLater();
                     });

    return future;
}

void OpenAiProvider::abort()
{
    if (m_currentReply)
        m_currentReply->abort();
}

} // namespace llocr
