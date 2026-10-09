#include <QtTest>

#include <QDir>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "core/ModelProfiles.h"
#include "core/OcrRequest.h"
#include "models/GeneralPurposeModel.h"
#include "models/OcrModel.h"
#include "parsers/BlockStyle.h"
#include "parsers/ParserFactory.h"

using namespace llocr;

namespace {

// Exposes the protected request/response formation for white-box tests.
class ExposedGeneralPurposeModel : public GeneralPurposeModel
{
public:
    QByteArray build(const CheckRequest &request, const QByteArray &imageDataUrl) { return buildRequestBody(request, imageDataUrl); }

    CheckResult parse(const QByteArray &responseData) { return parseResponse(responseData); }
};

class ExposedOcrModel : public OcrModel
{
public:
    explicit ExposedOcrModel(const QString &modelId) : OcrModel(modelId) {}

    QByteArray build(const OcrRequest &request, const QByteArray &imageDataUrl) { return buildRequestBody(request, imageDataUrl); }
};

class RecordingServer : public QObject
{
public:
    QByteArray body;
    bool gotRequest = false;
    bool holdResponse = false;

    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_buffers.insert(socket, QByteArray());
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    m_buffers[socket].append(socket->readAll());
                    maybeRespond(socket);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            }
        });
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server.serverPort(); }

private:
    void maybeRespond(QTcpSocket *socket)
    {
        const QByteArray &raw = m_buffers.value(socket);
        const int headerEnd = raw.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        int contentLength = 0;
        const QList<QByteArray> lines = raw.left(headerEnd).split('\n');
        for (const QByteArray &line : lines) {
            if (line.toLower().startsWith("content-length:"))
                contentLength = line.mid(15).trimmed().toInt();
        }
        if (raw.size() < headerEnd + 4 + contentLength)
            return;
        body = raw.mid(headerEnd + 4, contentLength);
        gotRequest = true;
        if (holdResponse)
            return;

        const QByteArray replyBody = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"}}]}";
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " +
                      QByteArray::number(replyBody.size()) +
                      "\r\n"
                      "Connection: close\r\n\r\n" +
                      replyBody);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

}  // namespace

