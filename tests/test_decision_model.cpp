#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "core/ConnectionConfig.h"
#include "models/DecisionModel.h"

using namespace llocr;

namespace {

// Exposes the protected request/response formation for white-box tests.
class ExposedDecisionModel : public DecisionModel
{
public:
    QByteArray build(const DecisionRequest &request, const QByteArray &imageDataUrl) { return buildRequestBody(request, imageDataUrl); }

    DecisionResult parse(const QByteArray &responseData) { return parseResponse(responseData); }
};

// A loopback HTTP server that records the request line and body and answers
// with a fixed /v1/systemone body.
class RecordingServer : public QObject
{
public:
    QByteArray replyBody = QByteArrayLiteral("{\"answers\": {\"match\": {\"noul\": 0.97}}, \"usage\": {\"input_tokens\": 42, \"output_tokens\": 0}}");
    QString requestPath;
    QByteArray requestBody;
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
        const int firstSpace = raw.indexOf(' ');
        const int secondSpace = raw.indexOf(' ', firstSpace + 1);
        requestPath = raw.mid(firstSpace + 1, secondSpace - firstSpace - 1);
        requestBody = raw.mid(headerEnd + 4, contentLength);
        gotRequest = true;
        if (holdResponse)
            return;

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

DecisionRequest testRequest()
{
    DecisionRequest request;
    request.image = QImage(8, 8, QImage::Format_ARGB32);
    request.image.fill(Qt::gray);
    request.stateText = QStringLiteral("1. Intr0duction");
    request.question = QStringLiteral("Does the image show exactly this text?");
    return request;
}

}  // namespace

class TestDecisionModel : public QObject
{
    Q_OBJECT

private slots:
    void requestBodyCarriesStateImageAndQuestion()
    {
        ExposedDecisionModel model;
        const QByteArray body = model.build(testRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());

        const QJsonObject root = doc.object();
        // The state is data, the images array carries one data URL, and the
        // single question is the profile's yes/no wording. No model field:
        // a llama-server serves exactly one model.
        QCOMPARE(root.value(QStringLiteral("state")).toString(), QStringLiteral("1. Intr0duction"));
        const QJsonArray images = root.value(QStringLiteral("images")).toArray();
        QCOMPARE(images.size(), 1);
        QCOMPARE(images.first().toString(), QStringLiteral("data:image/png;base64,AAAA"));
        const QJsonObject match = root.value(QStringLiteral("questions")).toObject().value(QStringLiteral("match")).toObject();
        QCOMPARE(match.value(QStringLiteral("type")).toString(), QStringLiteral("noul"));
        QCOMPARE(match.value(QStringLiteral("instructions")).toString(), QStringLiteral("Does the image show exactly this text?"));
        QVERIFY(!root.contains(QStringLiteral("model")));
        QVERIFY(!root.contains(QStringLiteral("messages")));
    }

    void responseParsingAcceptsAllSpellings()
    {
        ExposedDecisionModel model;

        // The documented shape: a named map of answers, {"noul": x}.
        const DecisionResult noul = model.parse("{\"answers\": {\"match\": {\"noul\": 0.97}}, \"usage\": {\"input_tokens\": 42, \"output_tokens\": 0}}");
        QVERIFY(noul.ok);
        QCOMPARE(noul.probability, 0.97);

        // Older/newer endpoint versions: a bare number, or {"probability": x}.
        const DecisionResult flat = model.parse("{\"answers\": {\"match\": 0.42}}");
        QVERIFY(flat.ok);
        QCOMPARE(flat.probability, 0.42);

        const DecisionResult probability = model.parse("{\"answers\": {\"match\": {\"probability\": 0.5}}}");
        QVERIFY(probability.ok);
        QCOMPARE(probability.probability, 0.5);

        // A probability outside [0, 1] is clamped, not trusted.
        const DecisionResult clamped = model.parse("{\"answers\": {\"match\": 1.5}}");
        QVERIFY(clamped.ok);
        QCOMPARE(clamped.probability, 1.0);
    }

    void responseParsingRejectsBrokenAnswers()
    {
        ExposedDecisionModel model;

        const DecisionResult malformed = model.parse("{ not json");
        QVERIFY(!malformed.ok);
        QVERIFY(!malformed.errorMessage.text().isEmpty());

        const DecisionResult noAnswers = model.parse("{\"usage\": {\"input_tokens\": 42}}");
        QVERIFY(!noAnswers.ok);

        const DecisionResult noMatch = model.parse("{\"answers\": {\"other\": {\"noul\": 0.9}}}");
        QVERIFY(!noMatch.ok);

        const DecisionResult stringAnswer = model.parse("{\"answers\": {\"match\": \"yes\"}}");
        QVERIFY(!stringAnswer.ok);

        const DecisionResult objectAnswer = model.parse("{\"answers\": {\"match\": {\"noul\": \"high\"}}}");
        QVERIFY(!objectAnswer.ok);
    }

    void judgeSendsRequestToSystemoneAndParsesTheAnswer()
    {
        RecordingServer server;
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        DecisionModel model;
        QFuture<DecisionResult> future = model.judge(testRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(server.gotRequest);

        // The decision endpoint is a llama.cpp extension: the request must hit
        // /v1/systemone, not the chat-completions route.
        QCOMPARE(server.requestPath, QStringLiteral("/v1/systemone"));

        const QJsonObject body = QJsonDocument::fromJson(server.requestBody).object();
        QCOMPARE(body.value(QStringLiteral("state")).toString(), QStringLiteral("1. Intr0duction"));
        QVERIFY(!server.requestBody.contains("\"messages\""));

        const DecisionResult result = future.result();
        QVERIFY2(result.ok, result.errorMessage.text().toUtf8().constData());
        QCOMPARE(result.probability, 0.97);
    }

    void judgeFailsOnServerGarbage()
    {
        RecordingServer server;
        server.replyBody = QByteArrayLiteral("<html>not json</html>");
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        DecisionModel model;
        const auto future = model.judge(testRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);
        QVERIFY(!future.result().ok);
        QVERIFY(!future.result().errorMessage.text().isEmpty());
    }

    void abortStopsTheInFlightRequest()
    {
        RecordingServer server;
        server.holdResponse = true;
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.responseTimeoutMs = 10000;

        DecisionModel model;
        const auto future = model.judge(testRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(server.gotRequest, 5000);
        model.abort();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(!future.result().ok);
        QVERIFY(!future.result().errorMessage.text().isEmpty());
    }
};

QTEST_MAIN(TestDecisionModel)
#include "test_decision_model.moc"
