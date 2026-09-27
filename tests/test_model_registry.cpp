#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

#include "runtime/InstalledReconcile.h"
#include "runtime/ModelRegistry.h"

using namespace llocr;

namespace {

// Creates a fake repo subdir under `modelsDir` with a couple of GGUF files and
// returns the absolute dir path.
QString makeRepoDir(const QString &modelsDir, const QString &name)
{
    const QString dir = QDir(modelsDir).filePath(name);
    QDir().mkpath(dir);
    QFile a(QDir(dir).filePath(QStringLiteral("model-Q4_K_M.gguf")));
    a.open(QIODevice::WriteOnly);
    a.write("GGUF placeholder");
    a.close();
    return dir;
}

QString corruptIndex(const QString &modelsDir)
{
    const QString path = ModelRegistry::indexPathFor(modelsDir);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{ not valid json ");
    f.close();
    return path;
}

// Writes a placeholder GGUF and returns its absolute path.
QString writeFile(const QString &dir, const QString &name)
{
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write("GGUF placeholder");
    f.close();
    return path;
}

}  // namespace

class TestModelRegistry : public QObject
{
    Q_OBJECT

private slots:
    void rescansWhenIndexMissing();
    void scansMultiQuantAsSeparateEntries();
    void rebuildsOnCorruptIndex();
    void atomicWriteRoundtrip();
    void persistsRoles();
    void persistsExplicitDefaultCtxSize();
    void recoversFromTruncatedIndex();
    void assertsRegistryLock();
    void refusesExternalDelete();
    void refusesActiveDeleteWhileReady();
    void allowsManagedDeleteWhenNotActive();
    void canonicalPathCheck();
    void refusesSymlinkEscapeFromModelsDir();

    // The reconciliation policy (ADR 116): what each of the three sources of
    // truth is allowed to decide.
    void dropsEntriesWhoseFilesAreGone();
    void keepsIndexMetadataAndRefreshesFileFacts();
    void keepsProjectorPairingAndRecordsItsAbsence();
    void reportsStaleSelectionsWithoutDroppingThem();
    void anUnreadableModelsDirectoryIsNotAnEmptyOne();
    void reportsWhatItReconciled();
};

void TestModelRegistry::rescansWhenIndexMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeRepoDir(dir.path(), QStringLiteral("org__repo"));

    bool rebuilt = false;
    QString err;
    const QList<ModelEntry> entries = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(rebuilt);
    QVERIFY(err.isEmpty());
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).origin, ModelOrigin::Managed);
    QVERIFY(entries.at(0).modelPath.endsWith(QStringLiteral("model-Q4_K_M.gguf")));
    QCOMPARE(entries.at(0).quantization, QStringLiteral("Q4_K_M"));
}

void TestModelRegistry::scansMultiQuantAsSeparateEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = makeRepoDir(dir.path(), QStringLiteral("org__repo"));

    auto writeFile = [&](const QString &name) {
        const QString p = QDir(sub).filePath(name);
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("GGUF placeholder");
        f.close();
    };
    // A second quant plus its split part, and the shared vision projector.
    writeFile(QStringLiteral("model-Q8_0-00001-of-00002.gguf"));
    writeFile(QStringLiteral("model-Q8_0-00002-of-00002.gguf"));
    writeFile(QStringLiteral("mmproj-model-F16.gguf"));

    const QList<ModelEntry> entries = ModelRegistry::scanModelsDir(dir.path());
    QCOMPARE(entries.size(), 2);

    bool sawQ4 = false;
    bool sawQ8 = false;
    for (const ModelEntry &e : entries) {
        QVERIFY(e.mmprojPath.endsWith(QStringLiteral("mmproj-model-F16.gguf")));
        if (e.quantization == QLatin1String("Q4_K_M")) {
            sawQ4 = true;
            QVERIFY(e.parts.isEmpty());
            QVERIFY(e.modelPath.endsWith(QStringLiteral("model-Q4_K_M.gguf")));
            QVERIFY(e.id == QStringLiteral("org__repo_Q4_K_M"));
        } else if (e.quantization == QLatin1String("Q8_0")) {
            sawQ8 = true;
            QCOMPARE(e.parts.size(), 1);
            QVERIFY(e.modelPath.endsWith(
                QStringLiteral("model-Q8_0-00001-of-00002.gguf")));
            QVERIFY(e.parts.at(0).endsWith(
                QStringLiteral("model-Q8_0-00002-of-00002.gguf")));
            QVERIFY(e.id == QStringLiteral("org__repo_Q8_0"));
        } else {
            QFAIL(qPrintable("unexpected quantization " + e.quantization));
        }
    }
    QVERIFY(sawQ4);
    QVERIFY(sawQ8);
}

