#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/ModelProfiles.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelPresetCatalog.h"

using namespace llocr;

// Covers §4.6 (ModelPresetCatalog was untested) and the §4.1 schema test:
// the shipped built-in catalog must always parse and carry the fields the
// install pipeline relies on.
class TestModelPresetCatalog : public QObject
{
    Q_OBJECT

private slots:
    void builtInCatalogParses();
    // A copy-paste between two profiles once gave the verifier role to an OCR
    // model, and the two offered each other's rows. The role split and the
    // parameter lists are what make a model recognisable, so both are checked
    // against the values the model card and the launcher actually document.
    void shippedProfilesAreTheOnesTheDocsDescribe();
    void checkModelBlockPromptsOverridePerType();
    void runtimeNoteReachesTheSettings();
    void mergeByUserPrecedence();
    void importToJsonRoundTrips();
    void saveThenReset();
};

void TestModelPresetCatalog::builtInCatalogParses()
{
    // load() with an empty user path exercises just the built-in resource,
    // which must always be present and parse cleanly.
    QString err;
    const QList<ModelPreset> presets = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), QString(), err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(presets.size() >= 2);  // the shipped presets

    for (const ModelPreset &p : presets) {
        // Schema contract: the fields the pipeline consumes are present.
        QVERIFY(!p.id.isEmpty());
        QVERIFY(!p.title.isEmpty());
        QVERIFY(!p.repo.isEmpty());
        QVERIFY(!p.model.isEmpty());  // at least a main model file
        QVERIFY(p.ctxSize > 0);
        QVERIFY(!p.minBuild.isEmpty());
        QVERIFY(!p.license.isEmpty());
        // Every entry names a model profile, and that profile answers at least
        // one role — otherwise the entry could never be offered in a window.
        QVERIFY(!p.profileId.isEmpty());
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), p.profileId);
        QVERIFY2(profile, qPrintable(QStringLiteral("preset %1 names unknown profile %2").arg(p.id, p.profileId)));
        QVERIFY(ModelProfiles::roleFor(*profile, QStringLiteral("ocr")) || ModelProfiles::roleFor(*profile, QStringLiteral("check")));
    }

    // A model that serves one role is offered only there, and the catalog does
    // not carry the role itself: two lists could disagree with the profile. A
    // model may serve both — the same weights, two tasks — and the roles it
    // declares are pinned per model below.
}

void TestModelPresetCatalog::shippedProfilesAreTheOnesTheDocsDescribe()
{
    struct Expected {
        const char *id;
        const char *roles;  // comma-separated, as shipped
        const char *minBuild;
        const char *quants;  // comma-separated, as shipped
        const char *launch;
    };
    // The launch lists are the ones a wrong edit silently truncates: a model
    // that loses image-*-tokens or dry-sequence-breaker still starts, and the only
    // symptom is a worse vision budget. ctx-size and n-predict are not listed on
    // purpose — they follow the machine's memory and live in the platform
    // profiles of serverLaunch.json (ADR 126).
    const QList<Expected> expected = {
        {"unlimited-ocr", "ocr", "b4000", "q8_0,q4_k_m", "image-min-tokens,image-max-tokens,dry-sequence-breaker,special"},
        {"lfm25-vl-3b", "ocr", "b8000", "q4_k_m,q8_0", "special"},
        {"qwen3.5-4b", "check", "b4000", "q8_0,q4_k_xl", ""},
        {"teleocr", "ocr,check", "b4000", "q4_k_m,q8_0", ""},
    };

    for (const Expected &e : expected) {
        const QString id = QString::fromUtf8(e.id);
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), id);
        QVERIFY2(profile, qPrintable(id));
        QCOMPARE(profile->minBuild, QString::fromUtf8(e.minBuild));

        QStringList quants;
        for (const ModelProfiles::Quant &q : profile->files.quants)
            quants.append(q.id);
        QCOMPARE(quants.join(QLatin1Char(',')), QString::fromUtf8(e.quants));

        // Exactly the roles this model serves — a stray second role means a
        // profile was pasted from the wrong file. Compared as a set: the role
        // keys live in a QHash and have no order to compare against.
        const QStringList declared = QString::fromUtf8(e.roles).split(QLatin1Char(','));
        QCOMPARE(profile->roles.size(), declared.size());
        for (const QString &roleName : declared)
            QVERIFY2(ModelProfiles::roleFor(*profile, roleName), qPrintable(QStringLiteral("%1 does not declare the role %2").arg(id, roleName)));
        for (const QString &roleName : declared) {
            const ModelProfiles::Role *role = ModelProfiles::roleFor(*profile, roleName);
            QVERIFY2(role, qPrintable(id));
            QStringList launch;
            for (const LaunchParameter &p : role->launch)
                launch.append(p.name);
            QCOMPARE(launch.join(QLatin1Char(',')), QString::fromUtf8(e.launch));
        }

        // A model that serves both roles has to declare the same launch layer
        // in both: the flags are startup flags, so a difference makes the
        // runtime unload and reload the very model the user picked once.
        if (declared.size() == 2) {
            const ModelProfiles::Role *ocr = ModelProfiles::roleFor(*profile, QStringLiteral("ocr"));
            const ModelProfiles::Role *check = ModelProfiles::roleFor(*profile, QStringLiteral("check"));
            QVERIFY2(ocr->launch == check->launch, qPrintable(QStringLiteral("%1: the two roles declare different launch parameters, so switching task reloads the model").arg(id)));
        }
    }
}

