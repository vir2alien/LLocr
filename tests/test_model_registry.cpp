#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

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

}  // namespace

class TestModelRegistry : public QObject
{
    Q_OBJECT

private slots:
    void rescansWhenIndexMissing();
    void rebuildsOnCorruptIndex();
    void atomicWriteRoundtrip();
    void recoversFromTruncatedIndex();
    void assertsRegistryLock();
    void refusesExternalDelete();
    void refusesActiveDeleteWhileReady();
    void allowsManagedDeleteWhenNotActive();
    void canonicalPathCheck();
    void refusesSymlinkEscapeFromModelsDir();
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
    QCOMPARE(loaded.at(0).byteSize, qint64(1024));
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
}

QTEST_MAIN(TestModelRegistry)
#include "test_model_registry.moc"