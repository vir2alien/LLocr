#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <QAbstractItemModelTester>

#include "testsettings.h"

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/LaunchProfile.h"
#include "core/ModelProfiles.h"
#include "runtime/LaunchParametersModel.h"
#include "runtime/LaunchProfileStore.h"

using namespace llocr;

namespace {

QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// Two presets: metal-tagged and cpu-tagged, for the auto-switch tests. The
// policy section is not part of any preset: the app owns it, and the settings
// table never shows it.
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
    "profiles": [
        { "id": "cpu", "name": "CPU", "backend": "cpu",
          "parameters": [
              { "order": 1, "name": "ctx-size", "value": 16384 },
              { "order": 2, "name": "flash-attn", "value": "off" }
          ] }
    ]
})";

// One file per model, as shipped. Unlimited-OCR names the vision budget and
// the DRY reset; LFM2.5-VL names neither; Qwen answers the check role only.
// The shipped shape (ADR 126): the context window belongs to the machine, so
// the platform profile carries it.
constexpr const char *kUnlimitedProfileJson = R"({
    "schemaVersion": 1,
    "id": "unlimited-ocr",
    "title": "Unlimited-OCR",
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
    "roles": { "ocr": { "launch": [
        { "order": 1, "name": "ctx-size", "value": 16384 }
    ] } }
})";