// A check model can carry its own wording per block type — it was trained with
// it — while a type it says nothing about still gets verifyPrompts.json. The
// types are the labels the OCR parsers emit, so a typo here would silently
// disable the override.
void TestModelPresetCatalog::checkModelBlockPromptsOverridePerType()
{
    const ModelProfiles::Role *role = ModelProfiles::roleFor(QStringLiteral("teleocr"), QStringLiteral("check"));
    QVERIFY(role);
    QVERIFY(!role->blockPrompts.isEmpty());

    const QStringList expected = {QStringLiteral("text"), QStringLiteral("title"), QStringLiteral("table"), QStringLiteral("code"), QStringLiteral("formula"), QStringLiteral("equation")};
    for (const QString &type : expected)
        QVERIFY2(!role->blockPrompts.value(type).isEmpty(), qPrintable(type));

    // Every override has to keep the answer protocol's output format: the block
    // prompt is the only place it is stated, the system prompt carries the rest.
    QCOMPARE(role->blockPrompts.value(QStringLiteral("formula")).contains(QStringLiteral("Output format after FIX: LaTeX.")), true);
    QCOMPARE(role->blockPrompts.value(QStringLiteral("table")).contains(QStringLiteral("Output format after FIX: HTML table")), true);
    // Tables: the model card asks for OTSL, the app compares HTML — the override
    // must not ask for the format the parser cannot use.
    QVERIFY(!role->blockPrompts.value(QStringLiteral("table")).contains(QStringLiteral("OTSL format")));

    // An unknown type, a model without prompts and a model outside the catalog
    // all fall back to what the caller passes.
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const QString fallback = QStringLiteral("from verifyPrompts.json");
    QCOMPARE(ModelProfiles::blockPromptFor(profiles, QStringLiteral("teleocr"), QStringLiteral("check"), QStringLiteral("list"), fallback), fallback);
    QCOMPARE(ModelProfiles::blockPromptFor(profiles, QStringLiteral("teleocr"), QStringLiteral("check"), QStringLiteral("formula"), fallback).isEmpty(), false);
    QCOMPARE(ModelProfiles::blockPromptFor(profiles, QStringLiteral("qwen3.5-4b"), QStringLiteral("check"), QStringLiteral("formula"), fallback), fallback);
    QCOMPARE(ModelProfiles::blockPromptFor(profiles, QStringLiteral("some-guf"), QStringLiteral("check"), QStringLiteral("formula"), fallback), fallback);
    // The ocr role of the same model has no block prompts at all.
    QCOMPARE(ModelProfiles::blockPromptFor(profiles, QStringLiteral("unlimited-ocr"), QStringLiteral("ocr"), QStringLiteral("formula"), fallback), fallback);
}

// A model the managed runtime cannot load says so in its profile, and the note
// reaches the store the settings windows read. Fed from a profile built here, not
// from a shipped one: no shipped model needs a note, and a test that borrowed one
// would start failing the day its model became runnable.
void TestModelPresetCatalog::runtimeNoteReachesTheSettings()
{
    ModelProfiles::Profile unrunnable;
    unrunnable.id = QStringLiteral("needs-a-patch");
    unrunnable.title = unrunnable.id;
    unrunnable.runtimeNote = QStringLiteral("stock llama.cpp cannot load this GGUF");

    QList<ModelProfiles::Profile> profiles = {unrunnable, ModelProfiles::instance().constFirst()};
    QCOMPARE(ModelProfiles::runtimeNoteFor(profiles, QStringLiteral("needs-a-patch")), QStringLiteral("stock llama.cpp cannot load this GGUF"));
    // No note, no warning: every model the app ships runs on the runtime it
    // downloads (TeleOCR carried a note until its repository moved to weights
    // that load on a stock build).
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, profiles.last().id).isEmpty());
    QVERIFY(ModelProfiles::runtimeNoteFor(profiles, QStringLiteral("some-guf")).isEmpty());
}

