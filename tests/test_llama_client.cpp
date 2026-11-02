#include <QElapsedTimer>
#include <QProcess>
#include <QtTest>

#include <algorithm>

#include "core/LlamaClient.h"
#include "core/RequestJournal.h"

using namespace llocr;

#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

namespace {
// An address from the TEST-NET/unused range: the TCP connect phase cannot
// complete quickly — it either hangs until the connect timeout or fails with
// unreachable. Both outcomes resolve the future, which is what these tests
// assert; the reason is irrelevant.
constexpr const char *kBlackHoleHost = "10.255.255.1";
constexpr quint16 kBlackHolePort = 81;
}  // namespace

class TestLlamaClient : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        if (m_mock.state() != QProcess::NotRunning) {
            m_mock.kill();
            m_mock.waitForFinished(5000);
        }
    }

    // C++-I4: a second postJson used to overwrite the connect-phase members,
    // so the first future never finished (and its watcher leaked). Both
    // overlapping requests must reach a result.
    void overlappingConnectsBothFinish()
    {
        LlamaClient client;
        const QUrl url(QStringLiteral("http://%1:%2/v1/chat/completions").arg(kBlackHoleHost).arg(kBlackHolePort));
        QFuture<HttpResponse> first = client.postJson(url, "{}", {}, {}, 4000, 4000);
        QFuture<HttpResponse> second = client.postJson(url, "{}", {}, {}, 4000, 4000);

        QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), 15000);
        QVERIFY(!first.result().success);
        QVERIFY(!second.result().success);
        QVERIFY(!first.result().error.isEmpty());
        QVERIFY(!second.result().error.isEmpty());
    }

    void abortFinishesConnectPhase()
    {
        LlamaClient client;
        const QUrl url(QStringLiteral("http://%1:%2/v1/chat/completions").arg(kBlackHoleHost).arg(kBlackHolePort));
        QFuture<HttpResponse> future = client.postJson(url, "{}", {}, {}, 8000, 8000);
        // Some environments answer the black-hole connect instantly (no hang
        // to abort); the premise of this test is a connect still in flight.
        if (future.isFinished())
            QSKIP("the connect finished immediately — nothing to abort mid-flight");

        client.abort();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 8000);
        QVERIFY(!future.result().success);
        QCOMPARE(future.result().error, QStringLiteral("Operation canceled"));
    }

    // A 3xx is refused outright: with Qt's default policy the redirect would
    // be followed and the Bearer's fate on the next hop would not be ours.
    void redirectIsRefusedWithoutFollowing()
    {
        QVERIFY2(startMock({QStringLiteral("LLOCR_MOCK_CHAT_REDIRECT=1")}) != 0, "the mock did not report a listening port");
        LlamaClient client;
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(m_mockPort));
        QFuture<HttpResponse> future = client.postJson(url, "{}", QStringLiteral("secret-key"), {}, 5000, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 20000);
        QVERIFY(!future.result().success);
        QVERIFY(future.result().error.contains(QStringLiteral("redirect")));

        const QStringList lines = RequestJournal::instance().lines();
        QVERIFY(std::any_of(lines.cbegin(), lines.cend(), [](const QString &line) { return line.contains(QStringLiteral("HTTP 302")); }));
        QVERIFY(std::none_of(lines.cbegin(), lines.cend(), [](const QString &line) { return line.contains(QStringLiteral("/moved")) || line.contains(QStringLiteral("secret-key")); }));
    }

    // The endpoint's userinfo (user:token@host) is credentials: it reaches
    // neither the journal nor the error texts.
    void userinfoStaysOutOfJournalAndErrors()
    {
        QVERIFY2(startMock() != 0, "the mock did not report a listening port");
        RequestJournal::instance().clear();

        LlamaClient client;
        const QUrl url(QStringLiteral("http://alice:secret-pw@127.0.0.1:%1/v1/chat/completions").arg(m_mockPort));
        QFuture<HttpResponse> future = client.postJson(url, "{}", {}, {}, 5000, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 20000);
        QVERIFY2(future.result().success, qPrintable(future.result().error));

        const QStringList lines = RequestJournal::instance().lines();
        QVERIFY(std::any_of(lines.cbegin(), lines.cend(), [this](const QString &line) { return line.contains(QStringLiteral("127.0.0.1:%1").arg(m_mockPort)); }));
        QVERIFY(std::none_of(lines.cbegin(), lines.cend(), [](const QString &line) { return line.contains(QStringLiteral("alice")) || line.contains(QStringLiteral("secret-pw")); }));

        const QUrl deadUrl(QStringLiteral("http://alice:secret-pw@%1:%2/v1/chat/completions").arg(kBlackHoleHost).arg(kBlackHolePort));
        QFuture<HttpResponse> dead = client.postJson(deadUrl, "{}", {}, {}, 3000, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(dead.isFinished(), 15000);
        QVERIFY(!dead.result().success);
        QVERIFY(dead.result().error.contains(QStringLiteral("10.255.255.1")));
        QVERIFY(!dead.result().error.contains(QStringLiteral("alice")));
        QVERIFY(!dead.result().error.contains(QStringLiteral("secret-pw")));

        // The 404 leg: Qt's transfer-error text embeds the request's raw URL
        // ("Error transferring <url> — server replied: …"), so the userinfo
        // must be stripped from it in the error text and the journal.
        const QUrl missingUrl(QStringLiteral("http://alice:secret-pw@127.0.0.1:%1/v1/nowhere").arg(m_mockPort));
        QFuture<HttpResponse> missing = client.postJson(missingUrl, "{}", {}, {}, 5000, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(missing.isFinished(), 20000);
        QVERIFY(!missing.result().success);
        QVERIFY2(missing.result().error.contains(QStringLiteral("127.0.0.1:%1").arg(m_mockPort)), qPrintable(missing.result().error));
        QVERIFY2(!missing.result().error.contains(QStringLiteral("alice")), qPrintable(missing.result().error));
        QVERIFY2(!missing.result().error.contains(QStringLiteral("secret-pw")), qPrintable(missing.result().error));

        const QStringList journalLines = RequestJournal::instance().lines();
        QVERIFY(std::none_of(journalLines.cbegin(), journalLines.cend(), [](const QString &line) { return line.contains(QStringLiteral("alice")) || line.contains(QStringLiteral("secret-pw")); }));
    }

    void hugeResponseIsCapped()
    {
        QVERIFY2(startMock({QStringLiteral("LLOCR_MOCK_CHAT_HUGE_BODY=70")}) != 0, "the mock did not report a listening port");
        LlamaClient client;
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(m_mockPort));
        QFuture<HttpResponse> future = client.postJson(url, "{}", {}, {}, 5000, 60000);
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 90000);
        QVERIFY(!future.result().success);
        QVERIFY(future.result().error.contains(QStringLiteral("64 MB")));
    }