void TestModelRegistry::rebuildsOnCorruptIndex()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    corruptIndex(dir.path());

    bool rebuilt = false;
    QString err;
    const QList<ModelEntry> entries = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(rebuilt);
    QVERIFY(!err.isEmpty());   // "corrupt; rescanning"
    QCOMPARE(entries.size(), 1);  // recovered from disk
}

// The role tags ("ocr" / "check") recorded at install time survive a
// save/load roundtrip — the role-filtered installed lists rely on them.
void TestModelRegistry::persistsRoles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelEntry e;
    e.id = QStringLiteral("org__repo");
    e.title = QStringLiteral("Repo");
    e.dir = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    e.modelPath = QDir(e.dir).filePath(QStringLiteral("model-Q4_K_M.gguf"));
    e.origin = ModelOrigin::Managed;
    e.roles = {QStringLiteral("ocr"), QStringLiteral("check")};

    QString err;
    QVERIFY(ModelRegistry::save(dir.path(), {e}, err));
    QVERIFY(err.isEmpty());

    bool rebuilt = false;
    const QList<ModelEntry> loaded = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(!rebuilt);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).roles,
             QStringList({QStringLiteral("ocr"), QStringLiteral("check")}));

    // A legacy entry without roles stays empty (visible in both lists). Its
    // file has to exist: the reconciliation drops an entry whose files are gone
    // (see dropsEntriesWhoseFilesAreGone), and that is a separate test.
    QFile old(QDir(e.dir).filePath(QStringLiteral("old-Q4_K_M.gguf")));
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write("GGUF placeholder");
    old.close();

    ModelEntry legacy;
    legacy.id = QStringLiteral("legacy");
    legacy.dir = e.dir;
    legacy.modelPath = old.fileName();
    legacy.modelPath = QDir(e.dir).filePath(QStringLiteral("old-Q4_K_M.gguf"));
    legacy.origin = ModelOrigin::Managed;
    QVERIFY2(ModelRegistry::save(dir.path(), {e, legacy}, err), qPrintable(err));
    const QList<ModelEntry> reloaded = ModelRegistry::load(dir.path(), rebuilt, err);
    QCOMPARE(reloaded.size(), 2);
    for (const ModelEntry &x : reloaded) {
        if (x.id == QStringLiteral("legacy"))
            QVERIFY(x.roles.isEmpty());
        else
            QCOMPARE(x.roles,
                     QStringList({QStringLiteral("ocr"), QStringLiteral("check")}));
    }
}

void TestModelRegistry::atomicWriteRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelEntry e;
    e.id = QStringLiteral("org__repo");
    e.title = QStringLiteral("Repo");
    e.repo = QStringLiteral("org/repo");
    e.dir = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    e.modelPath = QDir(e.dir).filePath(QStringLiteral("model-Q4_K_M.gguf"));
    e.origin = ModelOrigin::Managed;
    e.byteSize = 1024;

    QString err;
    QVERIFY(ModelRegistry::save(dir.path(), {e}, err));
    QVERIFY(err.isEmpty());

    bool rebuilt = false;
    const QList<ModelEntry> loaded = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(!rebuilt);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).id, QStringLiteral("org__repo"));
    // byteSize is a fact about the file, so the reconciliation refreshes it
    // from disk rather than trusting the recorded value (ADR 116).
    QCOMPARE(loaded.at(0).byteSize, QFileInfo(loaded.at(0).modelPath).size());
}