void TestModelPresetCatalog::mergeByUserPrecedence()
{
    // Build a user catalog that overrides one built-in id and adds a new one.
    QList<ModelPreset> user;
    ModelPreset override_;
    override_.id = QStringLiteral("unlimited-ocr-q4km");
    override_.title = QStringLiteral("User-tuned Unlimited-OCR");
    override_.repo = QStringLiteral("user/override-repo");
    override_.model = QStringLiteral("model.gguf");
    user << override_;

    ModelPreset added;
    added.id = QStringLiteral("brand-new-preset");
    added.title = QStringLiteral("New");
    added.repo = QStringLiteral("user/new-repo");
    added.model = QStringLiteral("new.gguf");
    user << added;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userPath = QDir(dir.path()).filePath(QStringLiteral("catalog.json"));
    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, user, err));
    QVERIFY(err.isEmpty());

    const QList<ModelPreset> merged = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());

    // The overridden id now carries the user's title/repo.
    bool sawOverride = false;
    bool sawNew = false;
    for (const ModelPreset &p : merged) {
        if (p.id == override_.id) {
            sawOverride = true;
            QCOMPARE(p.title, QStringLiteral("User-tuned Unlimited-OCR"));
            QCOMPARE(p.repo, QStringLiteral("user/override-repo"));
        }
        if (p.id == added.id)
            sawNew = true;
    }
    QVERIFY(sawOverride);
    QVERIFY(sawNew);
}

void TestModelPresetCatalog::importToJsonRoundTrips()
{
    ModelPreset p;
    p.id = QStringLiteral("x/y");
    p.title = QStringLiteral("T");
    p.repo = QStringLiteral("x/y");
    p.revision = QStringLiteral("deadbeef");
    p.model = QStringLiteral("model-Q4_K_M.gguf");
    p.mmproj = QStringLiteral("mmproj.gguf");
    p.ctxSize = 4096;
    p.minBuild = QStringLiteral("b4000");
    p.license = QStringLiteral("apache-2.0");
    p.sha256.insert(QStringLiteral("model-q4_k_m.gguf"), QStringLiteral("abcdef"));

    const ModelPreset restored = ModelPreset::fromJson(p.toJson());
    QCOMPARE(restored.id, p.id);
    QCOMPARE(restored.revision, p.revision);
    QCOMPARE(restored.model, p.model);
    QCOMPARE(restored.mmproj, p.mmproj);
    QCOMPARE(restored.ctxSize, 4096);
    QCOMPARE(restored.sha256.value(QStringLiteral("model-q4_k_m.gguf")), QStringLiteral("abcdef"));
    // Serialize→parse→serialize is stable.
    QCOMPARE(ModelPreset::fromJson(restored.toJson()).toJson().toVariantMap(), p.toJson().toVariantMap());
}

void TestModelPresetCatalog::saveThenReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userPath = QDir(dir.path()).filePath(QStringLiteral("catalog.json"));

    QList<ModelPreset> presets;
    ModelPreset p;
    p.id = QStringLiteral("one");
    p.repo = QStringLiteral("org/one");
    p.model = QStringLiteral("one.gguf");
    presets << p;

    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, presets, err));
    QVERIFY(QFile::exists(userPath));

    // The file round-trips: load() reads the two sources and returns `one`.
    const QList<ModelPreset> loaded = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());
    bool sawSaved = false;
    for (const ModelPreset &pp : loaded)
        if (pp.id == QStringLiteral("one"))
            sawSaved = true;
    QVERIFY(sawSaved);

    // "Restore defaults" removes the user catalog.
    QVERIFY(ModelPresetCatalog::resetUserCatalog(userPath, err));
    QVERIFY(!QFile::exists(userPath));
    err.clear();
    // After reset the user catalog is gone: load() only yields built-ins.
    const QList<ModelPreset> afterReset = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), userPath, err);
    QVERIFY(err.isEmpty());
    bool hasOne = false;
    for (const ModelPreset &pp : afterReset)
        if (pp.id == QStringLiteral("one"))
            hasOne = true;
    QVERIFY(!hasOne);
}

QTEST_MAIN(TestModelPresetCatalog)
#include "test_model_preset_catalog.moc"