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
    request.blockType = QStringLiteral("title");
    request.recognizedText = QStringLiteral("1. Intr0duction");
    request.notation = QStringLiteral("Markup is a carrier, not content.");
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
        // The state is a JSON object — the candidate text in a named field
        // (block type, text, the profile's notation line), which is what makes
        // the model compare it against the crop. The images array carries one
        // data URL, and the single question is the profile's yes/no wording.
        // No model field: a llama-server serves exactly one model.
        const QJsonObject state = root.value(QStringLiteral("state")).toObject();
        QCOMPARE(state.value(QStringLiteral("block_type")).toString(), QStringLiteral("title"));
        QCOMPARE(state.value(QStringLiteral("recognized_text")).toString(), QStringLiteral("1. Intr0duction"));
        QCOMPARE(state.value(QStringLiteral("notation")).toString(), QStringLiteral("Markup is a carrier, not content."));
        const QJsonArray images = root.value(QStringLiteral("images")).toArray();
        QCOMPARE(images.size(), 1);
        QCOMPARE(images.first().toString(), QStringLiteral("data:image/png;base64,AAAA"));
        const QJsonObject match = root.value(QStringLiteral("questions")).toObject().value(QStringLiteral("match")).toObject();
        QCOMPARE(match.value(QStringLiteral("type")).toString(), QStringLiteral("noul"));
        QCOMPARE(match.value(QStringLiteral("instructions")).toString(), QStringLiteral("Does the image show exactly this text?"));
        // No criteria in the request when the profile ships none: the
        // endpoint's default sides apply.
        QVERIFY(!match.contains(QStringLiteral("criteria")));
        QVERIFY(!root.contains(QStringLiteral("model")));
        QVERIFY(!root.contains(QStringLiteral("messages")));
    }

    void requestBodyCarriesNoulCriteria()
    {
        ExposedDecisionModel model;
        DecisionRequest request = testRequest();
        request.criteriaTrue = QStringLiteral("The image shows the same characters as the state.");
        request.criteriaFalse = QStringLiteral("The image shows at least one different character.");
        const QByteArray body = model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"));
        const QJsonObject match = QJsonDocument::fromJson(body).object().value(QStringLiteral("questions")).toObject().value(QStringLiteral("match")).toObject();
        // The card's noul criteria: both sides travel under their own key.
        const QJsonObject criteria = match.value(QStringLiteral("criteria")).toObject();
        QCOMPARE(criteria.value(QStringLiteral("true")).toString(), QStringLiteral("The image shows the same characters as the state."));
        QCOMPARE(criteria.value(QStringLiteral("false")).toString(), QStringLiteral("The image shows at least one different character."));
    }

    void requestBodyCarriesExactlyOneQuestion()
    {
        // The decision scheme asks ONE question: a noul `match`. Every extra
        // question is a separate forward pass on d3 (~1.4 s measured), so the
        // request must carry nothing else.
        ExposedDecisionModel model;
        DecisionRequest request = testRequest();
        request.question = QStringLiteral("Does the recognized text reproduce the crop?");

        const QJsonObject root = QJsonDocument::fromJson(model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
        const QJsonObject wire = root.value(QStringLiteral("questions")).toObject();
        QCOMPARE(wire.size(), 1);
        QCOMPARE(wire.value(QStringLiteral("match")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("noul"));
        QCOMPARE(wire.value(QStringLiteral("match")).toObject().value(QStringLiteral("instructions")).toString(), QStringLiteral("Does the recognized text reproduce the crop?"));
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

    void responseParsingReadsTheMultiQuestionAnswers()
    {
        ExposedDecisionModel model;

        // The v6 shape: three noul answers plus a choice map under "choice",
        // all from one forward pass (docs/21, §v6).
        const DecisionResult full = model.parse("{\"answers\": {"
                                                "\"match\": {\"type\": \"noul\", \"noul\": 0.18},"
                                                "\"has_text\": {\"type\": \"noul\", \"noul\": 0.99},"
                                                "\"readable\": {\"type\": \"noul\", \"noul\": 0.94},"
                                                "\"diagnosis\": {\"type\": \"choice\", \"choice\": {"
                                                "\"exact_match\": 0.12, \"fixable_ocr_error\": 0.83, \"unreadable_or_clipped\": 0.04, \"empty_crop\": 0.01}}}}");
        QVERIFY(full.ok);
        QCOMPARE(full.probability, 0.18);
        QCOMPARE(full.hasText, 0.99);
        QCOMPARE(full.readable, 0.94);
        QCOMPARE(full.diagnosis.size(), 4);
        QCOMPARE(full.topDiagnosis(), QStringLiteral("fixable_ocr_error"));

        // The other spelling: the class map under "probabilities".
        const DecisionResult probabilities = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"diagnosis\": {\"probabilities\": {\"exact_match\": 0.95, \"fixable_ocr_error\": 0.05}}}}");
        QVERIFY(probabilities.ok);
        QCOMPARE(probabilities.diagnosis.size(), 2);
        QCOMPARE(probabilities.topDiagnosis(), QStringLiteral("exact_match"));

        // The measured llama-server shape (decision-eval probe.json): a
        // string label next to the probabilities map, plus a confidence that
        // must not leak into the diagnosis.
        const DecisionResult measured = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"diagnosis\": {\"type\": \"choice\", \"choice\": \"fixable_ocr_error\", \"probabilities\": "
                                                    "{\"exact_match\": 0.12, \"fixable_ocr_error\": 0.83, \"unreadable_or_clipped\": 0.04, \"empty_crop\": 0.01}, \"confidence\": 0.85}}}");
        QVERIFY(measured.ok);
        QCOMPARE(measured.diagnosis.size(), 4);
        QCOMPARE(measured.topDiagnosis(), QStringLiteral("fixable_ocr_error"));

        // A label with no map at all is a certain verdict for that class.
        const DecisionResult labelOnly = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"diagnosis\": {\"type\": \"choice\", \"choice\": \"exact_match\"}}}");
        QVERIFY(labelOnly.ok);
        QCOMPARE(labelOnly.diagnosis.size(), 1);
        QCOMPARE(labelOnly.topDiagnosis(), QStringLiteral("exact_match"));

        // v5 answers ship match only: the aux fields stay unset (negative).
        const DecisionResult legacy = model.parse("{\"answers\": {\"match\": {\"noul\": 0.97}}}");
        QVERIFY(legacy.ok);
        QCOMPARE(legacy.probability, 0.97);
        QVERIFY(legacy.hasText < 0);
        QVERIFY(legacy.readable < 0);
        QVERIFY(legacy.diagnosis.isEmpty());

        // A present-but-unreadable answer is an error, never a silent verdict.
        const DecisionResult brokenChoice = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"diagnosis\": {\"choice\": {\"exact_match\": \"high\"}}}}");
        QVERIFY(!brokenChoice.ok);
        const DecisionResult emptyChoice = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"diagnosis\": {\"type\": \"choice\"}}}");
        QVERIFY(!emptyChoice.ok);
        const DecisionResult brokenNoul = model.parse("{\"answers\": {\"match\": {\"noul\": 0.9}, \"has_text\": {\"noul\": \"yes\"}}}");
        QVERIFY(!brokenNoul.ok);
    }

    void routingCoversTheFourOutcomes()
    {
        DecisionResult result;
        result.ok = true;
        result.probability = 0.9;
        result.hasText = 0.99;
        result.readable = 0.94;
        result.diagnosis = {{QStringLiteral("exact_match"), 0.12}, {QStringLiteral("fixable_ocr_error"), 0.83}, {QStringLiteral("unreadable_or_clipped"), 0.04}, {QStringLiteral("empty_crop"), 0.01}};

        // A readable crop with a fixable discrepancy goes to the fixer (Mismatch).
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Mismatch);

        // An empty crop is a hallucination — human eyes, no fixer call.
        result.hasText = 0.1;
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Review);
        result.hasText = 0.99;
        result.diagnosis = {{QStringLiteral("empty_crop"), 0.9}};
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Review);

        // An unreadable/clipped crop goes to Review before the fixer burns GPU.
        result.diagnosis = {{QStringLiteral("exact_match"), 0.1}, {QStringLiteral("unreadable_or_clipped"), 0.8}};
        result.readable = 0.2;
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Review);
        result.readable = 0.94;

        // Both heads agree: Ok.
        result.diagnosis = {{QStringLiteral("exact_match"), 0.95}};
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Ok);
        // The per-block threshold applies: same answer below it is a Mismatch.
        QCOMPARE(result.routedStatus(0.96, 0.25, 0.45), CheckStatus::Mismatch);
        // The choice head vetoes a high noul: not exact_match, not Ok.
        result.diagnosis = {{QStringLiteral("fixable_ocr_error"), 0.6}, {QStringLiteral("exact_match"), 0.4}};
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Mismatch);

        // Back-compat: no aux answers (v5 profile, old server) — the
        // threshold-only verdict, Ok above and Mismatch below.
        result.hasText = -1.0;
        result.readable = -1.0;
        result.diagnosis.clear();
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Ok);
        result.probability = 0.5;
        QCOMPARE(result.routedStatus(0.7, 0.25, 0.45), CheckStatus::Mismatch);
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
        QCOMPARE(body.value(QStringLiteral("state")).toObject().value(QStringLiteral("recognized_text")).toString(), QStringLiteral("1. Intr0duction"));
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
