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

#include "app/CheckController.h"
#include "config/RequestProfileStore.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "runtime/InstalledState.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeState.h"
#include "runtime/SelfTestController.h"
#include "runtime/ServerCapabilities.h"
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
// argv; a test that needs extra server flags declares them as the preset's
// own platform rows — the only place flags survive the startup pruning now.
QString writeTestLaunchCatalog(const QTemporaryDir &dir, const QByteArray &parametersJson = QByteArrayLiteral("[]"))
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("launch-presets.json"));
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArrayLiteral("{ \"schemaVersion\": 1, \"profiles\": ["
                                  "{ \"id\": \"test\", \"parameters\": ") +
                parametersJson + QByteArrayLiteral(" }] }"));
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
        settings.setDecisionModelName(QStringLiteral("decision-model"));

        RuntimeController runtime(settings, launchProfiles);
        ResolvedConnection ocr, check, decision;
        runtime.ensureConnectionReady(ConnectionRole::Ocr, [&](const ResolvedConnection &c) { ocr = c; });
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) { check = c; });
        runtime.ensureConnectionReady(ConnectionRole::Decision, [&](const ResolvedConnection &c) { decision = c; });
        QCOMPARE(ocr.modelId, QStringLiteral("ocr-model"));
        QCOMPARE(check.modelId, QStringLiteral("check-model"));
        QCOMPARE(decision.modelId, QStringLiteral("decision-model"));
        QCOMPARE(decision.baseUrl, ocr.baseUrl);
        QCOMPARE(decision.apiKey, ocr.apiKey);
        QCOMPARE(decision.timeoutMs, ocr.timeoutMs);

        // An empty check/modelName is sent as-is: single-model servers (e.g.
        // llama-server) ignore the model field entirely, and a multi-model
        // OpenAI-compatible server returns its own "model not found" error,
        // which is surfaced verbatim (§7.5). No silent fallback to the OCR name.
        settings.setCheckModelName(QString());
        ResolvedConnection empty;
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) { empty = c; });
        QVERIFY(empty.modelId.isEmpty());
    }

    // The decision model has its own weights, so a resolve for the decision
    // role stops the server the OCR role started and restarts it with the
    // decision model (ADR 74, per-role config).
    void managedDecisionRoleSwitchesServer()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile ocrModel(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        QVERIFY(ocrModel.open(QIODevice::WriteOnly));
        ocrModel.write("ocr");
        ocrModel.close();
        QFile decisionModel(dir.filePath(QStringLiteral("decision-Q8_0.gguf")));
        QVERIFY(decisionModel.open(QIODevice::WriteOnly));
        decisionModel.write("decision");
        decisionModel.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("ocr-Q4_K_M.gguf")));
        store.setDecisionLaunchModelPath(dir.filePath(QStringLiteral("decision-Q8_0.gguf")));
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
        runtime.ensureConnectionReady(ConnectionRole::Decision, [&](const ResolvedConnection &c) {
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
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) {
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

    // One model profile serving both roles, with the launch layer the profile
    // gives each role: the same one twice, or two different ones.
    ModelProfiles::Profile twoRoleProfile(const QString &id, int ocrCtxSize, int checkCtxSize)
    {
        ModelProfiles::Profile profile;
        profile.id = id;
        profile.title = id;
        for (const QString &roleName : {QStringLiteral("ocr"), QStringLiteral("blockRecognition")}) {
            ModelProfiles::Role role;
            role.alias = QStringLiteral("llocr-%1-%2").arg(id, roleName);
            LaunchParameter ctx;
            ctx.name = QStringLiteral("ctx-size");
            ctx.kind = LaunchValueKind::Number;
            ctx.value = roleName == QLatin1String("ocr") ? ocrCtxSize : checkCtxSize;
            ctx.order = 1;
            role.launch.append(ctx);
            profile.roles.insert(roleName, role);
        }
        return profile;
    }

    // The other half of the switch rule: one model for both roles does not mean
    // two loads. The same weights are already in memory and the request
    // parameters travel in the body, so a check request on a running OCR server
    // of the same model has to be answered by that server — not by a reload the
    // user would watch as a multi-second stall between recognising and verifying.
    void sameModelForBothRolesKeepsTheRunningServer()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("shared");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        store.setModelRecipeId(QStringLiteral("shared"));
        store.setCheckRequestProfileId(QStringLiteral("shared"));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        // Same launch layer in both roles — the shape a model used for both
        // tasks is expected to ship (teleocr).
        launchProfiles.setModelProfiles({twoRoleProfile(QStringLiteral("shared"), 16384, 16384)});
        RuntimeController runtime(store, launchProfiles);

        int first = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++first; });
        QTRY_VERIFY_WITH_TIMEOUT(first == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        QList<int> states;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() { states.append(int(runtime.state())); });

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QVERIFY(!resolved.baseUrl.isEmpty());
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(!states.contains(int(RuntimeState::Stopped)), "the running server was stopped to serve a check request on the same model");
        QVERIFY2(!states.contains(int(RuntimeState::Starting)), "the model was loaded again for the check role");

        runtime.stopServer();
    }

    // …and the other side of the coin: the same weights with a different launch
    // layer per role. Those flags are startup flags, so the server has to be
    // restarted — the alternative is silently serving the check role with the
    // OCR role's context window and KV type.
    void sameWeightsWithDifferentLaunchFlagsRestartTheServer()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("shared");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("shared-Q4_K_M.gguf")));
        store.setModelRecipeId(QStringLiteral("shared"));
        store.setCheckRequestProfileId(QStringLiteral("shared"));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        // One model, two roles, two different context windows.
        launchProfiles.setModelProfiles({twoRoleProfile(QStringLiteral("shared"), 32768, 16384)});
        RuntimeController runtime(store, launchProfiles);

        int first = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++first; });
        QTRY_VERIFY_WITH_TIMEOUT(first == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);

        QList<int> states;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() { states.append(int(runtime.state())); });

        int done = 0;
        ResolvedConnection resolved;
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) {
            resolved = c;
            ++done;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done == 1, 15000);
        QVERIFY2(resolved.error.isEmpty(), qPrintable(resolved.error));
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY2(states.contains(int(RuntimeState::Stopped)), "the check role kept the OCR role's launch flags instead of restarting the server");

        runtime.stopServer();
    }

    // The model layer is part of the launch configuration, so switching the OCR
    // model changes what the server should be started with even though the
    // Launch tab (the platform layer alone) looks untouched: the running server
    // must be flagged for a restart, and switching back must clear it.
    void switchingTheOcrModelRaisesTheRestartBanner()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("a-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("a");
        model.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(dir.filePath(QStringLiteral("a-Q4_K_M.gguf")));
        store.setModelRecipeId(QStringLiteral("a"));
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        store.setStartupTimeoutMs(10000);

        // Model "a" carries a row of its own (the Unlimited-OCR shape); model
        // "b" does not (the LFM2.5-VL shape). The launch path deliberately
        // stays on the same file: the model layer is what varies here.
        ModelProfiles::Profile a;
        a.id = QStringLiteral("a");
        a.title = QStringLiteral("A");
        ModelProfiles::Role aRole;
        aRole.alias = QStringLiteral("llocr-a");
        LaunchParameter ctx;
        ctx.name = QStringLiteral("ctx-size");
        ctx.kind = LaunchValueKind::Number;
        ctx.value = 16384;
        ctx.order = 1;
        aRole.launch.append(ctx);
        LaunchParameter vision;
        vision.name = QStringLiteral("image-min-tokens");
        vision.kind = LaunchValueKind::Number;
        vision.value = 456;
        vision.order = 2;
        aRole.launch.append(vision);
        a.roles.insert(QStringLiteral("ocr"), aRole);

        ModelProfiles::Profile b;
        b.id = QStringLiteral("b");
        b.title = QStringLiteral("B");
        ModelProfiles::Role bRole;
        bRole.alias = QStringLiteral("llocr-b");
        bRole.launch.append(ctx);
        b.roles.insert(QStringLiteral("ocr"), bRole);

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        launchProfiles.setModelProfiles({a, b});
        QVERIFY(launchProfiles.activeProfile(QStringLiteral("a"), QStringLiteral("ocr")).find(QStringLiteral("image-min-tokens")) != nullptr);
        QVERIFY(launchProfiles.activeProfile(QStringLiteral("b"), QStringLiteral("ocr")).find(QStringLiteral("image-min-tokens")) == nullptr);

        RuntimeController runtime(store, launchProfiles);
        int finished = 0;
        runtime.ensureConnectionReady([&](const ResolvedConnection &) { ++finished; });
        QTRY_VERIFY_WITH_TIMEOUT(finished == 1, 15000);
        QCOMPARE(runtime.state(), RuntimeState::Ready);
        QVERIFY(!runtime.launchConfigDirty());

        // Switching to a model whose launch layer differs flags the running
        // server for a restart — the Launch tab did not change, but what the
        // server should be started with did.
        store.setModelRecipeId(QStringLiteral("b"));
        QVERIFY(runtime.launchConfigDirty());

        // …and switching back resolves it without a restart.
        store.setModelRecipeId(QStringLiteral("a"));
        QVERIFY(!runtime.launchConfigDirty());

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
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) {
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
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY2(resolved.error.contains(QStringLiteral("Block OCR model"), Qt::CaseInsensitive), qPrintable(resolved.error));
        QVERIFY(resolved.baseUrl.isEmpty());
        QVERIFY(runtime.state() != RuntimeState::Starting);
        QVERIFY(runtime.state() != RuntimeState::Ready);

        // A stale check-model path is named in the error.
        store.setCheckLaunchModelPath(dir.filePath(QStringLiteral("gone.gguf")));
        ResolvedConnection resolved2;
        runtime.ensureConnectionReady(ConnectionRole::BlockRecognition, [&](const ResolvedConnection &c) { resolved2 = c; });
        QVERIFY2(resolved2.error.contains(QStringLiteral("gone.gguf")), qPrintable(resolved2.error));

        // The decision role refuses the same way, naming its own window.
        ResolvedConnection resolved3;
        runtime.ensureConnectionReady(ConnectionRole::Decision, [&](const ResolvedConnection &c) { resolved3 = c; });
        QVERIFY2(resolved3.error.contains(QStringLiteral("Decision model"), Qt::CaseInsensitive), qPrintable(resolved3.error));
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
        // Delay the socket bind so the resolve stays in Starting. The delay is
        // a platform row of the test preset.
        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir, QByteArrayLiteral("[ { \"order\": 1, \"name\": \"delay-start\", \"value\": 4000 } ]")));
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

    // Stopping a check stops the check, not the server it borrowed. The managed
    // runtime is app-wide state: a start or role switch the check triggered keeps
    // running after the job is abandoned (ADR 133), so the check only reports the
    // stop once the resolve lands.
    void stoppingACheckLeavesTheManagedServerRunning()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("ocr.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("ocr");
        model.close();
        QFile checkModel(dir.filePath(QStringLiteral("check.gguf")));
        QVERIFY(checkModel.open(QIODevice::WriteOnly));
        checkModel.write("check");
        checkModel.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(QString::fromUtf8(LLOCR_MOCK_SERVER));
        store.setLaunchModelPath(model.fileName());
        store.setCheckLaunchModelPath(checkModel.fileName());
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        store.setStartOnDemand(true);
        // The socket bind is delayed, so the resolve the check asks for is still
        // in Starting when the job is stopped — the window the cancel used to hit.
        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir, QByteArrayLiteral("[ { \"order\": 1, \"name\": \"delay-start\", \"value\": 2000 } ]")));
        store.setStartupTimeoutMs(30000);

        RuntimeController runtime(store, launchProfiles);
        RequestProfileStore checkProfiles(store);
        CheckController check(checkProfiles, runtime);
        QSignalSpy finishedSpy(&check, &CheckController::checkFinished);

        QList<int> states;
        connect(&runtime, &RuntimeController::stateChanged, &runtime, [&]() { states.append(int(runtime.state())); });

        QImage block(24, 16, QImage::Format_RGB32);
        block.fill(Qt::white);
        check.checkBlock(block, QString(), QString());
        QTRY_VERIFY_WITH_TIMEOUT(runtime.state() == RuntimeState::Starting, 5000);

        check.stop();

        QTRY_VERIFY_WITH_TIMEOUT(!check.busy(), 20000);
        QVERIFY2(finishedSpy.isEmpty(), "a stopped check must not report a result for the abandoned block");
        QTRY_COMPARE_WITH_TIMEOUT(runtime.state(), RuntimeState::Ready, 20000);
        QVERIFY2(!states.contains(int(RuntimeState::Stopped)), "stopping the check must not stop the managed server");

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
        // health watchdog truly hits the startup timeout. Both flags are
        // platform rows of the test preset.
        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir, QByteArrayLiteral("[ { \"order\": 1, \"name\": \"never-healthy\" }, { \"order\": 2, \"name\": \"no-models\" } ]")));

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

        // Request-profile store for the self-test request; test binaries embed
        // no resources, so the profile is injected.
        ModelProfiles::Profile selfTestProfile;
        selfTestProfile.id = QStringLiteral("unlimited-ocr");
        ModelProfiles::Role selfTestRole;
        selfTestRole.request = {RequestParameter{QStringLiteral("temperature"), 1, RequestValueKind::Number, 0.0, QString()},
                                RequestParameter{QStringLiteral("max_tokens"), 2, RequestValueKind::Number, 1024.0, QString()},
                                RequestParameter{QStringLiteral("stream"), 3, RequestValueKind::Boolean, false, QString()}};
        selfTestProfile.roles.insert(QStringLiteral("ocr"), selfTestRole);
        RequestProfileStore profiles(store);
        profiles.setModelProfiles({selfTestProfile});
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

    // A build tag is a number, not text: «b11312» sorts before «b4000», so the
    // profile's minBuild used to refuse every five-digit build and sent the user
    // to Settings → Runtime for a runtime that was already new enough.
    void minBuildIsComparedByNumber()
    {
        QTemporaryDir dir;
        const QString binary = dir.filePath(QStringLiteral("llama-server"));
        QFile binaryFile(binary);
        QVERIFY(binaryFile.open(QIODevice::WriteOnly));
        binaryFile.close();

        // The probe answers from this cache instead of running the binary, so the
        // build is the one this test names.
        const QString cacheDir = dir.filePath(QStringLiteral("runtime/cache"));
        QVERIFY(QDir().mkpath(cacheDir));
        QFile probeFile(ServerCapabilities::cacheFileName(cacheDir, binary));
        QVERIFY(probeFile.open(QIODevice::WriteOnly));
        ServerCapabilities caps;
        caps.ok = true;
        caps.build = QStringLiteral("b11312");
        probeFile.write(QJsonDocument(caps.toJson()).toJson(QJsonDocument::Compact));
        probeFile.close();

        const QString modelPath = dir.filePath(QStringLiteral("model.gguf"));
        QFile modelFile(modelPath);
        QVERIFY(modelFile.open(QIODevice::WriteOnly));
        modelFile.close();

        SettingsStore store;
        store.setConnectionMode(QStringLiteral("managed"));
        store.setServerPath(binary);
        store.setLaunchModelPath(modelPath);
        store.setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        store.setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        // Neither switch: the resolve stops at «not set to start automatically»
        // instead of spawning anything, so the assertion is about the refusal.
        store.setAutoStart(false);
        store.setStartOnDemand(false);
        // A profile the catalog knows, whose minBuild is far below the build.
        store.setModelRecipeId(QStringLiteral("unlimited-ocr"));

        LaunchProfileStore launchProfiles(store, writeTestLaunchCatalog(dir));
        RuntimeController runtime(store, launchProfiles);

        ResolvedConnection resolved;
        runtime.ensureConnectionReady([&](const ResolvedConnection &c) { resolved = c; });
        QVERIFY2(!resolved.error.isEmpty(), "the resolve must refuse rather than start");
        QVERIFY2(!resolved.error.contains(QStringLiteral("needs llama.cpp")), qPrintable(resolved.error));
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

        // A launch parameter the old list did not cover: a saved user profile —
        // the exact path the Launch tab takes (edit the draft, save it).
        const QAbstractListModel *draft = launchProfiles.draftModel();
        QVERIFY(draft->rowCount() > 0);
        QVERIFY(launchProfiles.setDraftValue(0, QStringLiteral("4096")));
        launchProfiles.saveDraft();
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