void TestModelRegistry::persistsExplicitDefaultCtxSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A ctx size of 8192 must not be conflated with "unspecified" (review
    // 2.8): when chosen explicitly it is persisted, so a reload does not lose it
    // to a fallback default.
    ModelEntry e;
    e.id = QStringLiteral("org__repo");
    e.title = QStringLiteral("Repo");
    e.dir = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    e.modelPath = QDir(e.dir).filePath(QStringLiteral("model-Q4_K_M.gguf"));
    e.origin = ModelOrigin::Managed;
    e.byteSize = 1024;
    e.ctxSize = 8192;
    e.ctxSizeSet = true;

    QString err;
    QVERIFY(ModelRegistry::save(dir.path(), {e}, err));
    QVERIFY(err.isEmpty());

    // The explicit value (which equals the default) is written, not skipped.
    QFile idx(ModelRegistry::indexPathFor(dir.path()));
    QVERIFY(idx.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(idx.readAll());
    const QJsonArray arr = doc.object().value(QStringLiteral("models")).toArray();
    QVERIFY(arr.size() == 1);
    QCOMPARE(arr.at(0).toObject().value(QStringLiteral("ctxSize")).toInt(0), 8192);

    bool rebuilt = false;
    const QList<ModelEntry> loaded = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(!rebuilt);
    QVERIFY(err.isEmpty());
    QCOMPARE(loaded.size(), 1);
    QVERIFY(loaded.at(0).ctxSizeSet);
    QCOMPARE(loaded.at(0).ctxSize, 8192);
}

void TestModelRegistry::recoversFromTruncatedIndex()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    // A syntactically-open but truncated index must trigger a rescan, not a
    // TypeError crash or a lost registry.
    const QString path = ModelRegistry::indexPathFor(dir.path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{ \"schemaVersion\": 1, \"models\": ");
    f.close();

    bool rebuilt = false;
    QString err;
    const QList<ModelEntry> entries = ModelRegistry::load(dir.path(), rebuilt, err);
    QVERIFY(rebuilt);
    QVERIFY(!err.isEmpty());
    QCOMPARE(entries.size(), 1);  // recovered from the on-disk model
}

void TestModelRegistry::assertsRegistryLock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    // Simulate a concurrent writer: hold the per-write registry lock (§ H.6 /
    // ADR 46) so a second save must refuse rather than silently interleave.
    QLockFile holder(ModelRegistry::lockPathFor(dir.path()));
    QVERIFY(holder.lock());

    ModelEntry e;
    e.id = QStringLiteral("org__repo");
    e.title = QStringLiteral("Repo");
    e.dir = QDir(dir.path()).filePath(QStringLiteral("org__repo"));
    QString err;
    QVERIFY(!ModelRegistry::save(dir.path(), {e}, err));
    QVERIFY(!err.isEmpty());
}

void TestModelRegistry::refusesExternalDelete()
{
    ModelEntry e;
    e.origin = ModelOrigin::External;
    const QString reason = ModelRegistry::removalError(
        e, QStringLiteral("/tmp/models"), false, false);
    QVERIFY(!reason.isEmpty());
}

void TestModelRegistry::refusesActiveDeleteWhileReady()
{
    ModelEntry e;
    e.origin = ModelOrigin::Managed;
    e.modelPath = QStringLiteral("/models/org__repo/model.gguf");
    const QString reason = ModelRegistry::removalError(
        e, QStringLiteral("/models"), true /*active*/, true /*ready*/);
    QVERIFY(!reason.isEmpty());
    QVERIFY(reason.contains(QStringLiteral("in use")));
}

void TestModelRegistry::allowsManagedDeleteWhenNotActive()
{
    ModelEntry e;
    e.origin = ModelOrigin::Managed;
    e.modelPath = QStringLiteral("/models/org__repo/model.gguf");
    const QString reason = ModelRegistry::removalError(
        e, QStringLiteral("/models"), false, false);
    QVERIFY(reason.isEmpty());
}

