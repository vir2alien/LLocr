#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "runtime/RuntimeLocator.h"
#include "runtime/ServerCapabilities.h"

using namespace llocr;

// Path to the mock_llama_server test double, injected by tests/CMakeLists.txt
// as a compile definition (LLOCR_MOCK_SERVER).
#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

class TestRuntimeLocator : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        // Never leak the marker into other tests in this process: children
        // spawned without the variable must not write sidecar files.
        ::qunsetenv("LLOCR_MOCK_MARKER");
    }

    void probeValidBinary();
    void probeAcceptsRenamedBinary();
    void missingFileReports();
    void emptyPathReports();
    void belowMinimumDetect();
    void cacheInvalidatesOnFileChange();
    void cacheHoldsSingleEntry();
    void diskCacheSkipsReProbeOnUnchanged();
    void diskCacheIgnoresJunkFile();
};

static QString mockPath()
{
    return QString::fromUtf8(LLOCR_MOCK_SERVER);
}

void TestRuntimeLocator::probeValidBinary()
{
    const ProbeResult r = RuntimeLocator::probe(mockPath());
    QVERIFY(r.ok);
    QVERIFY(r.capabilities.ok);
    QVERIFY(!r.capabilities.belowMinimum);
    QCOMPARE(r.capabilities.build, QStringLiteral("b10594"));
    QVERIFY(r.error.isEmpty());
}

void TestRuntimeLocator::probeAcceptsRenamedBinary()
{
    // Copy the helper to a non-standard name; the probe must accept it purely
    // on behaviour, never on a "llama-server" name substring (ADR 41 note).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString renamed = QStringLiteral("%1/my-octopus-server").arg(dir.path());
    QVERIFY(QFile::copy(mockPath(), renamed));

    const ProbeResult r = RuntimeLocator::probe(renamed);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.capabilities.ok);

    // If the binary is runnable here the mock build is detected.
    if (r.ok)
        QCOMPARE(r.capabilities.build, QStringLiteral("b10594"));
}

