#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "app/LaunchProfileStore.h"
#include "app/RequestProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeState.h"
#include "runtime/SelfTestController.h"
#include "testsettings.h"

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

}  // namespace

// Stage G-core acceptance: ensureConnectionReady() for External and Managed
// (start → /health → /v1/models → alias), deduplication of concurrent calls,
// cancelPendingStart(), and the health-timeout path. Runs against the mock
// llama-server (tests/mock_llama_server.cpp).
class TestEnsureConnection : public QObject {
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

    void cleanup()
    {
        QSettings().clear();
    }

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
        runtime.ensureConnectionReady(ConnectionRole::Ocr,
                                      [&](const ResolvedConnection &c) { ocr = c; });
        runtime.ensureConnectionReady(ConnectionRole::Check,
                                      [&](const ResolvedConnection &c) { check = c; });
        QCOMPARE(ocr.modelId, QStringLiteral("ocr-model"));
        QCOMPARE(check.modelId, QStringLiteral("check-model"));
        // Same endpoint for both roles; only the model id differs.
        QCOMPARE(check.baseUrl, ocr.baseUrl);
        QCOMPARE(check.apiKey, ocr.apiKey);
        QCOMPARE(check.timeoutMs, ocr.timeoutMs);

        // An empty check/modelName is sent as-is: single-model servers (e.g.
        // llama-server) ignore the model field entirely, and a multi-model
        // OpenAI-compatible server returns its own "model not found" error,
        // which is surfaced verbatim (§7.5). No silent fallback to the OCR name.
        settings.setCheckModelName(QString());
        ResolvedConnection empty;
        runtime.ensureConnectionReady(ConnectionRole::Check,
                                      [&](const ResolvedConnection &c) { empty = c; });
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
        LaunchProfileStore checkProfiles(store, writeTestLaunchCatalog(dir),
                                         LaunchProfileStore::Role::Check);
        RuntimeController runtime(store, launchProfiles, &checkProfiles);

        // Start with the OCR model.
        int first = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++first; });
        QTRY_VERIFY_WITH_TIMEOUT(first == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        QList<int> states;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() {
            states.append(int(runtime.state()));
        });

        // The check resolve must restart the server for the check model.
        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::Check,
                                      [&](const ResolvedConnection &c) { resolved = c; ++done; });
        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QVERIFY(!resolved.baseUrl.isEmpty());
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(states.contains(int(RuntimeState::Stopped)),
                 "expected the server to be stopped for the role switch");

        // Back to recognition: switches to the OCR model again.
        states.clear();
        int done2 = 0;
        ResolvedConnection resolved2;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) { resolved2 = c; ++done2; });
        QTRY_VERIFY_WITH_TIMEOUT(done2 == 1, 15000);
        QVERIFY2(resolved2.error.isEmpty(), qPrintable(resolved2.error));
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(states.contains(int(RuntimeState::Stopped)),
                 "expected a restart switching back to the OCR model");

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
        LaunchProfileStore checkProfiles(store, writeTestLaunchCatalog(dir),
                                         LaunchProfileStore::Role::Check);
        RuntimeController runtime(store, launchProfiles, &checkProfiles);

        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::Check,
                                      [&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY2(resolved.error.contains(QStringLiteral("Check model"), Qt::CaseInsensitive),
                 qPrintable(resolved.error));
        QVERIFY(resolved.baseUrl.isEmpty());
        QVERIFY(runtime.state() != RuntimeState::Starting);
        QVERIFY(runtime.state() != RuntimeState::Ready);

        // A stale check-model path is named in the error.
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("gone.gguf")));
        ResolvedConnection resolved2;
        runtime.ensureConnectionReady(ConnectionRole::Check,
                                      [&](const ResolvedConnection &c) { resolved2 = c; });
        QVERIFY2(resolved2.error.contains(QStringLiteral("gone.gguf")),
                 qPrintable(resolved2.error));
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
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("delay-start"),
                                                    QStringLiteral("4000")));
        launchProfiles.saveDraft();
        store.setStartupTimeoutMs(30000);

        RuntimeController runtime(store, launchProfiles);

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });

        // Wait until the process is starting, then interrupt.
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
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("never-healthy"),
                                                    QString()));
        QVERIFY(launchProfiles.appendDraftParameter(QStringLiteral("no-models"),
                                                    QString()));
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
        const QString defaultsPath =
            dir.filePath(QStringLiteral("request-defaults.json"));
        {
            QFile defaultsFile(defaultsPath);
            QVERIFY(defaultsFile.open(QIODevice::WriteOnly));
            defaultsFile.write(QByteArrayLiteral(
                "{\"schemaVersion\":1,\"parameters\":["
                "{\"order\":1,\"name\":\"temperature\",\"value\":0.0},"
                "{\"order\":2,\"name\":\"max_tokens\",\"value\":1024},"
                "{\"order\":3,\"name\":\"stream\",\"value\":false}]}").constData());
        }
        RequestProfileStore profiles(store, defaultsPath);
        SelfTestController selfTest(store, runtime, profiles);
        SelfTestResult result;
        int done = 0;
        QFutureWatcher<SelfTestResult> watch;
        connect(&watch, &QFutureWatcher<SelfTestResult>::finished, this,
                [&]() { result = watch.result(); ++done; });
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

        // Start once so the server is Ready.
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
        QVERIFY2(err.contains(QStringLiteral("model"), Qt::CaseInsensitive),
                 qPrintable(err));
        // Nothing was spawned.
        QVERIFY(runtime.state() != RuntimeState::Starting);
        QVERIFY(runtime.state() != RuntimeState::Ready);

        // The recognition gate reports the same actionable reason when a start
        // would be needed (the server is not live).
        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
        });
        QVERIFY(resolved.error.contains(QStringLiteral("model"),
                                        Qt::CaseInsensitive));
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
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) {
            resolved = c;
        });
        QVERIFY2(resolved.error.contains(QStringLiteral("gone.gguf")),
                 qPrintable(resolved.error));
    }
};

QTEST_MAIN(TestEnsureConnection)
#include "test_ensure_connection.moc"