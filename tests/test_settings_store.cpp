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
        QCOMPARE(store.maxTokens(), 16384);
        QCOMPARE(store.dryMultiplier(), 0.8);
        QCOMPARE(store.dryBase(), 1.75);
        QCOMPARE(store.dryAllowedLength(), 35);
        QCOMPARE(store.dryPenaltyLastN(), 128);
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
        QCOMPARE(store.maxTokens(), 16384);
        QCOMPARE(store.dryMultiplier(), 0.8);
        QCOMPARE(store.dryBase(), 1.75);
        QCOMPARE(store.dryAllowedLength(), 35);
        QCOMPARE(store.dryPenaltyLastN(), 128);
        QCOMPARE(store.parserId(), QStringLiteral("det_tokens"));
        QCOMPARE(store.themeMode(), 0);
        QCOMPARE(store.language(), QStringLiteral("system"));
    }
};

QTEST_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
