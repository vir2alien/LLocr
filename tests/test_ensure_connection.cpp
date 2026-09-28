#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "config/RequestProfileStore.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "runtime/InstalledState.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeState.h"
#include "runtime/SelfTestController.h"
#include "testsettings.h"

#include <algorithm>

using namespace llocr;

#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

namespace {

// Launch-profile catalog for tests (test binaries embed no resources): one
// preset with an empty backend tag (matches any installed backend) so the
// store can persist user copies for it. The mock server only needs the core
// argv; individual tests append flags to the preset's user copy.
QString writeTestLaunchCatalog(const QTemporaryDir &dir)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("launch-presets.json"));
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": ["
                                  "{ \"id\": \"test\", \"parameters\": [] }] }"));
        f.commit();
    }
    return path;
}

// Same, but the active preset carries a launch parameter, so a test can change
// the effective launch configuration without touching a setting directly.
QString writeTestLaunchCatalogWithParameter(const QTemporaryDir &dir)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("launch-presets.json"));
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": ["
                                  "{ \"id\": \"test\", \"parameters\": ["
                                  "{ \"order\": 1, \"name\": \"ctx-size\", \"value\": 8192 }"
                                  "] }] }"));
        f.commit();
    }
    return path;
}

}  // namespace

// Stage G-core acceptance: ensureConnectionReady() for External and Managed
// (start → /health → /v1/models → alias), deduplication of concurrent calls,
// cancelPendingStart(), and the health-timeout path. Runs against the mock
// llama-server (tests/mock_llama_server.cpp).
class TestEnsureConnection : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_ensure_connection"));
    }

    void cleanup() { QSettings().clear(); }

    void externalResolvesImmediately()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        LaunchProfileStore launchProfiles(settings, writeTestLaunchCatalog(dir));
        settings.setConnectionMode(QStringLiteral("external"));
        settings.setBaseUrl(QStringLiteral("http://custom.example:9000"));
        settings.setApiKey(QStringLiteral("k"));
        settings.setModelName(QStringLiteral("my-model"));
        settings.setConnectionTimeoutMs(5000);

        RuntimeController runtime(settings, launchProfiles);
        ResolvedConnection conn;
        bool called = false;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            conn = c;
            called = true;
        });
        QVERIFY(called);  // External resolves synchronously (ADR 26)
        QCOMPARE(conn.baseUrl, QStringLiteral("http://custom.example:9000"));
        QCOMPARE(conn.apiKey, QStringLiteral("k"));
        QCOMPARE(conn.modelId, QStringLiteral("my-model"));
        QCOMPARE(conn.timeoutMs, 5000);
        QVERIFY(conn.error.isEmpty());
    }

    void externalCheckRoleUsesCheckModelName()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        LaunchProfileStore launchProfiles(settings, writeTestLaunchCatalog(dir));
        settings.setConnectionMode(QStringLiteral("external"));
        settings.setBaseUrl(QStringLiteral("http://custom.example:9000"));
        settings.setModelName(QStringLiteral("ocr-model"));
        settings.setCheckModelName(QStringLiteral("check-model"));

        RuntimeController runtime(settings, launchProfiles);
        ResolvedConnection ocr, check;
        runtime.ensureConnectionReady(ConnectionRole::Ocr, [&](const ResolvedConnection &c) { ocr = c; });
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) { check = c; });
        QCOMPARE(ocr.modelId, QStringLiteral("ocr-model"));
        QCOMPARE(check.modelId, QStringLiteral("check-model"));
        QCOMPARE(check.baseUrl, ocr.baseUrl);
        QCOMPARE(check.apiKey, ocr.apiKey);
        QCOMPARE(check.timeoutMs, ocr.timeoutMs);

        // An empty check/modelName is sent as-is: single-model servers (e.g.
        // llama-server) ignore the model field entirely, and a multi-model
        // OpenAI-compatible server returns its own "model not found" error,
        // which is surfaced verbatim (§7.5). No silent fallback to the OCR name.
        settings.setCheckModelName(QString());
        ResolvedConnection empty;
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) { empty = c; });
        QVERIFY(empty.modelId.isEmpty());
    }

    // Model per task (ADR 74): when the live managed server carries another
    // role's model, the resolve stops it and restarts it with the requested
    // role's configuration.
    void managedCheckRoleSwitchesServer()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile ocrModel(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        QVERIFY(ocrModel.open(QIODevice::WriteOnly));
        ocrModel.write("ocr");
        ocrModel.close();
        QFile checkModel(dir.filePath(QStringLiteral("check-Q4_K_M.gguf")));
        QVERIFY(checkModel.open(QIODevice::WriteOnly));
        checkModel.write("check");
        checkModel.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("check-Q4_K_M.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        int first = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++first; });
        QTRY_VERIFY_WITH_TIMEOUT(first == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        QList<int> states;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() { states.append(int(runtime.state())); });

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QVERIFY(!resolved.baseUrl.isEmpty());
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(states.contains(int(RuntimeState::Stopped)), "expected the server to be stopped for the role switch");

        states.clear();
        int done2 = 0;
        ResolvedConnection resolved2;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved2 = c;
            ++done2;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done2 == 1, 15000);
        QVERIFY2(resolved2.error.isEmpty(), qPrintable(resolved2.error));
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(states.contains(int(RuntimeState::Stopped)), "expected a restart switching back to the OCR model");

        runtime.stopServer();
    }

    // Regression: a Check request that arrives while an Ocr resolve is still in
    // flight must not be answered with the in-flight (OCR) connection — the
    // single managed server serves one role at a time, so the check request has
    // to get its own dispatch, including the role switch.
    void crossRoleRequestDuringResolveIsNotAnsweredWithTheWrongRole()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile ocrModel(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        QVERIFY(ocrModel.open(QIODevice::WriteOnly));
        ocrModel.write("ocr");
        ocrModel.close();
        QFile checkModel(dir.filePath(QStringLiteral("check-Q4_K_M.gguf")));
        QVERIFY(checkModel.open(QIODevice::WriteOnly));
        checkModel.write("check");
        checkModel.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("check-Q4_K_M.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        QStringList events;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() {
            if (runtime.state() == RuntimeState::Stopped)
                events.append(QStringLiteral("stopped"));
        });

        ResolvedConnection ocrConn;
        ResolvedConnection checkConn;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            ocrConn = c;
            events.append(QStringLiteral("ocr"));
        });
        // Issued while the first resolve is still in flight.
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) {
            checkConn = c;
            events.append(QStringLiteral("check"));
        });

        QTRY_VERIFY_WITH_TIMEOUT(events.contains(QStringLiteral("check")), 20000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(ocrConn.error.isEmpty(), qPrintable(ocrConn.error));
        QVERIFY2(checkConn.error.isEmpty(), qPrintable(checkConn.error));
        QVERIFY(!ocrConn.baseUrl.isEmpty());
        QVERIFY(!checkConn.baseUrl.isEmpty());

        // The OCR request is answered first, and only then does the server switch
        // for the check role.
        QCOMPARE(events.indexOf(QStringLiteral("ocr")), 0);
        QVERIFY2(events.indexOf(QStringLiteral("stopped")) > events.indexOf(QStringLiteral("ocr")), qPrintable(events.join(QLatin1Char(','))));
        QVERIFY2(events.indexOf(QStringLiteral("check")) > events.indexOf(QStringLiteral("stopped")), qPrintable(events.join(QLatin1Char(','))));

        runtime.stopServer();
    }

    // A check resolve without a selected (or existing) check model fails with
    // an actionable message naming the check model, and the server is left
    // untouched.
    void managedCheckRoleRefusedWithoutModel()
    {
        QTemporaryDir dir;
        QFile ocrModel(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(ocrModel.open(QIODevice::WriteOnly));
        ocrModel.write("x");
        ocrModel.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY2(resolved.error.contains(QStringLiteral("Check model"), Qt::CaseInsensitive), qPrintable(resolved.error));
        QVERIFY(resolved.baseUrl.isEmpty());
        QVERIFY(runtime.state() != RuntimeState::Starting);
        QVERIFY(runtime.state() != RuntimeState::Ready);

        // A stale check-model path is named in the error.
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("gone.gguf")));
        ResolvedConnection resolved2;
        runtime.ensureConnectionReady(ConnectionRole::Check, [&](const ResolvedConnection &c) { resolved2 = c; });
        QVERIFY2(resolved2.error.contains(QStringLiteral("gone.gguf")), qPrintable(resolved2.error));
    }

    // The probe spawns the binary and waits for it — up to two minutes on a
    // cold cache. It must not run on the GUI thread: a timer has to keep firing
    // while the resolve is in flight, or the window is frozen (ADR 105).
    // The llama-server owner record is finally read back: a server left running
    // by a previous LLocr run must be detected (ADR 107) and terminable, while
    // a dead pid or a recycled one must be left alone.
    void orphanedServerIsDetectedAndCanBeTerminated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        const RuntimePaths paths(store.runtimeRootDir(), store.runtimeModelsDir());
        paths.ensureDirectories();
        const QString ownerPath = QDir(paths.runtimeDir()).filePath(QStringLiteral("owner.json"));
        QVERIFY(!QFileInfo(ownerPath).absolutePath().isEmpty());

        const auto writeRecord = [&ownerPath](qint64 pid, qint64 parentPid, int port, const QString &program) {
            QJsonObject object;
            object.insert(QStringLiteral("pid"), double(pid));
            object.insert(QStringLiteral("parentPid"), double(parentPid));
            object.insert(QStringLiteral("port"), port);
            object.insert(QStringLiteral("program"), program);
            QFile file(ownerPath);
            if (!file.open(QIODevice::WriteOnly))
                return false;
            file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
            file.close();
            return true;
        };
        const QString mock = QFileInfo(QString::fromUtf8(LLOCR_MOCK_SERVER)).absoluteFilePath();

        // No record at all: nothing to report.
        RuntimeController runtime(store, launchProfiles);
        QVERIFY(!runtime.orphanDetected());
        QVERIFY(runtime.orphanInfo().isEmpty());

        // A record for a dead pid, and one naming ourselves: neither is an
        // orphan. Each check is a fresh controller, i.e. the next app start.
        QVERIFY(writeRecord(999999, 999998, 18080, mock));
        RuntimeController deadRecord(store, launchProfiles);
        QVERIFY2(!deadRecord.orphanDetected(), "a dead pid is not an orphan");

        QVERIFY(writeRecord(QCoreApplication::applicationPid(), 1, 18081, mock));
        RuntimeController ownRecord(store, launchProfiles);
        QVERIFY2(!ownRecord.orphanDetected(), "our own server is not an orphan");

        // A live foreign process whose image matches the record: an orphan.
        QProcess server;
        server.start(mock, {QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--never-healthy")});
        QVERIFY(server.waitForStarted(10000));
        QVERIFY(writeRecord(server.processId(), 999998, 18082, QFileInfo(server.program()).absoluteFilePath()));

        RuntimeController runtime2(store, launchProfiles);
        QVERIFY2(runtime2.orphanDetected(), "a live llama-server from a previous run must be reported");
        QVERIFY(runtime2.orphanInfo().contains(QString::number(server.processId())));

        const QString result = runtime2.terminateOrphan();
        QVERIFY2(result.isEmpty(), qPrintable(result));
        QVERIFY(!runtime2.orphanDetected());
        QVERIFY(server.waitForFinished(10000));
        QVERIFY(!QFile::exists(ownerPath));
    }

    // A moved runtime directory must take everything with it: the paths, and the
    // install lock. RuntimeInstaller used to freeze both in its constructor
    // while rescanInstalledBuilds() already looked in the new tree (ADR 109).
    void movingTheRuntimeDirectoryMovesTheInstallLock()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime-a")));
        settings.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        InstalledState installed(settings);

        const QString lockA = installed.paths().installLockPath();
        installed.ensureDirectories();  // the lock file needs its directory
        QCOMPARE(installed.installLock().fileName(), lockA);
        QVERIFY(installed.installLock().tryLock(0));
        QVERIFY(installed.installLock().isLocked());
        installed.installLock().unlock();

        settings.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime-b")));
        QSignalSpy changed(&installed, &InstalledState::pathsChanged);
        installed.installLock();  // a request through the state refreshes it

        const QString lockB = installed.paths().installLockPath();
        QVERIFY(lockB != lockA);
        installed.ensureDirectories();
        QCOMPARE(installed.installLock().fileName(), lockB);
        QVERIFY(installed.installLock().tryLock(0));
        installed.installLock().unlock();
        // The old directory is a different path entirely, so the old lock file
        // cannot be mistaken for the new one.
        QVERIFY(!installed.paths().installLockPath().contains(QStringLiteral("runtime-a")));
    }

    void managedStartKeepsTheEventLoopRunning()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        // Make the probe deterministically slow (the mock delays its --version
        // answer), so a probe running on the GUI thread would freeze the loop
        // for the whole delay. The binary is copied to a unique path first:
        // ServerCapabilities caches per path, so probing the shared mock would be
        // a cache hit and no binary would be spawned at all. The assertion is on
        // the *longest gap* between timer ticks, not on the tick count — the rest
        // of the start is asynchronous and would keep the loop busy, so a count
        // would pass even with a blocking probe.
        const QString mock = QFileInfo(QString::fromUtf8(LLOCR_MOCK_SERVER)).absoluteFilePath();
        const QString serverCopy = QDir(dir.path()).filePath(QStringLiteral("llama-server-copy"));
        QVERIFY(QFile::copy(mock, serverCopy));
        store.setServerPath(serverCopy);

        constexpr int kVersionDelayMs = 800;
        ::qputenv("LLOCR_MOCK_VERSION_DELAY_MS", QByteArray::number(kVersionDelayMs));

        int ticks = 0;
        qint64 maxGapMs = 0;
        qint64 lastTick = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (lastTick > 0)
                maxGapMs = std::max(maxGapMs, now - lastTick);
            lastTick = now;
            ++ticks;
        });
        heartbeat.start(5);
        // Let the timer establish a baseline tick before the resolve, otherwise
        // there is no "previous tick" to measure a gap against and a block at
        // the very beginning of the start would go unnoticed.
        QTest::qWait(60);
        lastTick = QDateTime::currentMSecsSinceEpoch();
        maxGapMs = 0;

        int finished = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++finished;
        });

        QTRY_VERIFY_WITH_TIMEOUT(finished == 1, 20000);
        ::qunsetenv("LLOCR_MOCK_VERSION_DELAY_MS");
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(ticks > 0, "the timer never fired");
        // A probe on the GUI thread blocks the loop for the whole delay; the
        // health poll ticks every 250 ms, so anything near the delay is a block.
        QVERIFY2(maxGapMs < kVersionDelayMs / 2,
                 qPrintable(QStringLiteral("longest gap between timer ticks: %1 ms "
                                           "(probe delay was %2 ms, %3 ticks total)")
                                .arg(maxGapMs)
                                .arg(kVersionDelayMs)
                                .arg(ticks)));

        runtime.stopServer();
    }

    void managedStartsAndResolvesAlias()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A "model file" the configValid gate requires to exist.
        QFile model(dir.filePath(QStringLiteral("model-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("dummy");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("model-Q4_K_M.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setAutoStart(false);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);
        QVERIFY(runtime.configValid());

        int finished = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++finished;
        });

        QTRY_VERIFY_WITH_TIMEOUT(finished == 1, 15000);
        QVERIFY(resolved.baseUrl.startsWith(QStringLiteral("http://127.0.0.1:")));
        QCOMPARE(resolved.modelId, QStringLiteral("llocr-local"));
        QVERIFY(resolved.apiKey.isEmpty());
        QVERIFY(resolved.error.isEmpty());

        runtime.stopServer();
    }

    void concurrentCallersShareFuture()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        // Two "concurrent" callers issued before the first one completes. Only a
        // single start may happen; both must end up with the same connection.
        int done = 0;
        ResolvedConnection ra, rb;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            ra = c;
            ++done;
        });
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            rb = c;
            ++done;
        });

        QTRY_VERIFY_WITH_TIMEOUT(done == 2, 15000);
        QVERIFY(!ra.baseUrl.isEmpty());
        QVERIFY(!rb.baseUrl.isEmpty());
        QCOMPARE(ra.baseUrl, rb.baseUrl);
        QCOMPARE(ra.modelId, rb.modelId);

        runtime.stopServer();
    }

    void cancelPendingStartInterrupts()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        // Delay the socket bind so the resolve stays in Starting. With launch
        // settings gone from QSettings the delay goes through the launch
        // profile's user copy (a valueless flag row).
        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("delay-start"), QStringLiteral("4000")));
        launchProfiles.saveDraft();
        store.setStartupTimeoutMs(30000);

        RuntimeController runtime(store, launchProfiles);

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });

        QTRY_VERIFY_WITH_TIMEOUT(runtime.state() == RuntimeState::Starting, 5000);
        runtime.cancelPendingStart();

        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 5000);
        QVERIFY(resolved.baseUrl.isEmpty());
        QVERIFY(resolved.error.contains(QStringLiteral("cancel")));

        runtime.stopServer();
    }

    void healthTimeoutSurfacesError()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setAutoRestart(false);
        store.setStartupTimeoutMs(1200);
        // --never-healthy alone is rescued by the /v1/models fallback (the mock
        // answers it with 200); --no-models disables that fallback so the
        // health watchdog truly hits the startup timeout. Both flags go
        // through the launch profile's user copy.
        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("never-healthy"), QString()));
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("no-models"), QString()));
        launchProfiles.saveDraft();

        RuntimeController runtime(store, launchProfiles);
        ResolvedConnection resolved;
        int done = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });

        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY(resolved.baseUrl.isEmpty());
        QVERIFY(!resolved.error.isEmpty());

        runtime.stopServer();
    }

    void runSelfTestSucceeds()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        // Request-profile store for the self-test request; written to the temp
        // dir because test binaries embed no resources.
        const QString defaultsPath = dir.filePath(QStringLiteral("request-defaults.json"));
        {
            QFile defaultsFile(defaultsPath);
            QVERIFY(defaultsFile.open(QIODevice::WriteOnly));
            defaultsFile.write(QByteArrayLiteral("{\"schemaVersion\":1,\"parameters\":["
                                                 "{\"order\":1,\"name\":\"temperature\",\"value\":0.0},"
                                                 "{\"order\":2,\"name\":\"max_tokens\",\"value\":1024},"
                                                 "{\"order\":3,\"name\":\"stream\",\"value\":false}]}")
                                   .constData());
        }
        RequestProfileStore profiles(store, defaultsPath);
        SelfTestController selfTest(store, runtime, profiles);
        SelfTestResult result;
        int done = 0;
        QFutureWatcher<SelfTestResult> watch;
        connect(&watch, &QFutureWatcher<SelfTestResult>::finished, this, [&]() {
            result = watch.result();
            ++done;
        });
        watch.setFuture(selfTest.runSelfTest());

        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.text, QStringLiteral("SELFTEST_OK"));

        runtime.stopServer();
    }

    // Settings-reset recovery: the server is already Ready, but the model path
    // was wiped from the settings while it kept running. The running server is
    // the source of truth — the resolve must go through (the /v1/models check
    // with the first-model fallback validates it), not fail with
    // "Managed server is not configured".
    void readyServerResolvesAfterModelSettingWiped()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        int first = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++first; });
        QTRY_VERIFY_WITH_TIMEOUT(first == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        // Simulate the settings reset: the model path (and with it configValid)
        // is gone while the server keeps running.
        store.setLaunchModelPath(QString());
        QVERIFY(!runtime.configValid());
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });

        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QVERIFY(!resolved.baseUrl.isEmpty());
        // The model id comes from the running server (mock advertises the
        // default alias), not from the wiped settings.
        QCOMPARE(resolved.modelId, QStringLiteral("llocr-local"));

        runtime.stopServer();
    }

    // A Managed start without a selected model is refused with an actionable
    // message instead of producing a Ready server that can never resolve.
    void managedStartRefusedWithoutModel()
    {
        QTemporaryDir dir;
        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        // launchModelPath intentionally left empty.
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);
        QVERIFY(!runtime.configValid());

        const QString err = runtime.startServer();
        QVERIFY2(!err.isEmpty(), "start must be refused without a model");
        QVERIFY2(err.contains(QStringLiteral("model"), Qt::CaseInsensitive), qPrintable(err));
        // Nothing was spawned.
        QVERIFY(runtime.state() != RuntimeState::Starting);
        QVERIFY(runtime.state() != RuntimeState::Ready);

        // The recognition gate reports the same actionable reason when a start
        // would be needed (the server is not live).
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY(resolved.error.contains(QStringLiteral("model"), Qt::CaseInsensitive));
    }

    // A stale model path (recorded but no longer on disk) must name the path
    // in both the start refusal and the recognition error.
    void missingModelPathNamedInErrors()
    {
        QTemporaryDir dir;
        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("gone.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);
        QVERIFY(!runtime.configValid());

        const QString err = runtime.startServer();
        QVERIFY2(err.contains(QStringLiteral("gone.gguf")), qPrintable(err));

        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY2(resolved.error.contains(QStringLiteral("gone.gguf")), qPrintable(resolved.error));
    }

    // The wizard gates used to be `Settings.serverPath.trim().length > 0`, so a
    // path that no longer exists advanced the step and the start then failed with
    // «File not found». The gates are now C++'s and mean "usable" (ADR 113).
    void configGatesRequireTheFilesToExist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString modelPath = dir.filePath(QStringLiteral("m.gguf"));
        QFile model(modelPath);
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(modelPath);
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);
        QVERIFY(runtime.serverPathValid());
        QVERIFY(runtime.modelPathValid());
        QVERIFY(runtime.configValid());

        // Set but missing: not a valid configuration, however non-empty the string.
        const QString absent = dir.filePath(QStringLiteral("not-here.gguf"));
        store.setLaunchModelPath(absent);
        QVERIFY(!store.launchModelPath().isEmpty());
        QVERIFY(!runtime.modelPathValid());
        QVERIFY(!runtime.configValid());
        // The binary gate is independent — the model step asks about it alone.
        QVERIFY(runtime.serverPathValid());

        store.setLaunchModelPath(modelPath);
        QVERIFY(runtime.configValid());
        store.setServerPath(absent);
        QVERIFY(!runtime.serverPathValid());
        QVERIFY(!runtime.configValid());
    }

    // The restart banner used to be raised by a hand-written list of eleven
    // Settings signals in QML. It is now the comparison of the configuration the
    // live server was started with against the one the settings produce — so a
    // setting nobody remembered to list still raises it, and an unrelated one
    // does not (ADR 113).
    void launchConfigDirtyFollowsTheRealLaunchConfiguration()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("m.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("m.gguf")));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalogWithParameter(dir));
        RuntimeController runtime(store, launchProfiles);
        // Nothing is running, so there is nothing to restart.
        QVERIFY(!runtime.launchConfigDirty());

        int finished = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++finished; });
        QTRY_VERIFY_WITH_TIMEOUT(finished == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        // Just started with these settings — nothing to restart yet.
        QVERIFY(!runtime.launchConfigDirty());

        // A setting the launch configuration does not read must stay quiet: the
        // old signal list would have raised a banner for a window-geometry change.
        store.setWindowWidth(store.windowWidth() + 40);
        QVERIFY(!runtime.launchConfigDirty());

        // A launch parameter the old list did not cover: a saved user profile.
        launchProfiles.setActiveProfileNumber(QStringLiteral("ctx-size"), 4096);
        QVERIFY(runtime.launchConfigDirty());

        runtime.stopServer();
        QTRY_COMPARE(runtime.state(), RuntimeState::Stopped);
        // A stopped server has no stale configuration to restart.
        QVERIFY(!runtime.launchConfigDirty());

        // …and a fresh start adopts the current settings, clearing it again.
        int restarted = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++restarted; });
        QTRY_VERIFY_WITH_TIMEOUT(restarted == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY(!runtime.launchConfigDirty());
        runtime.stopServer();
    }
};

QTEST_MAIN(TestEnsureConnection)
#include "test_ensure_connection.moc"