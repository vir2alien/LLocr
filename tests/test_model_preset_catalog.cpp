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
    // not carry the role itself: two lists could disagree with the profile.
    for (const ModelPreset &p : presets) {
        const ModelProfiles::Profile *profile = ModelProfiles::find(ModelProfiles::instance(), p.profileId);
        if (ModelProfiles::roleFor(*profile, QStringLiteral("check")))
            QVERIFY(!ModelProfiles::roleFor(*profile, QStringLiteral("ocr")));
    }
}

void TestModelPresetCatalog::shippedProfilesAreTheOnesTheDocsDescribe()
{
    struct Expected {
        const char *id;
        const char *role;
        const char *minBuild;
        const char *quants;  // comma-separated, as shipped
        const char *launch;
    };
    // The launch lists are the ones a wrong edit silently truncates: a model
    // that loses cache-type-* or cache-type-v still starts, and the only symptom
    // is a slower or a more memory-hungry run.
    const QList<Expected> expected = {
        {"unlimited-ocr", "ocr", "b4000", "q8_0,q4_k_m", "ctx-size,n-predict,cache-type-k,cache-type-v,image-min-tokens,image-max-tokens,dry-sequence-breaker,special"},
        {"lfm25-vl-3b", "ocr", "b8000", "q4_k_m,q8_0", "ctx-size,n-predict,cache-type-k,cache-type-v,special"},
        {"qwen3.5-4b", "check", "b4000", "q8_0,q4_k_xl", "ctx-size,n-predict,cache-type-k,cache-type-v"},
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

        // Exactly the one role this model serves — a stray second role means a
        // profile was pasted from the wrong file.
        QCOMPARE(profile->roles.keys(), QStringList{QString::fromUtf8(e.role)});

        const ModelProfiles::Role *role = ModelProfiles::roleFor(*profile, QString::fromUtf8(e.role));
        QVERIFY2(role, qPrintable(id));
        QStringList launch;
        for (const LaunchParameter &p : role->launch)
            launch.append(p.name);
        QCOMPARE(launch.join(QLatin1Char(',')), QString::fromUtf8(e.launch));
    }
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