void TestRuntimeLocator::missingFileReports()
{
    const ProbeResult r = RuntimeLocator::probe(QStringLiteral("/no/such/llama-server"));
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestRuntimeLocator::emptyPathReports()
{
    const ProbeResult r = RuntimeLocator::probe(QStringLiteral("   "));
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestRuntimeLocator::belowMinimumDetect()
{
    const ServerCapabilities caps = ServerCapabilities::detect(QStringLiteral("b3999"));
    QVERIFY(caps.ok);
    QVERIFY(caps.belowMinimum);
}

void TestRuntimeLocator::cacheInvalidatesOnFileChange()
{
    // §H.7 probe cache (probeCached): probing an unchanged file reuses the
    // cached result; once the file's mtime/size change the cache must miss and
    // the fresh probe reflects the new content.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = QStringLiteral("%1/replaced-server").arg(dir.path());
    // 1) First probe: a real mock binary → ok.
    QVERIFY(QFile::copy(mockPath(), target));
    QVERIFY(RuntimeLocator::probeCached(target).ok);

#ifdef Q_OS_UNIX
    // 2) Cache-hit proof: remove the owner-execute bit without touching mtime/size
    //    (chmod changes ctime, not mtime; size is unchanged). A fresh probe would
    //    now fail to exec, so probeCached returning ok proves it was served from
    //    the cache and did not re-spawn the binary.
    QFile::setPermissions(target, QFile::permissions(target) & ~QFileDevice::ExeUser);
    QVERIFY(RuntimeLocator::probeCached(target).ok);
#endif

    // 3) Overwrite with different content (size differs → cache miss) → the
    //    next probeCached must go through the binary, not the cache, and
    //    reflect the new (invalid) content.
    QFile f(target);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("build: 99999 (b99999)\n") > 0);
    f.close();
    const ProbeResult r2 = RuntimeLocator::probeCached(target);
    QVERIFY(!r2.ok);  // not a valid llama-server
    QVERIFY(!r2.error.isEmpty());
}

void TestRuntimeLocator::cacheHoldsSingleEntry()
{
    // Review 3.6: the probe cache is a single entry. Probing a *different*
    // binary evicts the previous key, so coming back to the first path
    // re-probes instead of serving the stale entry. To observe re-probes we
    // use the mock's LLOCR_MOCK_MARKER env hook: every spawn appends a line to
    // the given file, so a cache hit (no spawn) does not grow the counter.
    // (Mirrors the old Unix shell-wrapper trick, which cannot run on Windows.)
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mock = QFileInfo(mockPath()).absoluteFilePath();

    auto makeBinary = [&dir, &mock](const QString &path) {
        QVERIFY2(QFile::copy(mock, path), "copy mock failed");
    };
    auto probeCount = [](const QString &log) -> int {
        QFile f(log);
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        const QByteArray data = f.readAll();
        f.close();
        return int(data.count('\n'));
    };
    auto setMarker = [](const QString &log) {
        // The probe spawns --version/--help via a fresh QProcess that inherits
        // the current process environment, so the child mock records its own
        // start in `log` (see mock_llama_server.cpp). qputenv keeps the string
        // alive for putenv-style access (unlike ::putenv + constData()).
        ::qputenv("LLOCR_MOCK_MARKER", log.toUtf8());
    };

    const QString logA = QStringLiteral("%1/a.log").arg(dir.path());
    const QString logB = QStringLiteral("%1/b.log").arg(dir.path());
    const QString pathA = QStringLiteral("%1/server-a").arg(dir.path());
    const QString pathB = QStringLiteral("%1/server-b").arg(dir.path());
    makeBinary(pathA);
    makeBinary(pathB);

    setMarker(logA);
    QVERIFY(RuntimeLocator::probeCached(pathA).ok);
    QCOMPARE(probeCount(logA), 2);  // --version + --help for the fresh probe
    setMarker(logB);
    QVERIFY(RuntimeLocator::probeCached(pathB).ok);
    QCOMPARE(probeCount(logB), 2);

    // A was evicted by B: probing A again must re-probe (2 more marker lines).
    // A 4-slot cache would still hold A and return it without a re-probe.
    setMarker(logA);
    QVERIFY(RuntimeLocator::probeCached(pathA).ok);
    QCOMPARE(probeCount(logA), 4);
}

void TestRuntimeLocator::diskCacheSkipsReProbeOnUnchanged()
{
    // The persistent capabilities cache (ServerCapabilities::cacheFileName)
    // must serve probeCached(binary, cacheDir) without re-spawning the binary,
    // and invalidate when the file changes (path+mtime+size key). Invocations
    // are counted through the mock's LLOCR_MOCK_MARKER env hook (see
    // cacheHoldsSingleEntry) so a cache hit shows as an unchanged count.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mock = QFileInfo(mockPath()).absoluteFilePath();
    const QString cacheDir = QStringLiteral("%1/cache").arg(dir.path());
    const QString logA = QStringLiteral("%1/a.log").arg(dir.path());
    const QString logB = QStringLiteral("%1/b.log").arg(dir.path());
    const QString pathA = QStringLiteral("%1/server-a").arg(dir.path());
    const QString pathB = QStringLiteral("%1/server-b").arg(dir.path());
    QVERIFY2(QFile::copy(mock, pathA), "copy mock A failed");
    QVERIFY2(QFile::copy(mock, pathB), "copy mock B failed");

    auto probeCount = [](const QString &log) -> int {
        QFile f(log);
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        const QByteArray data = f.readAll();
        f.close();
        return int(data.count('\n'));
    };
    auto setMarker = [](const QString &log) {
        ::qputenv("LLOCR_MOCK_MARKER", log.toUtf8());
    };

    // 1) Fresh probe writes the disk cache entry.
    setMarker(logA);
    QVERIFY(RuntimeLocator::probeCached(pathA, cacheDir).ok);
    QCOMPARE(probeCount(logA), 2);  // --version + --help
    QVERIFY(QFile::exists(ServerCapabilities::cacheFileName(cacheDir, pathA)));

    // 2) Probe another path to evict the single in-memory slot.
    setMarker(logB);
    QVERIFY(RuntimeLocator::probeCached(pathB, cacheDir).ok);

    // 3) Back to A: the in-memory slot is gone, but the disk cache must answer
    //    without spawning again (invocation count stays 2).
    setMarker(logA);
    QVERIFY(RuntimeLocator::probeCached(pathA, cacheDir).ok);
    QCOMPARE(probeCount(logA), 2);

    // 4) Change A's content (size differs → new cache key) → memory and disk
    //    miss → the next probe goes through the binary, which now fails to
    //    start (a copy of the mock that was overwritten with junk). The exact
    //    marker count is not asserted here: a failed spawn never runs the mock
    //    body on Windows (CreateProcess rejects non-.exe), while the old Unix
    //    shell wrapper appended before exec — so only the !ok outcome is stable
    //    across platforms.
    {
        QFile f(pathA);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(f.write("not an executable anymore\n") > 0);
        f.close();
    }
    setMarker(logA);
    QVERIFY(!RuntimeLocator::probeCached(pathA, cacheDir).ok);
}

void TestRuntimeLocator::diskCacheIgnoresJunkFile()
{
    // A corrupt/foreign file at the cache entry name must be ignored (treated
    // as a miss), re-probed, and replaced with a valid entry.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mock = QFileInfo(mockPath()).absoluteFilePath();
    const QString cacheDir = QStringLiteral("%1/cache").arg(dir.path());
    const QString path = QStringLiteral("%1/server").arg(dir.path());
    QVERIFY2(QFile::copy(mock, path), "copy mock failed");

    const QString entryName = ServerCapabilities::cacheFileName(cacheDir, path);
    // The cache dir exists only after the first successful write (writeDiskCache
    // mkpaths it), so create it here to plant the junk entry.
    QVERIFY(QDir().mkpath(cacheDir));
    QFile junk(entryName);
    QVERIFY(junk.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(junk.write("this is not json") > 0);
    junk.close();

    QVERIFY(RuntimeLocator::probeCached(path, cacheDir).ok);
    QFile check(entryName);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QVERIFY(QJsonDocument::fromJson(check.readAll()).isObject());
}

QTEST_MAIN(TestRuntimeLocator)
#include "test_runtime_locator.moc"