class TestOcrModels : public QObject
{
    Q_OBJECT

private slots:
    void factoryDefaultAndRegistry()
    {
        QCOMPARE(OcrModel::defaultId(), QStringLiteral("unlimited-ocr"));
        QVERIFY(OcrModel::registeredIds().contains(QStringLiteral("unlimited-ocr")));
        QVERIFY(OcrModel::registeredIds().contains(QStringLiteral("lfm25-vl-3b")));

        const auto model = OcrModel::create(OcrModel::defaultId());
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), QStringLiteral("unlimited-ocr"));

        const auto lfm = OcrModel::create(QStringLiteral("lfm25-vl-3b"));
        QVERIFY(lfm != nullptr);
        QCOMPARE(lfm->id(), QStringLiteral("lfm25-vl-3b"));
    }

    // The raw-response dump (which includes the full base64 page image) is
    // opt-in via LLOCR_RAW_DEBUG — a normal recognition run must not write it.
    void rawDebugDumpIsOptIn()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString dumpDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("raw-debug"));
        QDir(dumpDir).removeRecursively();
        QVERIFY(!QDir(dumpDir).exists());

        RecordingServer server;
        QVERIFY(server.start());
        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        OcrRequest request;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        request.image = image;
        request.prompt = QStringLiteral("document parsing.");

        auto model = OcrModel::create(OcrModel::defaultId());
        QVERIFY(model != nullptr);
        QFuture<OcrResult> future = model->recognize(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QCOMPARE(future.result().text, QStringLiteral("ok"));

        QVERIFY2(!QDir(dumpDir).exists(), "the raw-debug dump must not be written unless LLOCR_RAW_DEBUG is set");
    }

    void unknownIdFallsBackToDefault()
    {
        const auto model = OcrModel::create(QStringLiteral("no-such-model"));
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), OcrModel::defaultId());
    }

    // ADR 88: Settings → Output defaults to "auto", i.e. the model adapter
    // owns the parser choice. Every registered model must therefore declare a
    // parser id that ParserFactory can actually build — a new adapter shipping
    // with a typo would otherwise only fail at recognition time.
    void everyModelDeclaresARegisteredParser()
    {
        const QStringList parsers = ParserFactory::registeredIds();
        for (const QString &modelId : OcrModel::registeredIds()) {
            const QString parserId = OcrModel::create(modelId)->defaultParserId();
            QVERIFY2(!parserId.isEmpty(), qPrintable(modelId));
            QVERIFY2(parsers.contains(parserId), qPrintable(QStringLiteral("model %1 declares unregistered parser %2").arg(modelId, parserId)));
        }
    }

    // The model contract now lives in resources/profiles/models/<id>.json: a
    // model is added as data, so the tests read the shipped file rather than a
    // C++ subclass.
    void unlimitedModelContract()
    {
        const OcrModel model(QStringLiteral("unlimited-ocr"));

        QCOMPARE(model.id(), QStringLiteral("unlimited-ocr"));
        QCOMPARE(model.displayName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(model.defaultParserId(), QStringLiteral("unlimited-ocr"));

        const QList<ModelProfiles::Prompt> variants = model.promptVariants();
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first().text, QStringLiteral("document parsing."));
        QVERIFY(!variants.first().id.isEmpty());
        QVERIFY(!variants.first().title.isEmpty());
    }

    void lfm25ModelContract()
    {
        const OcrModel model(QStringLiteral("lfm25-vl-3b"));

        QCOMPARE(model.id(), QStringLiteral("lfm25-vl-3b"));
        QCOMPARE(model.displayName(), QStringLiteral("LFM2.5-VL-3B"));
        QCOMPARE(model.defaultParserId(), QStringLiteral("lfm2.5-vl"));

        const QList<ModelProfiles::Prompt> variants = model.promptVariants();
        QCOMPARE(variants.size(), 1);
        // The prompt must describe the layout-annotation contract the parser
        // consumes: the image_index header and the [0, 1000] coordinate range.
        QVERIFY(variants.first().text.contains(QStringLiteral("image_index=<n>")));
        QVERIFY(variants.first().text.contains(QStringLiteral("[0, 1000]")));
        QVERIFY(!variants.first().id.isEmpty());
        QVERIFY(!variants.first().title.isEmpty());
        // The wire shape follows the model card: text, then the image.
        const ModelProfiles::Role *ocrRole = ModelProfiles::roleFor(QStringLiteral("lfm25-vl-3b"), QStringLiteral("ocr"));
        QVERIFY(ocrRole);
        QCOMPARE(ocrRole->promptBeforeImage, true);
    }

    // The message layout is the model's trained shape: the LFM2.5 card documents
    // text-then-image, the Qwen-family models document image-then-text. The
    // order travels in the profile, so one shipped entry can deviate without a
    // wire-shape fork in C++.
    void ocrBodyFollowsTheTrainedPartOrder()
    {
        OcrRequest request;
        request.prompt = QStringLiteral("Parse this document.");
        request.image = QImage(4, 4, QImage::Format_RGB32);

        request.modelId = QStringLiteral("lfm25-vl-3b");
        const QByteArray lfmBody = ExposedOcrModel(QStringLiteral("lfm25-vl-3b")).build(request, QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonDocument lfmDoc = QJsonDocument::fromJson(lfmBody);
        const QJsonArray lfmContent = lfmDoc.object().value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(lfmContent.size(), 2);
        QCOMPARE(lfmContent.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        QCOMPARE(lfmContent.last().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
        // One user message, no system message: the card allows the instruction
        // as a system or a user prompt, and the user prompt is what we use.
        QCOMPARE(lfmDoc.object().value(QStringLiteral("messages")).toArray().size(), 1);
        QCOMPARE(lfmDoc.object().value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));

        request.modelId = QStringLiteral("unlimited-ocr");
        const QByteArray unlimitedBody = ExposedOcrModel(QStringLiteral("unlimited-ocr")).build(request, QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonArray unlimitedContent = QJsonDocument::fromJson(unlimitedBody).object().value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(unlimitedContent.first().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
        QCOMPARE(unlimitedContent.last().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    }

    // Every label the LFM prompt advertises must be resolvable, otherwise the
    // parser styles a region as plain text the model meant as an image.
    void lfmVocabularyIsStyled()
    {
        const OcrModel lfm(QStringLiteral("lfm25-vl-3b"));
        const QString prompt = lfm.promptVariants().constFirst().text;
        const int start = prompt.indexOf(QStringLiteral("<label> is one of these layout labels:"));
        QVERIFY(start > 0);
        const QString list = prompt.mid(start).section(QLatin1Char('\n'), 0, 0);
        const QStringList labels = list.split(QStringLiteral(": "), Qt::SkipEmptyParts).last().split(QStringLiteral(", "));
        QVERIFY(labels.size() > 20);
        for (const QString &label : labels) {
            QVERIFY2(BlockStyleMap::instance().knowsLabel(label, QStringLiteral("lfm25-vl-3b")), qPrintable(QStringLiteral("label %1 has no style").arg(label)));
        }
        // The labels LFM adds over the base vocabulary come from its profile.
        QCOMPARE(blockStyleForLabel(QStringLiteral("image_block"), QStringLiteral("lfm25-vl-3b")).style, BlockStyle::ImagePlaceholder);
        QCOMPARE(blockStyleForLabel(QStringLiteral("code"), QStringLiteral("lfm25-vl-3b")).style, BlockStyle::PlainText);
        // …and Unlimited-OCR must not inherit them.
        QCOMPARE(blockStyleForLabel(QStringLiteral("image_block"), QStringLiteral("unlimited-ocr")).style, BlockStyle::PlainText);
    }

    void recognizeSendsDecodableJpegDataUrl()
    {
        RecordingServer server;
        QVERIFY(server.start());

        QImage image(137, 91, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x)
                image.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
        }

        OcrRequest request;
        request.image = image;
        request.prompt = QStringLiteral("document parsing.");
        request.modelId = QStringLiteral("unlimited-ocr");

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        const auto model = OcrModel::create(OcrModel::defaultId());
        QVERIFY(model != nullptr);

        QFuture<OcrResult> future = model->recognize(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(server.gotRequest);

        const QJsonDocument doc = QJsonDocument::fromJson(server.body);
        QVERIFY(doc.isObject());
        const QJsonArray content = doc.object().value(QStringLiteral("messages")).toArray().at(0).toObject().value(QStringLiteral("content")).toArray();
        QString dataUrl;
        QString textPart;
        for (const QJsonValue &part : content) {
            const QJsonObject obj = part.toObject();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("image_url"))
                dataUrl = obj.value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
                textPart = obj.value(QStringLiteral("text")).toString();
        }
        QCOMPARE(textPart, QStringLiteral("document parsing."));

        QVERIFY(!dataUrl.isEmpty());
        QVERIFY(dataUrl.startsWith(QStringLiteral("data:image/png;base64,")));
        const QByteArray png = QByteArray::fromBase64(dataUrl.mid(QStringLiteral("data:image/png;base64,").size()).toLatin1());
        QVERIFY(png.size() > 100);
        QVERIFY(png.startsWith("\x89PNG"));

        const QImage decoded = QImage::fromData(png, "PNG");
        QVERIFY(!decoded.isNull());
        QCOMPARE(decoded.width(), 137);
        QCOMPARE(decoded.height(), 91);
    }

    void futureCompletesAfterModelDestruction()
    {
        RecordingServer server;
        QVERIFY(server.start());

        QImage image(64, 48, QImage::Format_ARGB32);
        image.fill(Qt::gray);

        OcrRequest request;
        request.image = image;
        request.prompt = QStringLiteral("document parsing.");
        request.modelId = QStringLiteral("unlimited-ocr");

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        auto model = OcrModel::create(OcrModel::defaultId());
        QFuture<OcrResult> future = model->recognize(request, config);

        // I-05: the async chain is self-contained (no `this` captures), so
        // destroying the model mid-flight must neither dangle nor lose the
        // result — the future still completes with the parsed answer.
        model.reset();

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(future.resultCount() > 0);
        QVERIFY2(future.result().success, future.result().errorMessage.text().toUtf8().constData());
        QVERIFY(server.gotRequest);
        QCOMPARE(future.result().text, QStringLiteral("ok"));
    }

    void qtAbortOnDeadlineReportsCancellation()
    {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());
        QNetworkAccessManager network;
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.port())));
        QNetworkReply *reply = network.post(request, QByteArray("{}"));
        QTRY_VERIFY_WITH_TIMEOUT(server.gotRequest, 5000);
        QTimer::singleShot(50, reply, &QNetworkReply::abort);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        reply->deleteLater();
    }

    void requestTimeoutIsNotReportedAsCancellation()
    {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());
        LlamaClient client;
        const auto url = LlamaClient::endpointUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        const auto future = client.postJson(url, "{}", {}, 5000, 200);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(server.gotRequest);
        QVERIFY(!future.result().success);
        QVERIFY2(future.result().error.contains(QStringLiteral("timed out")), qPrintable(future.result().error));
        QVERIFY(future.result().error.contains(QStringLiteral("200")));

        // A timeout must not leak into the next request on the same client.
        server.holdResponse = false;
        const auto next = client.postJson(url, "{}", {}, 5000, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(next.isFinished(), 10000);
        QVERIFY2(next.result().success, qPrintable(next.result().error));
    }

    void manualAbortIsNotReportedAsTimeout()
    {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());
        LlamaClient client;
        const auto future = client.postJson(LlamaClient::endpointUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port())), "{}", {}, 5000, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(server.gotRequest, 5000);
        client.abort();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(!future.result().success);
        QVERIFY(!future.result().error.isEmpty());
        QVERIFY(!future.result().error.contains(QStringLiteral("timed out")));
    }

    void recognizeFailsCleanlyOnNullImage()
    {
        OcrRequest request;
        request.prompt = QStringLiteral("document parsing.");
        request.modelId = QStringLiteral("unlimited-ocr");

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:1");
        config.connectionTimeoutMs = 1000;
        config.responseTimeoutMs = 1000;

        const auto model = OcrModel::create(OcrModel::defaultId());
        QVERIFY(model != nullptr);

        QFuture<OcrResult> future = model->recognize(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        const OcrResult result = future.result();
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }

    void checkRequestBodyCarriesPromptAndImageOnly()
    {
        ExposedGeneralPurposeModel model;
        CheckRequest request;
        request.image = QImage(4, 4, QImage::Format_ARGB32);
        request.image.fill(Qt::gray);
        request.systemPrompt = QStringLiteral("You are an OCR verifier. Answer OK, FIX, or REVIEW.");
        request.typePrompt = QStringLiteral("Verify the text block.");
        request.modelId = QStringLiteral("ocr-verifier");
        request.parameters = {
            {QStringLiteral("temperature"), 0, RequestValueKind::Number, 0.0, QString()},
            {QStringLiteral("repeat_penalty"), 1, RequestValueKind::Number, 1.0, QString()},
            {QStringLiteral("presence_penalty"), 2, RequestValueKind::Number, 0.0, QString()},
            {QStringLiteral("frequency_penalty"), 3, RequestValueKind::Number, 0.0, QString()},
            {QStringLiteral("max_tokens"), 4, RequestValueKind::Number, 512.0, QString()},
            {QStringLiteral("stream"), 5, RequestValueKind::Boolean, false, QString()},
            {QStringLiteral("cache_prompt"), 6, RequestValueKind::Boolean, true, QString()},
        };

        const QByteArray body = model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());

        const QJsonObject root = doc.object();
        QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("ocr-verifier"));
        QCOMPARE(root.value(QStringLiteral("temperature")).toDouble(), 0.0);
        QCOMPARE(root.value(QStringLiteral("repeat_penalty")).toDouble(), 1.0);
        QCOMPARE(root.value(QStringLiteral("presence_penalty")).toDouble(), 0.0);
        QCOMPARE(root.value(QStringLiteral("frequency_penalty")).toDouble(), 0.0);
        QCOMPARE(root.value(QStringLiteral("max_tokens")).toDouble(), 512.0);
        QCOMPARE(root.value(QStringLiteral("stream")).toBool(), false);
        QCOMPARE(root.value(QStringLiteral("cache_prompt")).toBool(), true);

        // Thinking must be disabled so Qwen3-family models answer directly.
        QCOMPARE(root.value(QStringLiteral("chat_template_kwargs")).toObject().value(QStringLiteral("enable_thinking")).toBool(), false);

        // System message carries the shared protocol contract.
        const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        const QJsonObject systemMessage = messages.at(0).toObject();
        QCOMPARE(systemMessage.value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QVERIFY(systemMessage.value(QStringLiteral("content")).toString().startsWith(QStringLiteral("You are an OCR verifier.")));

        // User message: the type prompt and the image only — the block is
        // recognized from scratch, the previously recognized text is not sent.
        const QJsonArray content = messages.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(content.size(), 2);
        QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));

        const QString bodyText = QString::fromUtf8(body);
        QVERIFY(!bodyText.contains(QStringLiteral("ocr_candidate")));

        QCOMPARE(content.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Verify the text block."));
        QCOMPARE(content.at(1).toObject().value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString(), QStringLiteral("data:image/png;base64,AAAA"));
    }

    void checkResponseParsing()
    {
        ExposedGeneralPurposeModel model;

        // The block-recognition contract: the reply itself is the block text.
        const CheckResult text = model.parse("{\"choices\":[{\"message\":{\"role\":\"assistant\","
                                             "\"content\":\"1. Introduction\"}}]}");
        QCOMPARE(text.status, CheckStatus::Fixed);
        QCOMPARE(text.text, QStringLiteral("1. Introduction"));

        // Whitespace around the transcription is trimmed, nothing else.
        const CheckResult padded = model.parse("{\"choices\":[{\"message\":{\"content\":\"  hello world\\n  \"}}]}");
        QCOMPARE(padded.status, CheckStatus::Fixed);
        QCOMPARE(padded.text, QStringLiteral("hello world"));

        // A multi-line block (a table, a list) is transcribed as-is.
        const CheckResult multi = model.parse("{\"choices\":[{\"message\":{\"content\":\"<table>\\n<tr></tr>\\n</table>\"}}]}");
        QCOMPARE(multi.status, CheckStatus::Fixed);
        QCOMPARE(multi.text, QStringLiteral("<table>\n<tr></tr>\n</table>"));

        // A reply that is one Markdown code fence — VL models do this to
        // tables — carries packaging, not block text: the fence is unwrapped
        // and the table kept verbatim, with or without a language tag.
        const CheckResult fenced = model.parse("{\"choices\":[{\"message\":{\"content\":\"```html\\n<table>\\n<tr><td>x</td></tr>\\n</table>\\n```\"}}]}");
        QCOMPARE(fenced.status, CheckStatus::Fixed);
        QCOMPARE(fenced.text, QStringLiteral("<table>\n<tr><td>x</td></tr>\n</table>"));

        const CheckResult bareFence = model.parse("{\"choices\":[{\"message\":{\"content\":\"```\\nplain text\\n```\"}}]}");
        QCOMPARE(bareFence.status, CheckStatus::Fixed);
        QCOMPARE(bareFence.text, QStringLiteral("plain text"));

        // A fence around only part of the reply is content, not packaging.
        const CheckResult partialFence = model.parse("{\"choices\":[{\"message\":{\"content\":\"Before.\\n```html\\n<table></table>\\n```\\nAfter.\"}}]}");
        QCOMPARE(partialFence.status, CheckStatus::Fixed);
        QCOMPARE(partialFence.text, QStringLiteral("Before.\n```html\n<table></table>\n```\nAfter."));

        // An empty reply — nothing but end-of-sentence markers — means the
        // block could not be transcribed: a verdict (needs human eyes), not a
        // protocol error.
        const CheckResult markerOnly = model.parse("{\"choices\":[{\"message\":{\"content\":"
                                                   "\"<\uFF5Cend\u2581of\u2581sentence\uFF5C>\"}}]}");
        QCOMPARE(markerOnly.status, CheckStatus::Review);
        QVERIFY(markerOnly.text.isEmpty());

        // A think block must be dropped; only the final answer remains.
        const CheckResult withThink = model.parse("{\"choices\":[{\"message\":{\"content\":"
                                                  "\" thinkingreasoning response\\nrecognized text\"}}]}");
        QVERIFY2(withThink.status == CheckStatus::Fixed, withThink.errorMessage.text().toUtf8().constData());
        QCOMPARE(withThink.text, QStringLiteral("recognized text"));

        // Trailing end-of-sentence markers must be stripped (ADR 18 parity).
        const CheckResult withMarker = model.parse("{\"choices\":[{\"message\":{\"content\":"
                                                   "\"recognized\\n<\uFF5Cend\u2581of\u2581sentence\uFF5C>\"}}]}");
        QCOMPARE(withMarker.status, CheckStatus::Fixed);
        QCOMPARE(withMarker.text, QStringLiteral("recognized"));

        // Broken transports stay failures, not silent verdicts.
        const CheckResult emptyChoices = model.parse("{\"choices\":[]}");
        QCOMPARE(emptyChoices.status, CheckStatus::Failed);
        QVERIFY(!emptyChoices.errorMessage.isEmpty());

        const CheckResult notJson = model.parse("not json");
        QCOMPARE(notJson.status, CheckStatus::Failed);
        QVERIFY(!notJson.errorMessage.isEmpty());
    }

    void checkSendsRequestAndParsesResult()
    {
        RecordingServer server;
        QVERIFY(server.start());

        QImage image(64, 48, QImage::Format_ARGB32);
        image.fill(Qt::gray);

        CheckRequest request;
        request.image = image;
        request.systemPrompt = QStringLiteral("System");
        request.typePrompt = QStringLiteral("Verify.");
        request.modelId = QStringLiteral("ocr-verifier");

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        const auto model = std::make_unique<GeneralPurposeModel>();
        QFuture<CheckResult> future = model->check(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(server.gotRequest);
        QVERIFY2(future.result().status != CheckStatus::Failed, future.result().errorMessage.text().toUtf8().constData());
        // The reply body ("ok") IS the block text under the transcription contract.
        QCOMPARE(future.result().status, CheckStatus::Fixed);
        QCOMPARE(future.result().text, QStringLiteral("ok"));
    }
};

QTEST_MAIN(TestOcrModels)
#include "test_ocr_models.moc"
