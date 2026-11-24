#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "config/SettingsStore.h"
#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "core/LlamaClient.h"
#include "core/RequestJournal.h"
#include "core/RequestProfile.h"
#include "models/FixModel.h"
#include "runtime/ConnectionMode.h"
#include "runtime/RuntimeLog.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A loopback HTTP server that records every request body and answers with the
// next body from its queue.
class QueueServer : public QObject
{
public:
    QList<QByteArray> replies;
    QList<QByteArray> requests;
    // The status of each answer; 200 unless the test says otherwise.
    QList<int> statuses;

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
        requests.append(raw.mid(headerEnd + 4, contentLength));
        const QByteArray body = replies.isEmpty() ? QByteArrayLiteral("{}") : replies.takeFirst();
        const int status = statuses.isEmpty() ? 200 : statuses.takeFirst();
        const QByteArray reason = status == 401 ? QByteArrayLiteral("Unauthorized") : QByteArrayLiteral("OK");
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                      "\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " +
                      QByteArray::number(body.size()) +
                      "\r\n"
                      "Connection: close\r\n\r\n" +
                      body);
        socket->flush();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

CheckRequest fixRequest()
{
    CheckRequest request;
    request.modelId = QStringLiteral("gemma-4-12b");
    request.image = QImage(8, 8, QImage::Format_ARGB32);
    request.image.fill(Qt::gray);
    request.recognizedText = QStringLiteral("Коэффициент k = 0,85 подтверждён.");
    request.blockType = QStringLiteral("text");
    request.systemPrompt = QStringLiteral("You correct OCR errors in one block of a document.");
    request.typePrompt = QStringLiteral("Block type: text.");

    RequestParameter maxTokens;
    maxTokens.name = QStringLiteral("max_tokens");
    maxTokens.kind = RequestValueKind::Number;
    maxTokens.value = QVariant(2048.0);
    request.parameters.append(maxTokens);

    RequestParameter budget;
    budget.name = QLatin1String(kReasoningBudgetParameter);
    budget.kind = RequestValueKind::Number;
    budget.value = QVariant(512.0);
    request.parameters.append(budget);
    return request;
}

}  // namespace

class TestExternalRuntime : public QObject
{
    Q_OBJECT

    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    // The client keeps its own trace of the requests: the external server
    // writes no log the app could read (ADR 228).
    void journalRecordsTheRequestAndTheReply()
    {
        QueueServer server;
        server.replies << QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"}}]}");
        QVERIFY(server.start());
        RequestJournal::instance().clear();

        LlamaClient client;
        const QUrl url = LlamaClient::endpointUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        const QFuture<HttpResponse> future = client.postJson(url, QByteArrayLiteral("{\"model\":\"m\"}"), QStringLiteral("s3cret"), QStringLiteral("m"), 5000, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
        QVERIFY(future.result().success);
        QCOMPARE(RequestJournal::instance().count(), 1);

        const QString trace = RequestJournal::instance().lines().join(QLatin1Char('\n'));
        QVERIFY(trace.contains(QStringLiteral("POST http://127.0.0.1:%1/v1/chat/completions").arg(server.port())));
        QVERIFY(trace.contains(QStringLiteral("model m")));
        QVERIFY(trace.contains(QStringLiteral("HTTP 200")));
        QVERIFY(trace.contains(QStringLiteral("reply: OK")));
        // The key lives in the Authorization header and never in the trace.
        QVERIFY(!trace.contains(QStringLiteral("s3cret")));
    }

    // A server that wants a key answers 401. The app has to say that in its
    // own words: "server replied: Unauthorized" leaves the user guessing what
    // to do (ADR 233).
    void aRefusedRequestIsExplainedByTheApp()
    {
        QueueServer server;
        server.statuses << 401;
        server.replies << QByteArrayLiteral("{\"error\":{\"message\":\"API key required\"}}");
        QVERIFY(server.start());

        LlamaClient client;
        const QUrl url = LlamaClient::endpointUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        const QFuture<HttpResponse> future = client.postJson(url, QByteArrayLiteral("{\"model\":\"m\"}"), QString(), QStringLiteral("m"), 5000, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
        const HttpResponse response = future.result();
        QVERIFY(!response.success);
        QVERIFY2(response.error.contains(QStringLiteral("401")), qPrintable(response.error));
        QVERIFY2(response.error.contains(QStringLiteral("API key")), qPrintable(response.error));
        // The server's own words stay in the message.
        QVERIFY2(response.error.contains(QStringLiteral("API key required")), qPrintable(response.error));
    }

    // A server that ignores the reasoning budget spends the whole budget on
    // the thinking and answers nothing: the fix task asks again, without the
    // thinking, and says so (ADR 230).
    void fixAsksAgainWithoutTheThinkingWhenTheBudgetWasSpentOnIt()
    {
        QueueServer server;
        server.replies << QByteArrayLiteral("{\"choices\":[{\"message\":{\"reasoning_content\":\"thinking\"},\"finish_reason\":\"length\"}]}")
                       << QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}");
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.modelId = QStringLiteral("gemma-4-12b");
        config.responseTimeoutMs = 10000;

        FixModel model;
        const QFuture<CheckResult> future = model.check(fixRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);

        QCOMPARE(server.requests.size(), 2);
        const QJsonObject first = QJsonDocument::fromJson(server.requests.at(0)).object();
        QCOMPARE(first.value(QStringLiteral("reasoning_budget_tokens")).toInt(), 512);
        QCOMPARE(first.value(QStringLiteral("chat_template_kwargs")).toObject().value(QStringLiteral("enable_thinking")).toBool(), true);

        const QJsonObject second = QJsonDocument::fromJson(server.requests.at(1)).object();
        QCOMPARE(second.value(QStringLiteral("reasoning_budget_tokens")).toInt(), 0);
        QCOMPARE(second.value(QStringLiteral("chat_template_kwargs")).toObject().value(QStringLiteral("enable_thinking")).toBool(), false);
        // The thinking's room goes back to the answer.
        QCOMPARE(second.value(QStringLiteral("max_tokens")).toInt(), 1536);

        const CheckResult result = future.result();
        QCOMPARE(result.status, CheckStatus::Ok);
        QVERIFY(result.notes.text().contains(QStringLiteral("reasoning budget")));
    }