void TestModelRegistry::canonicalPathCheck()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    const QString modelPath = QDir(sub).filePath(QStringLiteral("model-Q4_K_M.gguf"));
    const QString canon = ModelRegistry::canonicalPath(modelPath);
    QVERIFY(!canon.isEmpty());
    QVERIFY(canon.endsWith(QStringLiteral("model-Q4_K_M.gguf")));
}

void TestModelRegistry::refusesSymlinkEscapeFromModelsDir()
{
    // Symbolic links are the test vehicle: Canonical path resolution walks the
    // link out of modelsDir. On Windows without Developer Mode / admin rights
    // symlink creation fails with WinError 1314 (QFile::link can't create one),
    // so the scenario cannot be reproduced there — guard it like the other
    // symlink-dependent tests.
#ifdef Q_OS_UNIX
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // modelsDir is a dedicated subdir; the symlink target lives OUTSIDE it but
    // still inside the temp root, so only the guard decides the boundary.
    const QString modelsDir = QDir(dir.path()).filePath(QStringLiteral("models"));
    QVERIFY(QDir().mkpath(modelsDir));
    makeRepoDir(modelsDir, QStringLiteral("org__repo"));

    // A real symlink inside modelsDir pointing at a directory outside it.
    const QString outsideTarget = QDir(dir.path()).filePath(QStringLiteral("outside_target"));
    QVERIFY(QDir().mkpath(outsideTarget));
    QFile gf(QDir(outsideTarget).filePath(QStringLiteral("model.gguf")));
    QVERIFY(gf.open(QIODevice::WriteOnly));
    gf.write("GGUF placeholder");
    gf.close();
    const QString linkDir = QDir(modelsDir).filePath(QStringLiteral("escaped__repo"));
    // QFile::link(source, linkName) makes `linkDir` point to `outsideTarget`.
    QVERIFY2(QFile::link(outsideTarget, linkDir), "failed to create symlink");

    ModelEntry e;
    e.origin = ModelOrigin::Managed;
    e.modelPath = QDir(linkDir).filePath(QStringLiteral("model.gguf"));
    const QString reason =
        ModelRegistry::removalError(e, modelsDir, /*active=*/false,
                                    /*runtimeReady=*/false);
    // Canonical resolution walks the symlink out of modelsDir, so removal must
    // be refused, not silently allowed to escape.
    QVERIFY(!reason.isEmpty());
    QVERIFY(reason.contains(QStringLiteral("outside")));
#endif
}

// A model the user deleted by hand stayed in the list and offered an «Activate»
// that could only fail with «Model file not found»: the merge only ever *added*
// to the index, it never dropped from it (ADR 116).
void TestModelRegistry::dropsEntriesWhoseFilesAreGone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repo = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    const QString model = QDir(repo).filePath(QStringLiteral("model-Q4_K_M.gguf"));

    ModelEntry kept;
    kept.id = QStringLiteral("org__repo");
    kept.dir = repo;
    kept.modelPath = model;
    kept.origin = ModelOrigin::Managed;

    ModelEntry phantom = kept;
    phantom.id = QStringLiteral("org__gone");
    phantom.modelPath = QDir(repo).filePath(QStringLiteral("deleted-Q4_K_M.gguf"));
    QVERIFY(!QFileInfo::exists(phantom.modelPath));

    ReconcileInput input;
    input.index = {kept, phantom};
    input.disk = ModelRegistry::scanModelsDir(dir.path());
    QCOMPARE(input.disk.size(), 1);  // only the surviving model is on disk

    const ReconcileResult result = reconcileInstalled(input);
    QCOMPARE(result.models.size(), 1);
    QCOMPARE(result.models.first().id, QStringLiteral("org__repo"));
    QCOMPARE(result.dropped, 1);
    QCOMPARE(result.added, 0);
    QVERIFY(result.indexChanged);

    // …and the index is rewritten, so the drop is not repeated forever.
    QString err;
    QVERIFY2(ModelRegistry::save(dir.path(), result.models, err), qPrintable(err));
    bool rebuilt = false;
    QCOMPARE(ModelRegistry::load(dir.path(), rebuilt, err).size(), 1);
}

