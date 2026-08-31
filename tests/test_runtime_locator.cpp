#include <QFile>
#include <QFileInfo>
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
    void probeValidBinary();
    void probeAcceptsRenamedBinary();
    void missingFileReports();
    void emptyPathReports();
    void belowMinimumDetect();
    void cacheInvalidatesOnFileChange();
    void cacheHoldsSingleEntry();
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
    // re-probes instead of serving the stale entry. We can't observe the
    // difference through the ProbeResult (identical binaries), so each path is
    // a wrapper script that appends a marker line per invocation and delegates
    // to the real mock: a re-probe grows the counter, a cache hit does not.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mock = QFileInfo(mockPath()).absoluteFilePath();

    auto writeWrapper = [&dir, &mock](const QString &path, const QString &log) {
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray body = QStringLiteral("#!/bin/sh\necho x >> '%1'\nexec '%2' \"$@\"\n")
                                    .arg(log, mock)
                                    .toUtf8();
        QVERIFY(script.write(body) > 0);
        script.close();
        QVERIFY(QFile::setPermissions(
            path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                      | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                      | QFileDevice::ReadOther | QFileDevice::ExeOther));
    };
    auto probeCount = [](const QString &log) -> int {
        QFile f(log);
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        const QByteArray data = f.readAll();
        f.close();
        return int(data.count('\n'));
    };

    const QString logA = QStringLiteral("%1/a.log").arg(dir.path());
    const QString logB = QStringLiteral("%1/b.log").arg(dir.path());
    const QString pathA = QStringLiteral("%1/server-a").arg(dir.path());
    const QString pathB = QStringLiteral("%1/server-b").arg(dir.path());
    writeWrapper(pathA, logA);
    writeWrapper(pathB, logB);

    QVERIFY(RuntimeLocator::probeCached(pathA).ok);
    QCOMPARE(probeCount(logA), 2);  // --version + --help for the fresh probe
    QVERIFY(RuntimeLocator::probeCached(pathB).ok);
    QCOMPARE(probeCount(logB), 2);

    // A was evicted by B: probing A again must re-probe (2 more marker lines).
    // A 4-slot cache would still hold A and return it without a re-probe.
    QVERIFY(RuntimeLocator::probeCached(pathA).ok);
    QCOMPARE(probeCount(logA), 4);
}

QTEST_MAIN(TestRuntimeLocator)
#include "test_runtime_locator.moc"