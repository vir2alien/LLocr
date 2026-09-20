#include "models/GeneralPurposeModel.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include "core/ServiceMarkers.h"

#include <algorithm>
#include <memory>

namespace llocr {

namespace {

// llama.cpp does not treat the Qwen end-of-sentence token as a stop, so the
// models emit it as literal text (see ADR 18 - the OCR parser strips the
// same markers, shared via core/ServiceMarkers.h). Also drop
// <think>...</think> blocks: thinking models can end their turn inside the
// reasoning block, leaving only control markers in content.
QString stripControlTokens(const QString &text)
{
    static const QRegularExpression thinkBlock(
        QStringLiteral(R"(<think>[\s\S]*?</think>\s*)"));

    QString out = stripServiceTokens(text);
    out.remove(thinkBlock);

    // An unclosed <think> means the answer never started; keep what
    // precedes it.
    const int thinkStart = out.indexOf(QStringLiteral("<think>"));
    if (thinkStart >= 0)
        out.truncate(thinkStart);

    return out.trimmed();
}

}  // namespace

QString GeneralPurposeModel::encodeImageDataUrl(const QImage &image, const QString &format, int quality)
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

QByteArray GeneralPurposeModel::buildRequestBody(const CheckRequest &request,
                                                 const QString &imageDataUrl)
{
    QJsonObject imageUrl{{QStringLiteral("url"), imageDataUrl}};
    QJsonObject imagePart{{QStringLiteral("type"), QStringLiteral("image_url")},
                          {QStringLiteral("image_url"), imageUrl}};
    // Single text part with the instruction followed by the labelled text to
    // verify — the same image+text shape the OCR request uses (several separate
    // text parts confuse some chat templates).
    const QString textPartText = request.prompt
        + QStringLiteral("\n\nRecognized text to verify:\n")
        + request.recognizedText;
    QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")},
                         {QStringLiteral("text"), textPartText}};

    QJsonArray content{imagePart, textPart};

    QJsonObject message{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), content}};

    QJsonObject root{
        {QStringLiteral("model"), request.modelId},
        {QStringLiteral("messages"), QJsonArray{message}}
    };

    // Qwen3-family thinking models may spend the whole turn inside <think> and
    // return no answer; ask the template to disable thinking (ignored by
    // templates/servers that do not know the kwarg).
    root.insert(QStringLiteral("chat_template_kwargs"),
                QJsonObject{{QStringLiteral("enable_thinking"), false}});

    QList<RequestParameter> parameters = request.parameters;
    std::stable_sort(parameters.begin(), parameters.end(),
                     [](const RequestParameter &a, const RequestParameter &b) {
                         return a.order < b.order;
                     });
    for (const RequestParameter &parameter : parameters)
        root.insert(parameter.name, RequestProfile::valueToJson(parameter.value));

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

CheckResult GeneralPurposeModel::parseResponse(const QByteArray &responseData)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return CheckResult::makeError(
            QCoreApplication::translate("GeneralPurposeModel", "Invalid JSON response"));

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return CheckResult::makeError(
            QCoreApplication::translate("GeneralPurposeModel", "No choices in response"));

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = stripControlTokens(message.value(QStringLiteral("content")).toString());

    if (content.isEmpty())
        return CheckResult::makeError(QCoreApplication::translate(
            "GeneralPurposeModel",
            "The model returned no corrected text, only end-of-sentence markers. "
            "Check that the selected model can process images."));

    CheckResult result;
    result.success = true;
    result.text = content;
    return result;
}

QFuture<CheckResult> GeneralPurposeModel::check(const CheckRequest &request,
                                                const ConnectionConfig &config)
{
    auto promise = std::make_shared<QPromise<CheckResult>>();
    promise->start();
    QFuture<CheckResult> future = promise->future();
    auto client = std::make_shared<LlamaClient>();
    m_activeClient = client;

    auto *encodeWatcher = new QFutureWatcher<QByteArray>();
    QObject::connect(encodeWatcher, &QFutureWatcher<QByteArray>::finished, encodeWatcher,
                     [promise, encodeWatcher, client, config]() {
                         encodeWatcher->deleteLater();
                         const QByteArray body = encodeWatcher->future().resultCount() > 0
                                                     ? encodeWatcher->result()
                                                     : QByteArray();
                         if (body.isEmpty()) {
                             promise->addResult(CheckResult::makeError(
                                 QCoreApplication::translate("GeneralPurposeModel",
                                     "Failed to encode the block image")));
                             promise->finish();
                             return;
                         }
                         auto *watcher = new QFutureWatcher<HttpResponse>();
                         QObject::connect(watcher, &QFutureWatcher<HttpResponse>::finished, watcher,
                                          [client, promise, watcher]() {
                                              const HttpResponse response =
                                                  watcher->future().resultCount() > 0 ? watcher->result() : HttpResponse{};
                                              if (response.success)
                                                  promise->addResult(parseResponse(response.body));
                                              else
                                                  promise->addResult(CheckResult::makeError(response.error));
                                              promise->finish();
                                              watcher->deleteLater();
                                          });
                         watcher->setFuture(client->postJson(LlamaClient::endpointUrl(config.baseUrl), body,
                                                             config.apiKey, config.timeoutMs));
                     });
    encodeWatcher->setFuture(QtConcurrent::run([request]() {
        const QString dataUrl = encodeImageDataUrl(request.image, QStringLiteral("png"));
        if (dataUrl.isEmpty())
            return QByteArray();
        return buildRequestBody(request, dataUrl);
    }));

    return future;
}

void GeneralPurposeModel::abort()
{
    if (m_activeClient)
        m_activeClient->abort();
}

} // namespace llocr
