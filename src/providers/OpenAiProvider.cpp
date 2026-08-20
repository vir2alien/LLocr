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

    // QJsonArray eosBias; //early eos catch method for tests
    // eosBias.append(1);
    // eosBias.append(-3.0);
    // QJsonArray logitBias;
    // logitBias.append(eosBias);
    QJsonObject root{
        {QStringLiteral("model"), request.modelId},
        {QStringLiteral("messages"), QJsonArray{message}},

        {QStringLiteral("dry_multiplier"), request.dryMultiplier},
        {QStringLiteral("dry_base"), request.dryBase},
        {QStringLiteral("dry_allowed_length"), request.dryAllowedLength},
        {QStringLiteral("dry_penalty_last_n"), request.dryPenaltyLastN},
        {QStringLiteral("dry_sequence_breakers"), QJsonArray{QStringLiteral("\uE000")}},

        {QStringLiteral("temperature"), request.temperature},

        {QStringLiteral("max_tokens"), request.maxTokens},
        {QStringLiteral("stream"), false}
        //{QStringLiteral("logit_bias"), logitBias}
    };

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
                         if (reply->error() != QNetworkReply::NoError)
                             promise->addResult(OcrResult::makeError(reply->errorString()));
                         else
                             promise->addResult(parseResponse(reply->readAll()));
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
