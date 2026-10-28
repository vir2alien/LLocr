#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "core/ConnectionConfig.h"
#include "models/LayoutModel.h"
#include "parsers/MinerULayoutParser.h"
#include "parsers/TeleOcrLayoutParser.h"

using namespace llocr;

namespace {

// The markup request formation is exposed for white-box checks the same way
// DecisionModel is.
class ExposedLayoutModel : public LayoutModel
{
public:
    static QByteArray build(const CheckRequest &request, const QByteArray &imageDataUrl) { return buildRequestBody(request, imageDataUrl); }
    static CheckResult parse(const QByteArray &responseData) { return parseResponse(responseData); }
};

// A loopback HTTP server that records the request line and body and answers
// with a fixed chat completion.
class RecordingServer : public QObject
{
public:
    QByteArray replyBody = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}");
    QString requestPath;
    QByteArray requestBody;
    bool gotRequest = false;

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
        const int firstSpace = raw.indexOf(' ');
        const int secondSpace = raw.indexOf(' ', firstSpace + 1);
        requestPath = raw.mid(firstSpace + 1, secondSpace - firstSpace - 1);
        requestBody = raw.mid(headerEnd + 4, contentLength);
        gotRequest = true;

        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " +
                      QByteArray::number(replyBody.size()) +
                      "\r\n"
                      "Connection: close\r\n\r\n" +
                      replyBody);
        socket->flush();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

CheckRequest layoutRequest()
{
    CheckRequest request;
    request.image = QImage(8, 8, QImage::Format_ARGB32);
    request.image.fill(Qt::gray);
    request.systemPrompt = QStringLiteral("You are a document layout analysis engine.");
    request.typePrompt = QStringLiteral("Analyze the image layout.");
    request.modelId = QStringLiteral("llocr-layout");
    RequestParameter temperature;
    temperature.name = QStringLiteral("temperature");
    temperature.order = 1;
    temperature.value = 0.0;
    RequestParameter cache;
    cache.name = QStringLiteral("cache_prompt");
    cache.order = 2;
    cache.kind = RequestValueKind::Boolean;
    cache.value = true;
    request.parameters = {temperature, cache};
    request.task = CheckRequest::Task::Layout;
    return request;
}

}  // namespace

class TestLayout : public QObject
{
    Q_OBJECT

private slots:
    // The markup request shape: the system contract, then the user message
    // with the page image first and the layout prompt last (the order TeleOCR
    // is documented for), plus the layout profile parameters top-level.
    void requestBodyCarriesSystemPromptAndImage()
    {
        const QByteArray body = ExposedLayoutModel::build(layoutRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonObject root = QJsonDocument::fromJson(body).object();

        const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("You are a document layout analysis engine."));

        const QJsonArray content = messages.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(content.size(), 2);
        QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
        QCOMPARE(content.at(0).toObject().value(QStringLiteral("image_url")).toObject().value(QStringLiteral("url")).toString(), QStringLiteral("data:image/png;base64,AAAA"));
        QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        QCOMPARE(content.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Analyze the image layout."));

        QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("llocr-layout"));
        QCOMPARE(root.value(QStringLiteral("temperature")).toDouble(), 0.0);
        QCOMPARE(root.value(QStringLiteral("cache_prompt")).toBool(), true);
    }

    // The markup reply travels the standard chat-completions route and the
    // stripped content is exactly what the layout parser consumes.
    void markupRoundTripThroughALoopbackServer()
    {
        RecordingServer server;
        server.replyBody = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"<box:34 56 789 123><label:title><up>\\n"
                                             "<box:100 200 900 800><label:text><down>\\n\"}}]}");
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        LayoutModel model;
        QFuture<CheckResult> future = model.check(layoutRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(server.gotRequest);
        QCOMPARE(server.requestPath, QStringLiteral("/v1/chat/completions"));

        const CheckResult result = future.result();
        QCOMPARE(result.status, CheckStatus::Recognized);
        QVERIFY(result.text.contains(QStringLiteral("<label:title>")));

        const OcrResult parsed = TeleOcrLayoutParser().parse(result.text);
        QVERIFY(parsed.success);
        QCOMPARE(parsed.pages.first().boxes.size(), 2);
        QVERIFY(parsed.pages.first().boxes.at(0).positioned);
    }

