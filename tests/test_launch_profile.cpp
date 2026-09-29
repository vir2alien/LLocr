#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ReleaseCatalog.h"
#include "testsettings.h"

using namespace llocr;

namespace {

QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// Two presets: metal-tagged and cpu-tagged, for the auto-switch tests. The
// policy section is a layer of its own: it depends on neither the model nor
// the platform, so it rides along with every preset.
constexpr const char *kPresetsJson = R"({
    "schemaVersion": 1,
    "policy": { "parameters": [
        { "order": 1, "name": "parallel", "value": 1 },
        { "order": 2, "name": "no-warmup" }
    ] },
    "profiles": [
        { "id": "metal", "name": "Metal", "os": "macos", "backend": "metal",
          "parameters": [
              { "order": 1, "name": "ctx-size", "value": 16384 },
              { "order": 2, "name": "flash-attn", "value": "off" }
          ] },
        { "id": "cpu", "name": "CPU", "backend": "cpu",
          "parameters": [
              { "order": 1, "name": "ctx-size", "value": 8192 }
          ] }
    ]
})";

constexpr const char *kEmptyCatalog = R"({ "schemaVersion": 1, "profiles": [] })";

// A platform catalog that names no model parameters: those live in
// models/<id>.json, so a name here would apply to every model the machine runs.
constexpr const char *kLayeredPresetsJson = R"({
    "schemaVersion": 1,
    "policy": { "parameters": [ { "order": 1, "name": "parallel", "value": 1 } ] },
    "fallback": { "parameters": [
        { "order": 1, "name": "ctx-size", "value": 16384 },
        { "order": 2, "name": "n-predict", "value": 8192 }
    ] },
    "profiles": [
        { "id": "cpu", "name": "CPU", "backend": "cpu",
          "parameters": [ { "order": 1, "name": "flash-attn", "value": "off" } ] }
    ]
})";

// One file per model, as shipped. Unlimited-OCR names the vision budget and
// the DRY reset; LFM2.5-VL names neither; Qwen answers the check role only.
constexpr const char *kUnlimitedProfileJson = R"({
    "schemaVersion": 1,
    "id": "unlimited-ocr",
    "title": "Unlimited-OCR",
    "fallback": { "launch": [ { "order": 1, "name": "ctx-size", "value": 16384 } ] },
    "roles": { "ocr": { "launch": [
        { "order": 1, "name": "ctx-size", "value": 16384 },
        { "order": 2, "name": "image-min-tokens", "value": 456 },
        { "order": 3, "name": "dry-sequence-breaker", "value": "none" }
    ] } }
})";

constexpr const char *kLfmProfileJson = R"({
    "schemaVersion": 1,
    "id": "lfm25-vl-3b",
    "title": "LFM2.5-VL-3B",
    "fallback": { "launch": [ { "order": 1, "name": "ctx-size", "value": 16384 } ] },
    "roles": { "ocr": { "launch": [
        { "order": 1, "name": "ctx-size", "value": 16384 }
    ] } }
})";

constexpr const char *kCheckProfileJson = R"({
    "schemaVersion": 1,
    "id": "qwen3.5-4b",
    "title": "Qwen3.5-4B",
    "fallback": { "launch": [ { "order": 1, "name": "ctx-size", "value": 8192 } ] },
    "roles": { "check": { "launch": [
        { "order": 1, "name": "ctx-size", "value": 8192 },
        { "order": 2, "name": "cache-type-k", "value": "q8_0" }
    ] } }
})";

QString writeProfileFile(const QTemporaryDir &dir, const QString &name, const QByteArray &json)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(json);
    return path;
}

}  // namespace