// Two sources for one entry: the index knows the curated values, the filesystem
// knows the size. Neither may overwrite the other.
void TestModelRegistry::keepsIndexMetadataAndRefreshesFileFacts()
{
    // The file has to exist: an entry whose files are gone is dropped, which is
    // the previous test's rule and would mask the one under test here.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString model = writeFile(dir.path(), QStringLiteral("a-Q4_K_M.gguf"));
    QVERIFY(!model.isEmpty());

    ModelEntry recorded;
    recorded.id = QStringLiteral("org__repo");
    recorded.modelPath = model;
    recorded.dir = dir.path();
    recorded.revision = QStringLiteral("deadbeef");
    recorded.license = QStringLiteral("apache-2.0");
    recorded.roles = {QStringLiteral("check")};
    recorded.parser = QStringLiteral("det_tokens");
    recorded.prompt = QStringLiteral("parse the page");
    recorded.addedAt = QStringLiteral("2026-01-01T00:00:00Z");
    recorded.byteSize = 999999;  // stale on purpose

    ModelEntry scanned = recorded;
    scanned.byteSize = 16;

    ReconcileInput input;
    input.index = {recorded};
    input.disk = {scanned};
    const ReconcileResult result = reconcileInstalled(input);
    QCOMPARE(result.added, 0);  // matched, not re-added
    QCOMPARE(result.models.size(), 1);
    const ModelEntry &merged = result.models.first();
    // The filesystem has the newer answer…
    QCOMPARE(merged.byteSize, qint64(16));
    QCOMPARE(merged.quantization, recorded.quantization);  // part of the id
    // …and only for what it can actually know.
    QCOMPARE(merged.revision, QStringLiteral("deadbeef"));
    QCOMPARE(merged.license, QStringLiteral("apache-2.0"));
    QCOMPARE(merged.roles, QStringList({QStringLiteral("check")}));
    QCOMPARE(merged.parser, QStringLiteral("det_tokens"));
    QCOMPARE(merged.prompt, QStringLiteral("parse the page"));
    QCOMPARE(merged.addedAt, QStringLiteral("2026-01-01T00:00:00Z"));
    QCOMPARE(result.refreshed, 1);
}

// The scan attaches the first projector it finds in a folder to every model in
// it, so it must not be allowed to invent a pairing the index does not have —
// neither an existing one nor a missing one. (A model that "acquires" a
// projector it was not installed with moves from the check list to the
// recognition list, which is a user-visible miscategorisation.)
void TestModelRegistry::keepsProjectorPairingAndRecordsItsAbsence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A directory of its own: makeRepoDir() would add a third model.
    const QString repo = QDir(dir.path()).filePath(QStringLiteral("org__repo"));
    QVERIFY(QDir().mkpath(repo));
    const QString mmproj = writeFile(repo, QStringLiteral("mmproj-F16.gguf"));
    const QString pairedModel = writeFile(repo, QStringLiteral("a-Q4_K_M.gguf"));
    const QString unpairedModel = writeFile(repo, QStringLiteral("b-Q4_K_M.gguf"));
    QVERIFY(!mmproj.isEmpty() && !pairedModel.isEmpty() && !unpairedModel.isEmpty());

    ModelEntry paired;
    paired.id = QStringLiteral("paired");
    paired.dir = repo;
    paired.modelPath = pairedModel;
    paired.mmprojPath = mmproj;
    paired.origin = ModelOrigin::Managed;

    ModelEntry unpaired;
    unpaired.id = QStringLiteral("unpaired");
    unpaired.dir = repo;
    unpaired.modelPath = unpairedModel;
    unpaired.origin = ModelOrigin::Managed;

    ReconcileInput input;
    input.index = {paired, unpaired};
    input.disk = ModelRegistry::scanModelsDir(dir.path());
    QCOMPARE(input.disk.size(), 2);
    // The scan really does attach the same projector to both.
    QCOMPARE(input.disk.at(0).mmprojPath, mmproj);
    QCOMPARE(input.disk.at(1).mmprojPath, mmproj);

    const ReconcileResult result = reconcileInstalled(input);
    QCOMPARE(result.models.size(), 2);
    QCOMPARE(result.models.at(0).mmprojPath, mmproj);    // pairing kept
    QVERIFY(result.models.at(1).mmprojPath.isEmpty());  // absence kept
}