    // An empty reply is a verdict ("nothing recognized"), not an error: the
    // markup pass reports a failed page instead of wiping the blocks.
    void emptyReplyIsNotAFailure()
    {
        ExposedLayoutModel model;
        const CheckResult empty = model.parse("{\"choices\":[{\"message\":{\"content\":\"\"}}]}");
        QCOMPARE(empty.status, CheckStatus::Review);
        QVERIFY(empty.text.isEmpty());

        const CheckResult garbage = model.parse("{ not json");
        QCOMPARE(garbage.status, CheckStatus::Failed);
        QVERIFY(!garbage.errorMessage.text().isEmpty());
    }

    // MinerU's layout reply is special-token markup: the <|box_start|> family
    // is the data itself. The layout model reads the reply structurally — no
    // service-token cleanup — so the markers reach the parser. (The block
    // recognition model strips them; that contract is pinned in
    // test_ocr_models.)
    void layoutReplyKeepsTheSpecialTokenMarkers()
    {
        const QByteArray reply = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\""
                                                   "<|box_start|>116 101 273 117<|box_end|><|ref_start|>title<|ref_end|><|rotate_up|>\\n"
                                                   "<|box_start|>113 132 885 310<|box_end|><|ref_start|>text<|ref_end|><|rotate_up|><|im_end|>\"}}]}");

        const CheckResult structural = ExposedLayoutModel::parse(reply);
        QCOMPARE(structural.status, CheckStatus::Recognized);
        QVERIFY(structural.text.contains(QStringLiteral("<|box_start|>")));

        const OcrResult parsed = MinerULayoutParser().parse(structural.text);
        QVERIFY(parsed.success);
        QCOMPARE(parsed.pages.first().boxes.size(), 2);
        QCOMPARE(parsed.pages.first().boxes.at(0).label, QStringLiteral("title"));
        QVERIFY(parsed.pages.first().boxes.at(0).positioned);
        QCOMPARE(parsed.pages.first().boxes.at(1).label, QStringLiteral("text"));
    }

    // The rotation token after every block (ADR 245): the direction the
    // block's text top points, kept on the box for the page-level vote. A
    // line without the token reads as up.
    void theMineruReplyCarriesTheBlockRotation()
    {
        const QByteArray reply = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\""
                                                   "<|box_start|>113 132 885 310<|box_end|><|ref_start|>text<|ref_end|><|rotate_left|>\n"
                                                   "<|box_start|>113 320 885 498<|box_end|><|ref_start|>text<|ref_end|><|rotate_right|>\n"
                                                   "<|box_start|>113 508 885 686<|box_end|><|ref_start|>text<|ref_end|>\n"
                                                   "<|box_start|>113 696 885 874<|box_end|><|ref_start|>page_number<|ref_end|><|rotate_down|>\"}}]}");

        const OcrResult parsed = MinerULayoutParser().parse(QString::fromUtf8(reply));
        QVERIFY(parsed.success);
        QCOMPARE(parsed.pages.first().boxes.size(), 4);
        QCOMPARE(parsed.pages.first().boxes.at(0).orientation, 270);
        QCOMPARE(parsed.pages.first().boxes.at(1).orientation, 90);
        QCOMPARE(parsed.pages.first().boxes.at(2).orientation, 0);
        QCOMPARE(parsed.pages.first().boxes.at(3).orientation, 180);
    }

    // TeleOCR carries the same word in the third tag of its line.
    void theTeleocrReplyCarriesTheBlockRotation()
    {
        const QByteArray reply = QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\""
                                                   "<box:34 56 789 123><label:title><up>\n"
                                                   "<box:34 133 789 200><label:text><left>\n"
                                                   "<box:34 210 789 277><label:text><right>\"}}]}");

        const OcrResult parsed = TeleOcrLayoutParser().parse(QString::fromUtf8(reply));
        QVERIFY(parsed.success);
        QCOMPARE(parsed.pages.first().boxes.size(), 3);
        QCOMPARE(parsed.pages.first().boxes.at(0).orientation, 0);
        QCOMPARE(parsed.pages.first().boxes.at(1).orientation, 270);
        QCOMPARE(parsed.pages.first().boxes.at(2).orientation, 90);
    }
};

QTEST_MAIN(TestLayout)
#include "test_layout.moc"
