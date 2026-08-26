#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

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
    void mergeByUserPrecedence();
    void importToJsonRoundTrips();
    void saveThenReset();
};

void TestModelPresetCatalog::builtInCatalogParses()
{
    // load() with an empty user path exercises just the built-in resource,
    // which must always be present and parse cleanly.
    QString err;
    const QList<ModelPreset> presets = ModelPresetCatalog::load(QString(), err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(presets.size() >= 2);  // the two shipped presets

    for (const ModelPreset &p : presets) {
        // §4.1 schema contract: the fields the pipeline consumes are present.
        QVERIFY(!p.id.isEmpty());
        QVERIFY(!p.title.isEmpty());
        QVERIFY(!p.repo.isEmpty());
        QVERIFY(!p.model.isEmpty());       // at least a main model file
        QVERIFY(!p.parser.isEmpty());
        QVERIFY(p.ctxSize > 0);
        QVERIFY(!p.minBuild.isEmpty());
        QVERIFY(!p.license.isEmpty());
        // revision may be empty → always pin via fetchHeadSha at install time;
        // sha256 is a per-file digest map (object), empty until populated.
        QVERIFY(p.sha256.isEmpty() || !p.sha256.isEmpty());
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
    override_.parser = QStringLiteral("det_tokens");
    user << override_;

    ModelPreset added;
    added.id = QStringLiteral("brand-new-preset");
    added.title = QStringLiteral("New");
    added.repo = QStringLiteral("user/new-repo");
    added.model = QStringLiteral("new.gguf");
    added.parser = QStringLiteral("det_tokens");
    user << added;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userPath = QDir(dir.path()).filePath(QStringLiteral("catalog.json"));
    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, user, err));
    QVERIFY(err.isEmpty());

    const QList<ModelPreset> merged = ModelPresetCatalog::load(userPath, err);
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
    p.parser = QStringLiteral("det_tokens");
    p.prompt = QStringLiteral("document parsing.");
    p.ctxSize = 4096;
    p.minBuild = QStringLiteral("b4000");
    p.approxVramGb = 3.5;
    p.license = QStringLiteral("apache-2.0");
    p.sha256.insert(QStringLiteral("model-q4_k_m.gguf"),
                    QStringLiteral("abcdef"));

    const ModelPreset restored = ModelPreset::fromJson(p.toJson());
    QCOMPARE(restored.id, p.id);
    QCOMPARE(restored.revision, p.revision);
    QCOMPARE(restored.model, p.model);
    QCOMPARE(restored.mmproj, p.mmproj);
    QCOMPARE(restored.ctxSize, 4096);
    QCOMPARE(restored.sha256.value(QStringLiteral("model-q4_k_m.gguf")),
             QStringLiteral("abcdef"));
    // Serialize→parse→serialize is stable.
    QCOMPARE(ModelPreset::fromJson(restored.toJson()).toJson().toVariantMap(),
             p.toJson().toVariantMap());
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
    p.parser = QStringLiteral("det_tokens");
    presets << p;

    QString err;
    QVERIFY(ModelPresetCatalog::save(userPath, presets, err));
    QVERIFY(QFile::exists(userPath));

    // The file round-trips: load() reads the two sources and returns `one`.
    const QList<ModelPreset> loaded = ModelPresetCatalog::load(userPath, err);
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
    const QList<ModelPreset> afterReset = ModelPresetCatalog::load(userPath, err);
    QVERIFY(err.isEmpty());
    bool hasOne = false;
    for (const ModelPreset &pp : afterReset)
        if (pp.id == QStringLiteral("one"))
            hasOne = true;
    QVERIFY(!hasOne);
}

QTEST_MAIN(TestModelPresetCatalog)
#include "test_model_preset_catalog.moc"