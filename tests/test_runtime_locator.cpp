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

QTEST_MAIN(TestRuntimeLocator)
#include "test_runtime_locator.moc"