class TestLaunchProfile : public QObject
{
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
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        // No runtime installed: the platform-recommended backend resolves
        // (cpu on Windows/Linux, metal on macOS — both presets exist).
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);
        const QString initialId = store.activeProfileId();
        QVERIFY(initialId == QStringLiteral("metal") || initialId == QStringLiteral("cpu"));

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

    // The combo offers only what this machine can run. A preset tagged for
    // another backend is not kept — activeProfileId() drops it on the next
    // resolve — so offering it is offering a choice that cannot be saved.
    void thePresetListHoldsOnlyWhatThisMachineCanRun()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);

        // The universal CPU profile fits any OS; the metal one does not fit a
        // cpu build.
        QCOMPARE(store.presetIds(), QStringList{QStringLiteral("cpu")});
        QCOMPARE(store.presetNames(), QStringList{QStringLiteral("CPU")});

        // What the machine could run with another build. On macOS that is the
        // metal profile; a Windows profile is not a candidate for this machine
        // and stays out of the answer.
        const QStringList other = store.otherPresetNames();
        if (ReleaseCatalog::detectPlatform().osTag == QLatin1String("macos"))
            QCOMPARE(other, QStringList{QStringLiteral("Metal")});
        else
            QVERIFY(!other.contains(QStringLiteral("Metal")));

        // A backend the catalog does not know (a build from somewhere else)
        // leaves nothing applicable, and an empty combo would be worse than the
        // full list.
        settings.setRuntimeBackend(QStringLiteral("vulkan"));
        QCOMPARE(store.presetIds().size(), 2);
        QCOMPARE(store.presetNames().size(), store.presetIds().size());
    }

    void storeDraftSaveLoad()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);
        QCOMPARE(store.activeProfileId(), QStringLiteral("cpu"));
        // 2 policy rows + 1 platform row.
        QCOMPARE(store.draftModel()->rowCount(), 3);
        QCOMPARE(store.draftModel()->data(store.draftModel()->index(2), LaunchParametersModel::ValueTextRole), QStringLiteral("8192"));

        // Draft edits do not touch the persisted state.
        QVERIFY(store.setDraftValue(2, "4096"));
        QCOMPARE(store.activeProfile().find(QStringLiteral("ctx-size"))->value.toDouble(), 8192.0);
        QVERIFY(!store.hasUserProfile());

        // Save commits the draft: a user copy is written and wins.
        store.saveDraft();
        QVERIFY(store.hasUserProfile());
        QCOMPARE(store.activeProfile().find(QStringLiteral("ctx-size"))->value.toDouble(), 4096.0);
        QCOMPARE(settings.launchProfileId(), QStringLiteral("cpu"));

        // A fresh store reads the user copy back.
        LaunchProfileStore store2(settings, presetsPath);
        QCOMPARE(store2.activeProfile().find(QStringLiteral("ctx-size"))->value.toDouble(), 4096.0);

        // Saving built-in values again drops the user copy.
        store2.loadDefaultDraft();
        store2.saveDraft();
        QVERIFY(!store2.hasUserProfile());
        QCOMPARE(store2.activeProfile().find(QStringLiteral("ctx-size"))->value.toDouble(), 8192.0);
    }

    void storeAppendRemove()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);

        // The UI must not add reserved or duplicate names.
        QVERIFY(!store.appendDraftParameter(QStringLiteral("model"), QString()));
        QVERIFY(!store.appendDraftParameter(QStringLiteral("port"), QString()));
        QVERIFY(!store.appendDraftParameter(QStringLiteral("ctx-size"), QString()));
        // A policy row is inherited by every platform, so it cannot be added
        // a second time either.
        QVERIFY(!store.appendDraftParameter(QStringLiteral("parallel"), QString()));
        QVERIFY(store.appendDraftParameter(QStringLiteral("flash-attn"), QStringLiteral("off")));
        QCOMPARE(store.draftModel()->rowCount(), 4);

        // Remove the built-in ctx-size row: the user copy now has one row.
        store.removeDraftRow(2);
        store.saveDraft();
        QCOMPARE(store.activeProfile().find(QStringLiteral("ctx-size")), nullptr);
        QCOMPARE(store.activeProfile().find(QStringLiteral("flash-attn"))->value.toString(), QStringLiteral("off"));

        // Restore defaults drops the user copy.
        store.resetToDefaults();
        QVERIFY(!store.hasUserProfile());
        QVERIFY(store.activeProfile().find(QStringLiteral("ctx-size")) != nullptr);
        QVERIFY(store.activeProfile().find(QStringLiteral("flash-attn")) == nullptr);
    }

    // The model layer is a property of the weights, so what one model names the
    // next must not inherit. This is the whole point of the split: a parameter
    // in the platform file applies to every model the machine runs.
    void modelParametersDoNotLeakBetweenModels()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kLayeredPresetsJson);
        const QString modelDir = QDir(dir.path()).filePath(QStringLiteral("models"));
        QVERIFY(QDir().mkpath(modelDir));
        QFile unlimitedFile(QDir(modelDir).filePath(QStringLiteral("unlimited-ocr.json")));
        QVERIFY(unlimitedFile.open(QIODevice::WriteOnly));
        QCOMPARE(unlimitedFile.write(QByteArray(kUnlimitedProfileJson)), qint64(QByteArray(kUnlimitedProfileJson).size()));
        unlimitedFile.close();
        QFile lfmFile(QDir(modelDir).filePath(QStringLiteral("lfm25-vl-3b.json")));
        QVERIFY(lfmFile.open(QIODevice::WriteOnly));
        QCOMPARE(lfmFile.write(QByteArray(kLfmProfileJson)), qint64(QByteArray(kLfmProfileJson).size()));
        lfmFile.close();

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath(QStringLiteral("modelsDir")));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);

        QString error;
        const QList<ModelProfiles::Profile> profiles = ModelProfiles::loadFrom(modelDir, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(profiles.size(), 2);
        store.setModelProfiles(profiles);

        // Unlimited-OCR carries the vision budget and the DRY reset.
        const LaunchProfile unlimited = store.activeProfile(QStringLiteral("unlimited-ocr"), QStringLiteral("ocr"));
        QVERIFY(unlimited.find(QStringLiteral("image-min-tokens")) != nullptr);
        QVERIFY(unlimited.find(QStringLiteral("dry-sequence-breaker")) != nullptr);
        QCOMPARE(unlimited.find(QStringLiteral("image-min-tokens"))->value.toDouble(), 456.0);

        // LFM2.5-VL names neither: a parameter it does not declare must be
        // absent, not inherited and not left at a stale value.
        const LaunchProfile lfm = store.activeProfile(QStringLiteral("lfm25-vl-3b"), QStringLiteral("ocr"));
        QVERIFY(lfm.find(QStringLiteral("image-min-tokens")) == nullptr);
        QVERIFY(lfm.find(QStringLiteral("dry-sequence-breaker")) == nullptr);
        // …and the layers it does share are all present.
        QVERIFY(lfm.find(QStringLiteral("ctx-size")) != nullptr);
        QVERIFY(lfm.find(QStringLiteral("flash-attn")) != nullptr);
        QVERIFY(lfm.find(QStringLiteral("parallel")) != nullptr);

        // A model with no profile at all — a hand-picked GGUF — still gets a
        // context window, from the global fallback in serverLaunch.json. Left to
        // llama.cpp its own default is 4096, which truncates a page with a
        // table, and the user has no way to see that it happened.
        const LaunchProfile unknown = store.activeProfile(QStringLiteral("some-other-model"), QStringLiteral("ocr"));
        QVERIFY(unknown.find(QStringLiteral("ctx-size")) != nullptr);
        QCOMPARE(unknown.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);
        QVERIFY(unknown.find(QStringLiteral("n-predict")) != nullptr);
        // The fallback is last resort only: it does not leak into a model that
        // has a profile, and it does not shadow the platform or policy layers.
        QVERIFY(unknown.find(QStringLiteral("parallel")) != nullptr);
        QVERIFY(unknown.find(QStringLiteral("flash-attn")) != nullptr);
        QVERIFY(unknown.find(QStringLiteral("image-min-tokens")) == nullptr);
    }

    // A model answers one role; asking it for another must not hand over the
    // rows it declares for the role it does have.
    void modelParametersArePerRole()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kLayeredPresetsJson);
        const QString modelDir = QDir(dir.path()).filePath(QStringLiteral("models"));
        QVERIFY(QDir().mkpath(modelDir));
        QFile file(QDir(modelDir).filePath(QStringLiteral("qwen3.5-4b.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(kCheckProfileJson)), qint64(QByteArray(kCheckProfileJson).size()));
        file.close();

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath(QStringLiteral("modelsDir")));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);

        QString error;
        const QList<ModelProfiles::Profile> loaded = ModelProfiles::loadFrom(modelDir, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.size(), 1);
        store.setModelProfiles(loaded);

        // The check role gets the verifier's own values.
        const LaunchProfile check = store.activeProfile(QStringLiteral("qwen3.5-4b"), QStringLiteral("check"));
        QCOMPARE(check.find(QStringLiteral("cache-type-k"))->value.toString(), QStringLiteral("q8_0"));
        QCOMPARE(check.find(QStringLiteral("ctx-size"))->value.toDouble(), 8192.0);

        // Asking the same model for the ocr role does not hand over the check
        // role's rows — the model simply does not answer that role, and the
        // global fallback covers the ground instead.
        const LaunchProfile ocr = store.activeProfile(QStringLiteral("qwen3.5-4b"), QStringLiteral("ocr"));
        QVERIFY(ocr.find(QStringLiteral("cache-type-k")) == nullptr);
        QVERIFY(ocr.find(QStringLiteral("cache_prompt")) == nullptr);
        QCOMPARE(ocr.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);

        // The notice asks the same question the composition does, per role: the
        // model is in the catalog, but not for this one.
        settings.setModelRecipeId(QStringLiteral("qwen3.5-4b"));
        settings.setCheckRequestProfileId(QStringLiteral("qwen3.5-4b"));
        QVERIFY(store.modelProfileMissing());
        QVERIFY(!store.checkModelProfileMissing());
    }

    // The notice the UI shows when the fallback is in play. Without it the
    // difference between the model's own 16384 and the fallback's is invisible.
    void fallbackIsReportedWhenTheModelIsUnknown()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kLayeredPresetsJson);
        const QString modelDir = QDir(dir.path()).filePath(QStringLiteral("models"));
        QVERIFY(QDir().mkpath(modelDir));
        QFile file(QDir(modelDir).filePath(QStringLiteral("unlimited-ocr.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(kUnlimitedProfileJson));
        file.close();

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath(QStringLiteral("modelsDir")));
        LaunchProfileStore store(settings, presetsPath);

        // Nothing loaded yet: every model is unknown, and the store says so
        // rather than silently serving the fallback.
        QVERIFY(store.modelProfileMissing());

        QString error;
        store.setModelProfiles(ModelProfiles::loadFrom(modelDir, error));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        settings.setModelRecipeId(QStringLiteral("unlimited-ocr"));
        QVERIFY(!store.modelProfileMissing());
        // The check role has no profile at all — it is a different model.
        QVERIFY(store.checkModelProfileMissing());

        settings.setModelRecipeId(QStringLiteral("some-other-model"));
        QVERIFY(store.modelProfileMissing());
    }

    // A profile written before the layers split carries the model's own
    // parameters, which now come from its profile. Adopted as is, it puts a
    // stale ctx-size back and duplicates the policy rows.
    void staleUserCopyIsPrunedToTheLayersItMayOwn()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kLayeredPresetsJson);
        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath(QStringLiteral("models")));
        settings.setRuntimeBackend(QStringLiteral("cpu"));  // the only preset the fixture has

        // A copy in the old shape: the whole set, model parameters included,
        // with the ctx-size the install path used to overwrite.
        const QDir profilesDir = RuntimePaths(settings.runtimeRootDir(), settings.runtimeModelsDir()).profilesDir();
        QVERIFY(QDir().mkpath(profilesDir.path()));
        const QString userPath = profilesDir.filePath(QStringLiteral("serverLaunch.json"));
        QFile user(userPath);
        QVERIFY(user.open(QIODevice::WriteOnly));
        user.write(QByteArrayLiteral("{\"schemaVersion\":1,\"profiles\":[{\"id\":\"cpu\",\"parameters\":["
                                     "{\"order\":1,\"name\":\"ctx-size\",\"value\":8192},"
                                     "{\"order\":2,\"name\":\"n-predict\",\"value\":4096},"
                                     "{\"order\":3,\"name\":\"flash-attn\",\"value\":\"off\"},"
                                     "{\"order\":4,\"name\":\"image-min-tokens\",\"value\":456}]}]}"));
        user.close();

        LaunchProfileStore store(settings, presetsPath);

        // The draft is exactly the two layers a user copy may own: policy and
        // platform. The model's rows must not be there.
        const QAbstractListModel *draft = store.draftModel();
        QStringList draftNames;
        for (int i = 0; i < draft->rowCount(); ++i)
            draftNames << draft->data(draft->index(i), LaunchParametersModel::NameRole).toString();
        QVERIFY(!draftNames.contains(QStringLiteral("ctx-size")));
        QVERIFY(!draftNames.contains(QStringLiteral("n-predict")));
        QVERIFY(!draftNames.contains(QStringLiteral("image-min-tokens")));
        // The user's own platform row survived…
        QVERIFY(draftNames.contains(QStringLiteral("flash-attn")));
        // …and the policy row is there once, not twice.
        QCOMPARE(draftNames.count(QStringLiteral("parallel")), 1);

        // Resolved, the model supplies its own context — not the stale 8192.
        const LaunchProfile active = store.activeProfile();
        QVERIFY(active.find(QStringLiteral("ctx-size")) != nullptr);
        QCOMPARE(active.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);
    }

    // The policy layer is a layer of its own: it rides along with every
    // platform, and its rows are not the user's to edit — an edit there would
    // either be silently dropped on save or would leak into a single preset.
    void policyLayerIsSharedAndReadOnly()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);

        const QAbstractListModel *model = store.draftModel();
        QCOMPARE(model->rowCount(), 4);
        QCOMPARE(model->data(model->index(0), LaunchParametersModel::NameRole), QStringLiteral("parallel"));
        QCOMPARE(model->data(model->index(1), LaunchParametersModel::NameRole), QStringLiteral("no-warmup"));
        QCOMPARE(model->data(model->index(0), LaunchParametersModel::EditableRole), false);
        QCOMPARE(model->data(model->index(1), LaunchParametersModel::EditableRole), false);
        QCOMPARE(model->data(model->index(2), LaunchParametersModel::EditableRole), true);

        // Neither editing nor removing a policy row is accepted.
        QVERIFY(!store.setDraftValue(0, "4"));
        store.removeDraftRow(1);
        QCOMPARE(model->rowCount(), 4);

        // A policy row is present whichever platform resolves.
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
        settings.setRuntimeBackend(QStringLiteral("metal"));
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
    }

    void storeSelectDraftProfileSwitchesRows()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);

        // Switching the draft preset loads that preset's rows (uncommitted).
        store.selectDraftProfile(QStringLiteral("metal"));
        QCOMPARE(store.draftProfileId(), QStringLiteral("metal"));
        QCOMPARE(store.draftModel()->rowCount(), 4);
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
        QCOMPARE(store.draftModel()->rowCount(), 3);
    }

    void emptyCatalogStillWorks()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kEmptyCatalog);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);
        QVERIFY(store.presetIds().isEmpty());
        QVERIFY(store.activeProfile().parameters.isEmpty());
        QVERIFY(store.draftModel()->rowCount() == 0);
        // Degenerate case (no presets at all): the draft can still be edited,
        // but saveDraft is a safe no-op — there is no preset id to key a user
        // copy by. Production always ships presets; this only guards corrupt
        // / missing resources.
        QVERIFY(store.appendDraftParameter(QStringLiteral("ctx-size"), QStringLiteral("8192")));
        QCOMPARE(store.draftModel()->rowCount(), 1);
        store.saveDraft();
        QVERIFY(!store.hasUserProfile());
        QVERIFY(store.activeProfile().parameters.isEmpty());
    }
};

QTEST_MAIN(TestLaunchProfile)
#include "test_launch_profile.moc"