// The settings are pointers, not membership: a model that is selected but gone
// is reported so the caller can say something, not silently dropped.
void TestModelRegistry::reportsStaleSelectionsWithoutDroppingThem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repo = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    const QString model = QDir(repo).filePath(QStringLiteral("model-Q4_K_M.gguf"));

    ModelEntry entry;
    entry.id = QStringLiteral("org__repo");
    entry.dir = repo;
    entry.modelPath = model;
    entry.origin = ModelOrigin::Managed;

    ReconcileInput input;
    input.index = {entry};
    input.disk = ModelRegistry::scanModelsDir(dir.path());
    input.selectedModelPath = model;
    input.selectedCheckModelPath = QDir(repo).filePath(QStringLiteral("gone.gguf"));
    input.selectedServerPath = QDir(dir.path()).filePath(QStringLiteral("llama-server"));
    input.selectedServerExists = false;

    const ReconcileResult result = reconcileInstalled(input);
    QCOMPARE(result.models.size(), 1);
    QVERIFY(!result.staleModelSelections.contains(model));
    QCOMPARE(result.staleModelSelections.size(), 1);
    QVERIFY(result.staleModelSelections.first().endsWith(QStringLiteral("gone.gguf")));
    QVERIFY(result.staleServerSelection);

    // A selection that resolves is not reported.
    input.selectedServerExists = true;
    QVERIFY(!reconcileInstalled(input).staleServerSelection);
}

// A models directory on an unmounted drive reads as "nothing there". Treated as
// an empty directory, every model would be reported deleted and the rewritten
// index would lose them for good.
void TestModelRegistry::anUnreadableModelsDirectoryIsNotAnEmptyOne()
{
    ModelEntry entry;
    entry.id = QStringLiteral("org__repo");
    entry.origin = ModelOrigin::Managed;

    ReconcileInput input;
    input.index = {entry};
    input.diskAvailable = false;
    input.selectedModelPath = QStringLiteral("/gone/model.gguf");

    const ReconcileResult result = reconcileInstalled(input);
    QVERIFY(result.diskUnavailable);
    QCOMPARE(result.models.size(), 1);
    QCOMPARE(result.dropped, 0);
    QCOMPARE(result.added, 0);
    QVERIFY(!result.indexChanged);
    // With no filesystem to consult, a selection cannot be called stale.
    QVERIFY(result.staleModelSelections.isEmpty());
}

// The report is what tells the caller — and the user — what happened.
void TestModelRegistry::reportsWhatItReconciled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repo = makeRepoDir(dir.path(), QStringLiteral("org__repo"));
    const QString kept = QDir(repo).filePath(QStringLiteral("model-Q4_K_M.gguf"));
    // A second repo only the scan knows about: a manual copy, or an install
    // interrupted before the index was written.
    const QString copied = makeRepoDir(dir.path(), QStringLiteral("org__other"));

    ModelEntry entry;
    entry.id = QStringLiteral("org__repo");
    entry.dir = repo;
    entry.modelPath = kept;
    entry.origin = ModelOrigin::Managed;

    ReconcileInput input;
    input.index = {entry};
    input.disk = ModelRegistry::scanModelsDir(dir.path());
    QCOMPARE(input.disk.size(), 2);

    const ReconcileResult result = reconcileInstalled(input);
    QCOMPARE(result.added, 1);
    QCOMPARE(result.dropped, 0);
    QCOMPARE(result.models.size(), 2);
    // Index order first, then what the scan contributed.
    QCOMPARE(result.models.first().id, QStringLiteral("org__repo"));
    QCOMPARE(QFileInfo(result.models.last().dir).canonicalFilePath(),
             QFileInfo(copied).canonicalFilePath());
}

QTEST_MAIN(TestModelRegistry)
#include "test_model_registry.moc"