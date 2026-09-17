#include <QtTest>

#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QTimer>

#include "core/ConnectionConfig.h"
#include "core/OcrRequest.h"
#include "models/OcrModelFactory.h"
#include "models/UnlimitedOcrModel.h"

using namespace llocr;

namespace {

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

        const QByteArray replyBody =
            "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"}}]}";
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + QByteArray::number(replyBody.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + replyBody);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

}  // namespace

class TestOcrModels : public QObject {
    Q_OBJECT

private slots:
    void factoryDefaultAndRegistry() {
        QCOMPARE(OcrModelFactory::defaultId(), QStringLiteral("unlimited-ocr"));
        QVERIFY(OcrModelFactory::registeredIds().contains(QStringLiteral("unlimited-ocr")));

        const auto model = OcrModelFactory::create(OcrModelFactory::defaultId());
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), QStringLiteral("unlimited-ocr"));
    }

    void unknownIdFallsBackToDefault() {
        const auto model = OcrModelFactory::create(QStringLiteral("no-such-model"));
        QVERIFY(model != nullptr);
        QCOMPARE(model->id(), OcrModelFactory::defaultId());
    }

    void idNameMapping() {
        const QString id = OcrModelFactory::defaultId();
        const QString name = OcrModelFactory::displayNameForId(id);
        QVERIFY(!name.isEmpty());
        QCOMPARE(OcrModelFactory::idForDisplayName(name), id);
    }

    void unlimitedModelContract() {
        UnlimitedOcrModel model;

        QCOMPARE(model.id(), QStringLiteral("unlimited-ocr"));
        QCOMPARE(model.displayName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(model.defaultParserId(), QStringLiteral("det_tokens"));

        const QList<OcrPromptVariant> variants = model.promptVariants();
        QCOMPARE(variants.size(), 1);
        QCOMPARE(variants.first().text, QStringLiteral("document parsing."));
        QVERIFY(!variants.first().id.isEmpty());
        QVERIFY(!variants.first().title.isEmpty());
    }

    void recognizeSendsDecodableJpegDataUrl() {
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
        config.timeoutMs = 10000;

        const auto model = OcrModelFactory::create(OcrModelFactory::defaultId());
        QVERIFY(model != nullptr);

        QFuture<OcrResult> future = model->recognize(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(server.gotRequest);

        const QJsonDocument doc = QJsonDocument::fromJson(server.body);
        QVERIFY(doc.isObject());
        const QJsonArray content = doc.object()
                                       .value(QStringLiteral("messages")).toArray().at(0).toObject()
                                       .value(QStringLiteral("content")).toArray();
        QString dataUrl;
        QString textPart;
        for (const QJsonValue &part : content) {
            const QJsonObject obj = part.toObject();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("image_url"))
                dataUrl = obj.value(QStringLiteral("image_url")).toObject()
                              .value(QStringLiteral("url")).toString();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
                textPart = obj.value(QStringLiteral("text")).toString();
        }
        QCOMPARE(textPart, QStringLiteral("document parsing."));

        QVERIFY(!dataUrl.isEmpty());
        QVERIFY(dataUrl.startsWith(QStringLiteral("data:image/png;base64,")));
        const QByteArray png = QByteArray::fromBase64(
            dataUrl.mid(QStringLiteral("data:image/png;base64,").size()).toLatin1());
        QVERIFY(png.size() > 100);
        QVERIFY(png.startsWith("\x89PNG"));

        const QImage decoded = QImage::fromData(png, "PNG");
        QVERIFY(!decoded.isNull());
        QCOMPARE(decoded.width(), 137);
        QCOMPARE(decoded.height(), 91);
    }

    void futureCompletesAfterModelDestruction() {
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
        config.timeoutMs = 10000;

        auto model = OcrModelFactory::create(OcrModelFactory::defaultId());
        QFuture<OcrResult> future = model->recognize(request, config);

        // I-05: the async chain is self-contained (no `this` captures), so
        // destroying the model mid-flight must neither dangle nor lose the
        // result — the future still completes with the parsed answer.
        model.reset();

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(future.resultCount() > 0);
        QVERIFY2(future.result().success,
                 future.result().errorMessage.toUtf8().constData());
        QVERIFY(server.gotRequest);
        QCOMPARE(future.result().text, QStringLiteral("ok"));
    }

    void qtAbortOnDeadlineReportsCancellation() {
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

    void requestTimeoutIsNotReportedAsCancellation() {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());
        LlamaClient client;
        const auto url = LlamaClient::endpointUrl(
            QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        const auto future = client.postJson(url, "{}", {}, 200);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(server.gotRequest);
        QVERIFY(!future.result().success);
        QVERIFY2(future.result().error.contains(QStringLiteral("timed out")),
                 qPrintable(future.result().error));
        QVERIFY(future.result().error.contains(QStringLiteral("200")));

        // A timeout must not leak into the next request on the same client.
        server.holdResponse = false;
        const auto next = client.postJson(url, "{}", {}, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(next.isFinished(), 10000);
        QVERIFY2(next.result().success, qPrintable(next.result().error));
    }

    void manualAbortIsNotReportedAsTimeout() {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());
        LlamaClient client;
        const auto future = client.postJson(LlamaClient::endpointUrl(
            QStringLiteral("http://127.0.0.1:%1").arg(server.port())), "{}", {}, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(server.gotRequest, 5000);
        client.abort();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(!future.result().success);
        QVERIFY(!future.result().error.isEmpty());
        QVERIFY(!future.result().error.contains(QStringLiteral("timed out")));
    }

    void recognizeFailsCleanlyOnNullImage() {
        OcrRequest request;
        request.prompt = QStringLiteral("document parsing.");
        request.modelId = QStringLiteral("unlimited-ocr");

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:1");
        config.timeoutMs = 1000;

        const auto model = OcrModelFactory::create(OcrModelFactory::defaultId());
        QVERIFY(model != nullptr);

        QFuture<OcrResult> future = model->recognize(request, config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        const OcrResult result = future.result();
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }
};

QTEST_MAIN(TestOcrModels)
#include "test_ocr_models.moc"
