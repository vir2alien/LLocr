#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// Two presets: metal-tagged and cpu-tagged, for the auto-switch tests.
constexpr const char *kPresetsJson = R"({
    "schemaVersion": 1,
    "profiles": [
        { "id": "metal", "name": "Metal", "os": "macos", "backend": "metal",
          "parameters": [
              { "order": 1, "name": "ctx-size", "value": 16384 },
              { "order": 2, "name": "no-warmup" }
          ] },
        { "id": "cpu", "name": "CPU", "backend": "cpu",
          "parameters": [
              { "order": 1, "name": "ctx-size", "value": 8192 }
          ] }
    ]
})";

constexpr const char *kEmptyCatalog = R"({ "schemaVersion": 1, "profiles": [] })";

QString writeProfileFile(const QTemporaryDir &dir, const QString &name,
                         const QByteArray &json)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(json);
    return path;
}

}  // namespace

class TestLaunchProfile : public QObject {
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_launch_profile"));
    }

    void cleanup()
    {
        // QSettings (the isolated INI) persists across test slots in one
        // process; launch profile ids and runtime dirs must not leak between
        // the slots.
        QSettings().clear();
    }

    void storeResolvesAndAutoSwitches()
    {
        QTemporaryDir dir;
        const QString presetsPath =
            writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        // No runtime installed: the platform-recommended backend resolves
        // (cpu on Windows/Linux, metal on macOS — both presets exist).
        LaunchProfileStore store(settings, presetsPath);
        const QString initialId = store.activeProfileId();
        QVERIFY(initialId == QStringLiteral("metal")
                || initialId == QStringLiteral("cpu"));

        // Installing a CPU backend switches (and persists) to the cpu preset.
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        QCOMPARE(store.activeProfileId(), QStringLiteral("cpu"));
        QCOMPARE(settings.launchProfileId(), QStringLiteral("cpu"));

        // Switching to a Metal backend switches back to the metal preset.
        settings.setRuntimeBackend(QStringLiteral("metal"));
        QCOMPARE(store.activeProfileId(), QStringLiteral("metal"));
        QCOMPARE(settings.launchProfileId(), QStringLiteral("metal"));

        // An unknown backend keeps the current selection (nothing better).
        settings.setRuntimeBackend(QStringLiteral("vulkan"));
        QCOMPARE(store.activeProfileId(), QStringLiteral("metal"));
    }

    void storeDraftSaveLoad()
    {
        QTemporaryDir dir;
        const QString presetsPath =
            writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);
        QCOMPARE(store.activeProfileId(), QStringLiteral("cpu"));
        QCOMPARE(store.draftModel()->rowCount(), 1);
        QCOMPARE(store.draftModel()->data(
                     store.draftModel()->index(0),
                     LaunchParametersModel::ValueTextRole),
                 QStringLiteral("8192"));

        // Draft edits do not touch the persisted state.
        QVERIFY(store.setDraftValue(0, "4096"));
        QCOMPARE(store.activeProfile().parameters.first().value.toDouble(), 8192.0);
        QVERIFY(!store.hasUserProfile());

        // Save commits the draft: a user copy is written and wins.
        store.saveDraft();
        QVERIFY(store.hasUserProfile());
        QCOMPARE(store.activeProfile().parameters.first().value.toDouble(), 4096.0);
        QCOMPARE(settings.launchProfileId(), QStringLiteral("cpu"));

        // A fresh store reads the user copy back.
        LaunchProfileStore store2(settings, presetsPath);
        QCOMPARE(store2.activeProfile().parameters.first().value.toDouble(), 4096.0);

        // Saving built-in values again drops the user copy.
        store2.loadDefaultDraft();
        store2.saveDraft();
        QVERIFY(!store2.hasUserProfile());
        QCOMPARE(store2.activeProfile().parameters.first().value.toDouble(), 8192.0);
    }

    void storeAppendRemove()
    {
        QTemporaryDir dir;
        const QString presetsPath =
            writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);

        // The UI must not add reserved or duplicate names.
        QVERIFY(!store.appendDraftParameter(QStringLiteral("model"), QString()));
        QVERIFY(!store.appendDraftParameter(QStringLiteral("port"), QString()));
        QVERIFY(!store.appendDraftParameter(QStringLiteral("ctx-size"), QString()));
        QVERIFY(store.appendDraftParameter(QStringLiteral("flash-attn"),
                                           QStringLiteral("off")));
        QCOMPARE(store.draftModel()->rowCount(), 2);

        // Remove the built-in ctx-size row: the user copy now has one row.
        store.removeDraftRow(0);
        store.saveDraft();
        QCOMPARE(store.activeProfile().parameters.size(), 1);
        QCOMPARE(store.activeProfile().parameters.first().name,
                 QStringLiteral("flash-attn"));

        // Restore defaults drops the user copy.
        store.resetToDefaults();
        QVERIFY(!store.hasUserProfile());
        QCOMPARE(store.activeProfile().parameters.size(), 1);
        QCOMPARE(store.activeProfile().parameters.first().name,
                 QStringLiteral("ctx-size"));
    }

    void storeSelectDraftProfileSwitchesRows()
    {
        QTemporaryDir dir;
        const QString presetsPath =
            writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);

        // Switching the draft preset loads that preset's rows (uncommitted).
        store.selectDraftProfile(QStringLiteral("metal"));
        QCOMPARE(store.draftProfileId(), QStringLiteral("metal"));
        QCOMPARE(store.draftModel()->rowCount(), 2);
        // The persisted selection is untouched by a draft-only switch.
        QCOMPARE(settings.launchProfileId(), QStringLiteral("cpu"));
        QCOMPARE(store.activeProfileId(), QStringLiteral("cpu"));

        // Save commits the selection together with the rows.
        store.saveDraft();
        QCOMPARE(settings.launchProfileId(), QStringLiteral("metal"));
        // The profile follows the installed backend: with cpu installed, the
        // metal selection is not active (the stored choice returns when the
        // backend switches back, or the next auto-resolve normalizes it).
        QCOMPARE(store.activeProfileId(), QStringLiteral("cpu"));

        // Cancel semantics: reloadDraft() returns to the persisted state.
        store.selectDraftProfile(QStringLiteral("cpu"));
        store.reloadDraft();
        QCOMPARE(store.draftProfileId(), QStringLiteral("cpu"));
        QCOMPARE(store.draftModel()->rowCount(), 1);
    }

    void emptyCatalogStillWorks()
    {
        QTemporaryDir dir;
        const QString presetsPath =
            writeProfileFile(dir, "presets.json", kEmptyCatalog);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore store(settings, presetsPath);
        QVERIFY(store.presetIds().isEmpty());
        QVERIFY(store.activeProfile().parameters.isEmpty());
        QVERIFY(store.draftModel()->rowCount() == 0);
        // Degenerate case (no presets at all): the draft can still be edited,
        // but saveDraft is a safe no-op — there is no preset id to key a user
        // copy by. Production always ships presets; this only guards corrupt
        // / missing resources.
        QVERIFY(store.appendDraftParameter(QStringLiteral("ctx-size"),
                                           QStringLiteral("8192")));
        QCOMPARE(store.draftModel()->rowCount(), 1);
        store.saveDraft();
        QVERIFY(!store.hasUserProfile());
        QVERIFY(store.activeProfile().parameters.isEmpty());
    }
};

QTEST_MAIN(TestLaunchProfile)
#include "test_launch_profile.moc"
