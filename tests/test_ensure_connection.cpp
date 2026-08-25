#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "app/SettingsStore.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeState.h"

using namespace llocr;

#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

// Stage G-core acceptance: ensureConnectionReady() for External and Managed
// (start → /health → /v1/models → alias), deduplication of concurrent calls,
// cancelPendingStart(), and the health-timeout path. Runs against the mock
// llama-server (tests/mock_llama_server.cpp).
class TestEnsureConnection : public QObject {
    Q_OBJECT

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
        SettingsStore settings;
        settings.setConnectionMode(QStringLiteral("external"));
        settings.setBaseUrl(QStringLiteral("http://custom.example:9000"));
        settings.setApiKey(QStringLiteral("k"));
        settings.setModelName(QStringLiteral("my-model"));
        settings.setConnectionTimeoutMs(5000);

        RuntimeController runtime(settings);
        QFuture<ResolvedConnection> future = runtime.ensureConnectionReady();
        future.waitForFinished();
        QVERIFY(!future.isCanceled());
        const ResolvedConnection conn = future.result();
        QCOMPARE(conn.baseUrl, QStringLiteral("http://custom.example:9000"));
        QCOMPARE(conn.apiKey, QStringLiteral("k"));
        QCOMPARE(conn.modelId, QStringLiteral("my-model"));
        QCOMPARE(conn.timeoutMs, 5000);
        QVERIFY(conn.error.isEmpty());
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

        RuntimeController runtime(store);
        QVERIFY(runtime.configValid());

        int finished = 0;
        ResolvedConnection resolved;
        QFutureWatcher<ResolvedConnection> watch;
        connect(&watch, &QFutureWatcher<ResolvedConnection>::finished, this,
                [&]() { resolved = watch.result(); ++finished; });
        watch.setFuture(runtime.ensureConnectionReady());

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

        RuntimeController runtime(store);

        // Two "concurrent" callers issued before the first one completes. Only a
        // single start may happen; both must end up with the same connection.
        const QFuture<ResolvedConnection> a = runtime.ensureConnectionReady();
        const QFuture<ResolvedConnection> b = runtime.ensureConnectionReady();

        int done = 0;
        ResolvedConnection ra, rb;
        auto *wa = new QFutureWatcher<ResolvedConnection>(this);
        connect(wa, &QFutureWatcher<ResolvedConnection>::finished, this,
                [&]() { ra = wa->result(); wa->deleteLater(); ++done; });
        wa->setFuture(a);
        auto *wb = new QFutureWatcher<ResolvedConnection>(this);
        connect(wb, &QFutureWatcher<ResolvedConnection>::finished, this,
                [&]() { rb = wb->result(); wb->deleteLater(); ++done; });
        wb->setFuture(b);

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
        // Delay the socket bind so the resolve stays in Starting.
        store.setLaunchExtraArgs(QStringLiteral("--delay-start 4000"));
        store.setStartupTimeoutMs(30000);

        RuntimeController runtime(store);

        int done = 0;
        ResolvedConnection resolved;
        QFutureWatcher<ResolvedConnection> watch;
        connect(&watch, &QFutureWatcher<ResolvedConnection>::finished, this,
                [&]() { resolved = watch.result(); ++done; });
        watch.setFuture(runtime.ensureConnectionReady());

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
        store.setStartupTimeoutMs(1200);
        store.setLaunchExtraArgs(QStringLiteral("--never-healthy"));

        RuntimeController runtime(store);
        ResolvedConnection resolved;
        int done = 0;
        QFutureWatcher<ResolvedConnection> watch;
        connect(&watch, &QFutureWatcher<ResolvedConnection>::finished, this,
                [&]() { resolved = watch.result(); ++done; });
        watch.setFuture(runtime.ensureConnectionReady());

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

        RuntimeController runtime(store);
        SelfTestResult result;
        int done = 0;
        QFutureWatcher<SelfTestResult> watch;
        connect(&watch, &QFutureWatcher<SelfTestResult>::finished, this,
                [&]() { result = watch.result(); ++done; });
        watch.setFuture(runtime.runSelfTest());

        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.text, QStringLiteral("SELFTEST_OK"));

        runtime.stopServer();
    }
};

QTEST_MAIN(TestEnsureConnection)
#include "test_ensure_connection.moc"