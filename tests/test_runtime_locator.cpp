#include <QFile>
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

QTEST_MAIN(TestRuntimeLocator)
#include "test_runtime_locator.moc"