    // The second request to such a server goes without the thinking at once:
    // the server has already shown that it ignores the budget.
    void theNextRequestToSuchAServerSkipsTheThinking()
    {
        QueueServer server;
        server.replies << QByteArrayLiteral("{\"choices\":[{\"message\":{\"reasoning_content\":\"thinking\"},\"finish_reason\":\"length\"}]}")
                       << QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}")
                       << QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}");
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.modelId = QStringLiteral("gemma-4-12b");
        config.responseTimeoutMs = 10000;

        FixModel model;
        QFuture<CheckResult> first = model.check(fixRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 15000);
        QCOMPARE(server.requests.size(), 2);

        const QFuture<CheckResult> second = model.check(fixRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), 15000);
        QCOMPARE(server.requests.size(), 3);
        const QJsonObject body = QJsonDocument::fromJson(server.requests.at(2)).object();
        QCOMPARE(body.value(QStringLiteral("reasoning_budget_tokens")).toInt(), 0);
        QCOMPARE(body.value(QStringLiteral("chat_template_kwargs")).toObject().value(QStringLiteral("enable_thinking")).toBool(), false);
        QCOMPARE(second.result().status, CheckStatus::Ok);
        QVERIFY(second.result().notes.text().contains(QStringLiteral("thinking")));
    }

    // A stop word that matched inside the thinking (the model quotes the
    // framed candidate, ADR 332) stops the reply before the answer — but the
    // budget is not the cause: the request is not retried without the
    // thinking, and the server is not marked as ignoring the budget.
    void stopInsideTheThinkingIsNotABudgetFailure()
    {
        QueueServer server;
        server.replies << QByteArrayLiteral("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"reasoning_content\":\"the candidate is <CANDIDATE> ...\",\"content\":\"\"}}]}")
                       << QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}");
        QVERIFY(server.start());

        ConnectionConfig config;
        config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.port());
        config.modelId = QStringLiteral("gemma-4-12b");
        config.responseTimeoutMs = 10000;

        FixModel model;
        const QFuture<CheckResult> future = model.check(fixRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 15000);

        // One request: no retry, and the reason names the stop, not the budget.
        QCOMPARE(server.requests.size(), 1);
        const CheckResult result = future.result();
        QCOMPARE(result.status, CheckStatus::Failed);
        QVERIFY2(result.errorMessage.text().contains(QStringLiteral("thinking")), qPrintable(result.errorMessage.text()));

        // The next request keeps the thinking: the server was not marked.
        const QFuture<CheckResult> next = model.check(fixRequest(), config);
        QTRY_VERIFY_WITH_TIMEOUT(next.isFinished(), 15000);
        QCOMPARE(server.requests.size(), 2);
        const QJsonObject body = QJsonDocument::fromJson(server.requests.at(1)).object();
        QCOMPARE(body.value(QStringLiteral("reasoning_budget_tokens")).toInt(), 512);
        QCOMPARE(body.value(QStringLiteral("chat_template_kwargs")).toObject().value(QStringLiteral("enable_thinking")).toBool(), true);
    }

    // The log window shows both sources at once: the client's own trace
    // (ADR 228) and the server process's output. The global mode does not
    // choose between them any more — the journal is the client's trace
    // whatever the runtime is.
    void theLogWindowShowsBothSources()
    {
        SettingsStore settings;
        settings.setMode(ConnectionMode::External);
        RuntimeLog log(settings);

        RequestJournal::instance().clear();
        RequestJournal::Record record;
        record.at = QDateTime::currentDateTime();
        record.endpoint = QStringLiteral("http://127.0.0.1:8000/v1/chat/completions");
        record.model = QStringLiteral("d3-mini-oQ8e");
        record.status = 200;
        record.requestBytes = 4096;
        record.responseBytes = 96;
        record.durationMs = 1200;
        record.excerpt = QStringLiteral("OK");
        RequestJournal::instance().record(record);

        QCOMPARE(log.journalLineCount(), 1);
        const QString trace = log.journalLog();
        QVERIFY(trace.contains(QStringLiteral("d3-mini-oQ8e")));
        QVERIFY(trace.contains(QStringLiteral("HTTP 200")));
        QVERIFY(trace.contains(QStringLiteral("reply: OK")));

        // The server process has not run: its own output stays empty.
        QVERIFY(log.serverLog().isEmpty());

        // The managed runtime keeps the journal and adds the server's output.
        settings.setMode(ConnectionMode::Managed);
        QCOMPARE(log.journalLineCount(), 1);
        QVERIFY(log.serverLog().isEmpty());
    }
};

QTEST_MAIN(TestExternalRuntime)
#include "test_external_runtime.moc"
