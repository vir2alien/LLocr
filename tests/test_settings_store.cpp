#include <QtTest>
#include <QCoreApplication>
#include <QSettings>

#include "app/SettingsStore.h"

using namespace llocr;

class TestSettingsStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_settings"));
    }

    void cleanup()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void defaultValues()
    {
        SettingsStore store;
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QStringLiteral(""));
        QCOMPARE(store.connectionTimeoutMs(), 120000);
        QCOMPARE(store.modelName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(store.temperature(), 0.0);
        QCOMPARE(store.maxTokens(), SettingsStore::kDefaultMaxTokens);
        QCOMPARE(store.dryMultiplier(), 0.8);
        QCOMPARE(store.dryBase(), 1.75);
        QCOMPARE(store.dryAllowedLength(), 35);
        QCOMPARE(store.dryPenaltyLastN(), SettingsStore::kDefaultDryPenaltyLastN);
        QCOMPARE(store.parserId(), QStringLiteral("det_tokens"));
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
    }

    void resetToDefaults()
    {
        SettingsStore store;
        // Modify values
        store.setBaseUrl(QStringLiteral("http://custom:1234"));
        store.setApiKey(QStringLiteral("secret-token"));
        store.setConnectionTimeoutMs(5000);
        store.setModelName(QStringLiteral("custom-model"));
        store.setTemperature(0.7);
        store.setMaxTokens(4096);
        store.setDryMultiplier(0.5);
        store.setDryBase(1.5);
        store.setDryAllowedLength(20);
        store.setDryPenaltyLastN(64);
        store.setParserId(QStringLiteral("raw"));
        store.setThemeMode(1);
        store.setLanguage(QStringLiteral("ru"));

        QCOMPARE(store.baseUrl(), QStringLiteral("http://custom:1234"));
        QCOMPARE(store.apiKey(), QStringLiteral("secret-token"));
        QCOMPARE(store.connectionTimeoutMs(), 5000);
        QCOMPARE(store.modelName(), QStringLiteral("custom-model"));
        QCOMPARE(store.temperature(), 0.7);
        QCOMPARE(store.maxTokens(), 4096);
        QCOMPARE(store.dryMultiplier(), 0.5);
        QCOMPARE(store.dryBase(), 1.5);
        QCOMPARE(store.dryAllowedLength(), 20);
        QCOMPARE(store.dryPenaltyLastN(), 64);
        QCOMPARE(store.parserId(), QStringLiteral("raw"));
        QCOMPARE(store.themeMode(), 1);
        QCOMPARE(store.language(), QStringLiteral("ru"));

        // Reset
        store.resetToDefaults();

        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
        QCOMPARE(store.apiKey(), QStringLiteral(""));
        QCOMPARE(store.connectionTimeoutMs(), 120000);
        QCOMPARE(store.modelName(), QStringLiteral("Unlimited-OCR"));
        QCOMPARE(store.temperature(), 0.0);
        QCOMPARE(store.maxTokens(), SettingsStore::kDefaultMaxTokens);
        QCOMPARE(store.dryMultiplier(), 0.8);
        QCOMPARE(store.dryBase(), 1.75);
        QCOMPARE(store.dryAllowedLength(), 35);
        QCOMPARE(store.dryPenaltyLastN(), SettingsStore::kDefaultDryPenaltyLastN);
        QCOMPARE(store.parserId(), QStringLiteral("det_tokens"));
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
    }

    void newRuntimeDefaults()
    {
        SettingsStore store;
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.setupVersion(), 0);
        QCOMPARE(store.setupDismissed(), false);
        QCOMPARE(store.serverPath(), QStringLiteral(""));
        QCOMPARE(store.serverPathIsManaged(), false);
        QCOMPARE(store.autoStart(), false);       // §4.3: off by default
        QCOMPARE(store.startOnDemand(), true);
        QCOMPARE(store.stopOnExit(), true);
        QCOMPARE(store.autoRestart(), true);
        QCOMPARE(store.startupTimeoutMs(), 180000);
        QCOMPARE(store.checkUpdates(), false);
        QCOMPARE(store.allowNonLoopback(), false);

        QCOMPARE(store.launchModelAlias(), QStringLiteral("llocr-local"));
        QCOMPARE(store.launchHost(), QStringLiteral("127.0.0.1"));
        QCOMPARE(store.launchPort(), 0);
        QCOMPARE(store.launchCtxSize(), 8192);
        QCOMPARE(store.launchGpuLayers(), -1);
        QCOMPARE(store.launchThreads(), 0);
        QCOMPARE(store.launchBatchSize(), 0);
        QCOMPARE(store.launchParallel(), 1);
        QCOMPARE(store.launchFlashAttn(), QStringLiteral("off"));
        QCOMPARE(store.launchNoMmap(), false);
        QCOMPARE(store.launchJinja(), false);
        QCOMPARE(store.hfToken(), QStringLiteral(""));
    }

    void migrationConfiguredProfileKeepsExternal()
    {
        // A pre-existing profile with a configured baseUrl must be treated as
        // already set up: no first-run wizard, and the mode stays External.
        QSettings pre;
        pre.setValue(QStringLiteral("provider/baseUrl"),
                     QStringLiteral("http://localhost:8080"));
        pre.setValue(QStringLiteral("provider/apiKey"), QStringLiteral("k"));
        pre.sync();

        SettingsStore store;
        QCOMPARE(store.setupVersion(), SettingsStore::kCurrentSetupVersion);
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
        QCOMPARE(store.baseUrl(), QStringLiteral("http://localhost:8080"));
    }

    void migrationCleanProfileGivesZero()
    {
        // A fresh profile (no provider/baseUrl) must yield setupVersion == 0
        // so the first-run wizard shows up.
        SettingsStore store;
        QCOMPARE(store.setupVersion(), 0);
        QCOMPARE(store.connectionMode(), QStringLiteral("external"));
    }

    void migrationRunsOnlyOnce()
    {
        // After the first construction setupVersion exists, so re-construction
        // must not touch anything (e.g. must not flip an explicit Managed mode
        // back to External).
        {
            SettingsStore store;
            store.setSetupVersion(1);
            store.setConnectionMode(QStringLiteral("managed"));
        }
        {
            SettingsStore store;
            QCOMPARE(store.setupVersion(), 1);
            QCOMPARE(store.connectionMode(), QStringLiteral("managed"));
        }
    }
};

QTEST_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