constexpr const char *kCheckProfileJson = R"({
    "schemaVersion": 1,
    "id": "qwen3.5-4b",
    "title": "Qwen3.5-4B",
    "roles": { "blockRecognition": { "launch": [
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

    // The launch profile follows the installed build with no picker in
    // between: switching the backend reloads the draft from the newly
    // resolved preset, so the settings table always shows what the server
    // will be started with.
    void draftFollowsTheResolvedProfile()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);
        QVERIFY(store.draftProfileId() == QStringLiteral("metal") || store.draftProfileId() == QStringLiteral("cpu"));

        // Installing a CPU build switches the draft (and the persisted
        // selection) to the cpu preset; back again on metal.
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        QCOMPARE(store.draftProfileId(), QStringLiteral("cpu"));
        settings.setRuntimeBackend(QStringLiteral("metal"));
        QCOMPARE(store.draftProfileId(), QStringLiteral("metal"));
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
        // The draft is the platform layer alone: one row, the machine's.
        QCOMPARE(store.draftModel()->rowCount(), 1);
        QCOMPARE(store.draftModel()->data(store.draftModel()->index(0), LaunchParametersModel::NameRole), QStringLiteral("ctx-size"));
        QCOMPARE(store.draftModel()->data(store.draftModel()->index(0), LaunchParametersModel::ValueTextRole), QStringLiteral("8192"));

        // Draft edits do not touch the persisted state.
        QVERIFY(store.setDraftValue(0, "4096"));
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

        // A model with no profile at all — a hand-picked GGUF — still gets the
        // machine's parameters, and nothing of any other model's.
        const LaunchProfile unknown = store.activeProfile(QStringLiteral("some-other-model"), QStringLiteral("ocr"));
        QVERIFY(unknown.find(QStringLiteral("ctx-size")) != nullptr);
        QCOMPARE(unknown.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);
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
        const LaunchProfile check = store.activeProfile(QStringLiteral("qwen3.5-4b"), QStringLiteral("blockRecognition"));
        QCOMPARE(check.find(QStringLiteral("cache-type-k"))->value.toString(), QStringLiteral("q8_0"));
        QCOMPARE(check.find(QStringLiteral("ctx-size"))->value.toDouble(), 8192.0);

        // Asking the same model for the ocr role does not hand over the check
        // role's rows — the model simply does not answer that role, and the
        // platform layer covers the ground instead.
        const LaunchProfile ocr = store.activeProfile(QStringLiteral("qwen3.5-4b"), QStringLiteral("ocr"));
        QVERIFY(ocr.find(QStringLiteral("cache-type-k")) == nullptr);
        QVERIFY(ocr.find(QStringLiteral("cache_prompt")) == nullptr);
        QCOMPARE(ocr.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);

        // The notice asks whether the model is in the catalog, not whether it
        // answers this role: qwen3.5-4b is in it, so neither window warns — it
        // simply does not answer the ocr role, and the machine's parameters stand
        // in for it. A model the catalog does not know is what the notice is for.
        settings.setModelRecipeId(QStringLiteral("qwen3.5-4b"));
        settings.setCheckRequestProfileId(QStringLiteral("qwen3.5-4b"));
        QVERIFY(!store.modelProfileMissing());
        QVERIFY(!store.checkModelProfileMissing());
        settings.setModelRecipeId(QStringLiteral("some-hand-picked-gguf"));
        QVERIFY(store.modelProfileMissing());
        QVERIFY(!store.checkModelProfileMissing());
    }

    void modelProfileMissingIsReported()
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

        // Nothing loaded yet: every model is unknown, and the store says so.
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

    // A user copy written in an older shape — model rows, policy rows, or a
    // generation budget that no profile carries any more — cannot survive the
    // merge cleanly, so the startup pruning drops the whole copy.
    void staleUserCopyIsDroppedWholesale()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kLayeredPresetsJson);
        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath(QStringLiteral("models")));
        settings.setRuntimeBackend(QStringLiteral("cpu"));  // the only preset the fixture has

        // A copy in the old shape: the whole set, model parameters included.
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

        // n-predict and image-min-tokens are not platform rows any more, so the
        // stale copy is gone: the draft is the built-in platform layer, and the
        // machine's 16384 stands — not the stale 8192.
        QVERIFY(!store.hasUserProfile());
        const QAbstractListModel *draft = store.draftModel();
        QCOMPARE(draft->rowCount(), 2);
        QCOMPARE(draft->data(draft->index(0), LaunchParametersModel::NameRole), QStringLiteral("ctx-size"));
        QCOMPARE(draft->data(draft->index(0), LaunchParametersModel::ValueTextRole), QStringLiteral("16384"));
        QCOMPARE(draft->data(draft->index(1), LaunchParametersModel::NameRole), QStringLiteral("flash-attn"));

        const LaunchProfile active = store.activeProfile();
        QCOMPARE(active.find(QStringLiteral("ctx-size"))->value.toDouble(), 16384.0);
        // The policy still rides along with the composed configuration…
        QVERIFY(active.find(QStringLiteral("parallel")) != nullptr);
        // …but it never entered the draft the user could edit.
        QCOMPARE(draft->data(draft->index(0), LaunchParametersModel::NameRole), QStringLiteral("ctx-size"));
    }

    // The draft is the platform layer: the policy reaches the server without
    // ever appearing in the table, and it rides along with every platform.
    void policyLayerIsSharedAndOutOfTheTable()
    {
        QTemporaryDir dir;
        const QString presetsPath = writeProfileFile(dir, "presets.json", kPresetsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));
        LaunchProfileStore store(settings, presetsPath);
        QAbstractItemModelTester tester(store.draftModel(), QAbstractItemModelTester::FailureReportingMode::Fatal);

        const QAbstractListModel *model = store.draftModel();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->data(model->index(0), LaunchParametersModel::NameRole), QStringLiteral("ctx-size"));
        QCOMPARE(model->data(model->index(1), LaunchParametersModel::NameRole), QStringLiteral("flash-attn"));

        // Every platform row is the user's to edit.
        QVERIFY(store.setDraftValue(0, "4096"));

        // A policy row is present in the composed configuration whichever
        // platform resolves — the app starts the server with it, not the table.
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
        QVERIFY(store.activeProfile().find(QStringLiteral("no-warmup")) != nullptr);
        settings.setRuntimeBackend(QStringLiteral("cpu"));
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
        settings.setRuntimeBackend(QStringLiteral("metal"));
        QVERIFY(store.activeProfile().find(QStringLiteral("parallel")) != nullptr);
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
        QVERIFY(store.activeProfileId().isEmpty());
        QVERIFY(store.activeProfile().parameters.isEmpty());
        QVERIFY(store.draftModel()->rowCount() == 0);
        // Degenerate case (no presets at all): there is no preset id to key a
        // user copy by, so saveDraft is a safe no-op. Production always ships
        // presets; this only guards corrupt / missing resources.
        store.saveDraft();
        QVERIFY(!store.hasUserProfile());
        QVERIFY(store.activeProfile().parameters.isEmpty());
    }
};

QTEST_MAIN(TestLaunchProfile)
#include "test_launch_profile.moc"
