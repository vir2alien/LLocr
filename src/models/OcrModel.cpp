#include "models/OcrModel.h"

#include <memory>

#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace llocr {

QString OcrModel::encodeImageDataUrl(const QImage &image, const QString &format, int quality)
{
    if (image.isNull())
        return QString();

    const QString fmt = format.isEmpty() ? QStringLiteral("png") : format.toLower();

    QByteArray raw;
    QBuffer buffer(&raw);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, fmt.toUpper().toLatin1().constData(), quality))
        return QString();
    buffer.close();

    return QStringLiteral("data:image/%1;base64,%2")
        .arg(fmt, QString::fromLatin1(raw.toBase64()));
}

QByteArray OcrModel::buildRequestBody(const OcrRequest &request,
                                      const QString &imageDataUrl) const
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

OcrResult OcrModel::parseResponse(const QByteArray &responseData) const
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return OcrResult::makeError(QCoreApplication::translate("OcrModel", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return OcrResult::makeError(QCoreApplication::translate("OcrModel", "No choices in response"));

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();

    OcrResult result;
    result.success = true;
    result.text = content;
    return result;
}

QFuture<OcrResult> OcrModel::recognize(const OcrRequest &request, const ConnectionConfig &config)
{
    auto promise = std::make_shared<QPromise<OcrResult>>();
    promise->start();
    QFuture<OcrResult> future = promise->future();

    auto *encodeWatcher = new QFutureWatcher<QByteArray>();
    encodeWatcher->setFuture(QtConcurrent::run([this, request, config]() {
        const QString dataUrl = encodeImageDataUrl(request.image, QStringLiteral("png"));
        if (dataUrl.isEmpty())
            return QByteArray();
        return buildRequestBody(request, dataUrl);
    }));
    QObject::connect(encodeWatcher, &QFutureWatcher<QByteArray>::finished, encodeWatcher,
                     [this, promise, encodeWatcher, config]() {
                         encodeWatcher->deleteLater();
                         const QByteArray body = encodeWatcher->future().resultCount() > 0
                                                     ? encodeWatcher->result()
                                                     : QByteArray();
                         if (body.isEmpty()) {
                             const bool hasResult =
                                 encodeWatcher->future().resultCount() > 0;
                             promise->addResult(OcrResult::makeError(
                                 QCoreApplication::translate("OcrModel",
                                     "Failed to encode the page image")
                                     + (hasResult
                                            ? QStringLiteral(" (image is null)")
                                            : QStringLiteral(" (out of memory?)"))));
                             promise->finish();
                             return;
                         }
                         auto *watcher = new QFutureWatcher<HttpResponse>();
                         watcher->setFuture(m_client.postJson(LlamaClient::endpointUrl(config.baseUrl), body,
                                                              config.apiKey, config.timeoutMs));
                         QObject::connect(watcher, &QFutureWatcher<HttpResponse>::finished, watcher,
                                          [this, promise, watcher]() mutable {
                                              const HttpResponse response =
                                                  watcher->future().resultCount() > 0 ? watcher->result() : HttpResponse{};
                                              if (response.success)
                                                  promise->addResult(parseResponse(response.body));
                                              else
                                                  promise->addResult(OcrResult::makeError(response.error));
                                              promise->finish();
                                              watcher->deleteLater();
                                          });
                     });

    return future;
}

void OcrModel::abort()
{
    m_client.abort();
}

} // namespace llocr