private:
    // Spawns the mock with extra KEY=VALUE environment entries and returns
    // the port it bound (0 on failure, with its stderr in m_mockStderr).
    int startMock(const QStringList &extraEnv = {})
    {
        m_mockStderr.clear();
        m_mockPort = 0;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString &entry : extraEnv) {
            const int eq = entry.indexOf(QLatin1Char('='));
            environment.insert(entry.left(eq), entry.mid(eq + 1));
        }
        m_mock.setProcessEnvironment(environment);
        m_mock.start(QString::fromUtf8(LLOCR_MOCK_SERVER), {QStringLiteral("--port"), QStringLiteral("0")});
        if (!m_mock.waitForStarted())
            return 0;
        const QByteArray listening = QByteArrayLiteral("llama server listening on http://127.0.0.1:");
        QElapsedTimer timer;
        timer.start();
        while (!m_mockStderr.contains(listening) && timer.elapsed() < 10000) {
            m_mockStderr += m_mock.readAllStandardError();
            m_mock.waitForReadyRead(100);
        }
        const int from = m_mockStderr.indexOf(listening);
        if (from < 0)
            return 0;
        const int begin = from + listening.size();
        const int end = m_mockStderr.indexOf('\n', begin);
        m_mockPort = m_mockStderr.mid(begin, end - begin).trimmed().toInt();
        return m_mockPort;
    }

    QProcess m_mock;
    QByteArray m_mockStderr;
    int m_mockPort = 0;
};

QTEST_MAIN(TestLlamaClient)
#include "test_llama_client.moc"
