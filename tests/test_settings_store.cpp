#include <QtTest>
#include <QCoreApplication>
#include <QHash>
#include <QMetaProperty>
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

    void resetTableEntriesAreValid()
    {
        // Every row in the defaults table (review 3.5) must reference a real,
        // writable Q_PROPERTY, and its default must match what the getter
        // returns on a fresh store — otherwise resetToDefaults() drifts from
        // the property declarations.
        SettingsStore store;
        const QMetaObject *mo = store.metaObject();
        QVERIFY(SettingsStore::defaultsCount() > 0);
        for (int i = 0; i < SettingsStore::defaultsCount(); ++i) {
            const SettingsStore::SettingDefault &entry = SettingsStore::defaults()[i];
            const QMetaProperty prop = mo->property(mo->indexOfProperty(entry.property));
            QVERIFY2(prop.isValid(), entry.property);
            QVERIFY2(prop.isWritable(), entry.property);
            QCOMPARE(store.property(entry.property), entry.defaultValue);
        }
    }

    void resetCoversEveryDeclaredProperty()
    {
        // Drift guard: set every writable property to a sentinel value, reset,
        // and verify none of the sentinels survive. A property that is added
        // but forgotten in kDefaults would stay at its sentinel and fail here.
        SettingsStore store;
        const QMetaObject *mo = store.metaObject();

        // Properties excluded from reset by design:
        //  - windowX/Y/Width/Height/State: UI state, intentionally not reset;
        //  - connectionMode / lastExternalBaseUrl: covered implicitly through
        //    setConnectionMode()'s restore path (baseUrl is reset, but its
        //    final value when coming from Managed is the restored endpoint).
        auto excluded = [](const QByteArray &name) {
            return name.startsWith("window")
                || name == QByteArrayLiteral("connectionMode")
                || name == QByteArrayLiteral("lastExternalBaseUrl");
        };

        QHash<QByteArray, QVariant> sentinels;
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty prop = mo->property(i);
            if (!prop.isWritable() || excluded(prop.name()))
                continue;

            // A sentinel guaranteed to differ from the property's current
            // (default) value.
            QVariant sentinel;
            switch (prop.metaType().id()) {
            case QMetaType::QString:
                sentinel = QVariant(QStringLiteral("\u00A7sentinel\u00A7"));
                break;
            case QMetaType::Int:
                sentinel = QVariant(1000000);
                break;
            case QMetaType::Double:
                sentinel = QVariant(12345.678);
                break;
            case QMetaType::Bool:
                sentinel = QVariant(!store.property(prop.name()).toBool());
                break;
            default:
                QFAIL("unexpected property type in SettingsStore");
            }
            QVERIFY2(prop.write(&store, sentinel), prop.name());
            sentinels.insert(prop.name(), sentinel);
        }
        QVERIFY(sentinels.size() > 0);

        store.resetToDefaults();

        for (auto it = sentinels.cbegin(); it != sentinels.cend(); ++it) {
            QVERIFY2(store.property(it.key()) != it.value(), it.key());
        }
    }

    void resetEmitsChangedSignals()
    {
        // QML binds to the per-property NOTIFY signals; after a reset every
        // changed setting must announce itself (review 3.5 keeps this via the
        // setters, which emit on change).
        SettingsStore store;
        QSignalSpy languageSpy(&store, &SettingsStore::languageChanged);
        QSignalSpy baseUrlSpy(&store, &SettingsStore::baseUrlChanged);
        QSignalSpy autoStartSpy(&store, &SettingsStore::autoStartChanged);
        QSignalSpy launchHostSpy(&store, &SettingsStore::launchHostChanged);

        store.setLanguage(QStringLiteral("ru"));
        store.setBaseUrl(QStringLiteral("http://custom:1"));
        store.setAutoStart(true);
        store.setLaunchHost(QStringLiteral("10.0.0.1"));

        QCOMPARE(languageSpy.count(), 1);
        QCOMPARE(baseUrlSpy.count(), 1);
        QCOMPARE(autoStartSpy.count(), 1);
        QCOMPARE(launchHostSpy.count(), 1);

        store.resetToDefaults();

        QCOMPARE(languageSpy.count(), 2);   // set + reset
        QCOMPARE(baseUrlSpy.count(), 2);
        QCOMPARE(autoStartSpy.count(), 2);
        QCOMPARE(launchHostSpy.count(), 2);
    }
};

QTEST_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
