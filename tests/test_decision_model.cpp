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
    QByteArray build(const DecisionRequest &request, const QByteArray &imageDataUrl, const QByteArray &renderImageDataUrl, const QString &modelId = QString())
    {
        return buildRequestBody(request, imageDataUrl, renderImageDataUrl, modelId);
    }

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
    request.visualDiff = true;
    request.image = QImage(8, 8, QImage::Format_ARGB32);
    request.image.fill(Qt::gray);
    request.renderImage = QImage(8, 8, QImage::Format_ARGB32);
    request.renderImage.fill(Qt::darkGray);
    request.recognizedText = QStringLiteral("1. Intr0duction");
    return request;
}

}  // namespace

class TestDecisionModel : public QObject
{
    Q_OBJECT

private slots:
    // The visual-diff decision (MiniCPM-OCR Jev, docs/31): the state is the
    // bare OCR text, the render of the candidate rides next to the crop in
    // that order, and the single noul question carries neither instructions
    // nor criteria — the server rejects a request that sends either. The wire
    // never carries any other question.
    void visualDiffRequestBodyCarriesTwoImagesAndABareQuestion()
    {
        ExposedDecisionModel model;
        const QByteArray body = model.build(testRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"), QByteArrayLiteral("data:image/png;base64,BBBB"), QStringLiteral("llocr-decision"));
        const QJsonObject root = QJsonDocument::fromJson(body).object();

        QCOMPARE(root.value(QStringLiteral("state")).toString(), QStringLiteral("1. Intr0duction"));
        const QJsonArray images = root.value(QStringLiteral("images")).toArray();
        QCOMPARE(images.size(), 2);
        QCOMPARE(images.first().toString(), QStringLiteral("data:image/png;base64,AAAA"));
        QCOMPARE(images.last().toString(), QStringLiteral("data:image/png;base64,BBBB"));
        const QJsonObject questions = root.value(QStringLiteral("questions")).toObject();
        QCOMPARE(questions.size(), 1);
        const QJsonObject match = questions.value(QStringLiteral("match")).toObject();
        QCOMPARE(match.value(QStringLiteral("type")).toString(), QStringLiteral("noul"));
        QVERIFY(!match.contains(QStringLiteral("instructions")));
        QVERIFY(!match.contains(QStringLiteral("criteria")));
        // The System One protocol names the model that answers (llama.cpp:
        // the loaded model's name or its alias); a server that routes by the
        // field rejects a request without it.
        QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("llocr-decision"));
        QVERIFY(!root.contains(QStringLiteral("messages")));
    }

    void visualDiffAnswerReadsLikeAnyNoulAnswer()
    {
        ExposedDecisionModel model;
        const DecisionResult result = model.parse("{\"answers\": {\"match\": {\"noul\": 0.99509}}, \"usage\": {\"input_tokens\": 42, \"output_tokens\": 0}}");
        QVERIFY(result.ok);
        QCOMPARE(result.probability, 0.99509);
    }

    void requestBodyWithoutAKnownModelCarriesNoModelField()
    {
        ExposedDecisionModel model;
        const QJsonObject root = QJsonDocument::fromJson(model.build(testRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"), QByteArrayLiteral("data:image/png;base64,BBBB"))).object();
        // An empty name is not a name: the field stays out rather than travel
        // as an empty string, which the server would reject.
        QVERIFY(!root.contains(QStringLiteral("model")));
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

    void routingIsTheThresholdSplit()
    {
        // The visual-diff protocol answers only `match`, so the verdict is
        // the threshold alone: Ok at or above it, Mismatch below. A crop the
        // old text protocol would have sent to Review (unreadable, empty)
        // now lands on Mismatch and goes to the fixer, which answers REVIEW
        // for a crop it cannot read (docs/31).
        DecisionResult result;
        result.ok = true;
        result.probability = 0.7;
        QCOMPARE(result.routedStatus(0.7), CheckStatus::Ok);
        result.probability = 0.69;
        QCOMPARE(result.routedStatus(0.7), CheckStatus::Mismatch);
        // The per-block threshold applies: the same answer below it is a
        // Mismatch.
        result.probability = 0.96;
        QCOMPARE(result.routedStatus(0.7), CheckStatus::Ok);
        QCOMPARE(result.routedStatus(0.97), CheckStatus::Mismatch);
    }

    void judgeSendsRequestToSystemoneAndParsesTheAnswer()
    {
        RecordingServer server;
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.modelId = QStringLiteral("llocr-decision");
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
        QCOMPARE(body.value(QStringLiteral("images")).toArray().size(), 2);
        // The request names the model that answers: a server that routes by
        // the field (omlx) answers 422 without it.
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("llocr-decision"));
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
