#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeState.h"

using namespace llocr;

#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

class TestServerProcess : public QObject {
    Q_OBJECT

private slots:
    void reachesReadyAndStops();
    void ringBufferCapturesOutput();
    void healthyTimeout();
    void crashAndAutoRestartRecovery();
    void stopDuringStartupIsSafe();
    void reportsTensorLoadPercent();
};

// Helper that builds a server pointed at the mock binary. Timers/network need
// the event loop: QTRY_VERIFY_WITH_TIMEOUT drives it while waiting.
static LlamaServerProcess *makeServer(const QStringList &extraArgs, int timeoutMs,
                                      bool autoRestart, QTemporaryDir &dir,
                                      QString &logFile)
{
    LlamaServerProcess::Options opts;
    opts.program = QString::fromUtf8(LLOCR_MOCK_SERVER);
    QStringList argv = {QStringLiteral("--hello"), QStringLiteral("5")};
    argv.append(extraArgs);
    opts.arguments = argv;
    opts.port = 0;
    opts.startupTimeoutMs = timeoutMs;
    opts.autoRestart = autoRestart;
    opts.logFile = QStringLiteral("%1/llama.log").arg(dir.path());
    logFile = opts.logFile;
    return new LlamaServerProcess(opts);
}

void TestServerProcess::reachesReadyAndStops()
{
    QTemporaryDir dir;
    QString logFile;
    QScopedPointer<LlamaServerProcess> s(makeServer({}, 8000, true, dir, logFile));
    QVERIFY(s->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(s->state() == RuntimeState::Ready, 12000);
    QVERIFY(s->isRunning());
    QVERIFY(s->resolvedPort() > 0);
    QVERIFY(s->startCount() == 1);
    s->stop();
    QTRY_COMPARE_WITH_TIMEOUT(s->state(), RuntimeState::Stopped, 8000);
    QVERIFY(s->isRunning() == false);
    QVERIFY(QFile::exists(logFile));
}

void TestServerProcess::ringBufferCapturesOutput()
{
    QTemporaryDir dir;
    QString logFile;
    QScopedPointer<LlamaServerProcess> m(makeServer({}, 60000, true, dir, logFile));
    QVERIFY(m->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m->state() == RuntimeState::Ready, 12000);
    QTRY_VERIFY_WITH_TIMEOUT(
        static_cast<int>(m->ringBuffer().filter(QStringLiteral("llama_model_loader")).size()) >= 5,
        12000);
    m->stop();
    QTRY_COMPARE_WITH_TIMEOUT(m->state(), RuntimeState::Stopped, 8000);
}

void TestServerProcess::healthyTimeout()
{
    QTemporaryDir dir;
    QString logFile;
    // --never-healthy: /health always 503 → the watchdog times out and fails.
    // --no-models: the /v1/models §2.9 fallback also fails, so readiness cannot
    // be reached through either path and the timeout must surface as Failed.
    QScopedPointer<LlamaServerProcess> m(
        makeServer({QStringLiteral("--never-healthy"), QStringLiteral("--no-models")},
                   1500, false, dir, logFile));
    QVERIFY(m->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m->state() == RuntimeState::Failed, 10000);
    m->stop();
    QTRY_COMPARE_WITH_TIMEOUT(m->state(), RuntimeState::Stopped, 8000);
}

void TestServerProcess::crashAndAutoRestartRecovery()
{
    QTemporaryDir dir;
    QString logFile;
    // The mock stays healthy, then hard-exits after 2500 ms (a real crash loop
    // from /health is per-process by construction — this one is while Ready).
    // Auto-restart must re-spawn and reach Ready again (bounded ≤3 / 5 min).
    QScopedPointer<LlamaServerProcess> m(
        makeServer({QStringLiteral("--crash-after"), QStringLiteral("2500")},
                   60000, true, dir, logFile));
    QVERIFY(m->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m->state() == RuntimeState::Ready, 12000);
    // Wait for the respawn to actually be spawned (not just scheduled): the
    // 500 ms restart window leaves the state at a stale Ready until spawn()
    // runs, so asserting on startCount avoids that race.
    QTRY_VERIFY_WITH_TIMEOUT(m->restartCount() >= 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(m->startCount() >= 2, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(m->state() == RuntimeState::Ready, 20000);
    QVERIFY(m->startCount() >= 2);
    m->stop();
    QTRY_COMPARE_WITH_TIMEOUT(m->state(), RuntimeState::Stopped, 8000);
}

void TestServerProcess::stopDuringStartupIsSafe()
{
    QTemporaryDir dir;
    QString logFile;
    // Stopping while /health is still being polled must land on Stopped, not
    // Failed. --no-models keeps the §2.9 fallback from flipping to Ready mid-test.
    QScopedPointer<LlamaServerProcess> m(
        makeServer({QStringLiteral("--never-healthy"), QStringLiteral("--no-models")},
                   600000, false, dir, logFile));
    QVERIFY(m->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m->state() == RuntimeState::Starting, 3000);
    m->stop();
    QTRY_COMPARE_WITH_TIMEOUT(m->state(), RuntimeState::Stopped, 5000);
}

void TestServerProcess::reportsTensorLoadPercent()
{
    QTemporaryDir dir;
    QString logFile;
    // --progress emits "llama_model_loader: - loading tensors, NN%" lines; the
    // classifier must surface them as a status percentage (§H.7 task 2).
    QScopedPointer<LlamaServerProcess> m(
        makeServer({QStringLiteral("--progress"), QStringLiteral("4")},
                   60000, true, dir, logFile));
    QVERIFY(m->start().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m->loadProgressPercent() == 100, 12000);
    QVERIFY(m->statusMessage().contains(QStringLiteral("Loading model")));
    QVERIFY(m->loadProgressPercent() >= 0 && m->loadProgressPercent() <= 100);
    m->stop();
    QTRY_COMPARE_WITH_TIMEOUT(m->state(), RuntimeState::Stopped, 8000);
}

QTEST_MAIN(TestServerProcess)
#include "test_server_process.moc"