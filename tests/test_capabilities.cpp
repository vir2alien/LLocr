#include <QtTest>

#include "runtime/ServerCapabilities.h"

using namespace llocr;

class TestCapabilities : public QObject {
    Q_OBJECT

private slots:
    void buildNumberExtraction();
    void modernBuildDetect();
    void ancientBuildProfiles();
    void helpRefinesAllowlist();
    void helpRemovesEnabledFlag();
    void unknownCapabilitiesAnswers();
    void jsonRoundTrip();
};

void TestCapabilities::buildNumberExtraction()
{
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("build: 10594 (b10594)")), 10594);
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("b10594")), 10594);
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("version: 4536")), 4536);
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("llama-server build 9999")), 9999);
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("1.4.0")), -1);   // semver tail — ambiguous
    QCOMPARE(ServerCapabilities::extractBuildNumber(QStringLiteral("hello")), -1);
}

void TestCapabilities::modernBuildDetect()
{
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("build: 10594 (b10594)"));
    QVERIFY(caps.ok);
    QVERIFY(!caps.belowMinimum);
    QCOMPARE(caps.build, QStringLiteral("b10594"));
    QVERIFY(caps.supportsFlashAttn);
    QVERIFY(caps.supportsFlashAttnValue);  // on|off|auto
    QVERIFY(caps.supportsAlias);
    QVERIFY(caps.supportsJinja);
    QVERIFY(caps.supportsCacheTypeK);
    QVERIFY(caps.supportsCacheTypeV);
}

void TestCapabilities::ancientBuildProfiles()
{
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("b4000"));
    QVERIFY(caps.ok);
    QVERIFY(!caps.belowMinimum);         // b4000 is exactly the floor
    QVERIFY(caps.supportsFlashAttn);     // but bare-boolean only
    QVERIFY(!caps.supportsFlashAttnValue);
    QVERIFY(!caps.supportsAlias);        // pre-dates --alias
    QVERIFY(!caps.supportsJinja);
    QVERIFY(!caps.supportsCacheTypeK);
    QVERIFY(!caps.supportsCacheTypeV);
}

void TestCapabilities::helpRefinesAllowlist()
{
    // Modern build but its --help disabled -ctk: allowlist refined down.
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("b10594"),
        QStringLiteral("usage: llama-server [options]\n  --flash-attn [on|off|auto]\n  --alias NAME\n  --jinja\n"));
    QVERIFY(caps.ok);
    QVERIFY(caps.supportsFlashAttnValue);
    QVERIFY(caps.supportsAlias);
    QVERIFY(caps.supportsJinja);
    // Not present in this help text → refined off.
    QVERIFY(!caps.supportsCacheTypeK);
    QVERIFY(!caps.supportsCacheTypeV);
}

void TestCapabilities::helpRemovesEnabledFlag()
{
    // Old build, help predates --flash-attn: flag must be dropped.
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("b4000"), QStringLiteral("usage: llama-server\n"));
    QVERIFY(caps.ok);
    QVERIFY(!caps.supportsFlashAttn);   // help shows no such flag
}

void TestCapabilities::unknownCapabilitiesAnswers()
{
    // Build unknown but the binary answered → ok, conservative caps, no gate.
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("llama-server 1.0.0"),
        QStringLiteral("usage: llama-server [options]\n  --alias name\n"));
    QVERIFY(caps.ok);
    QVERIFY(caps.build.isEmpty());
    QVERIFY(!caps.belowMinimum);
    // With no build number we start conservative and rely on --help refinement.
    QVERIFY(caps.supportsAlias);  // help proves it
}

void TestCapabilities::jsonRoundTrip()
{
    const ServerCapabilities caps = ServerCapabilities::detect(
        QStringLiteral("b10594"),
        QStringLiteral("--flash-attn [on|off|auto]\n--alias name\n--jinja\n-ctk q8_0\n-ctv q8_0\n"));
    const QJsonObject o = caps.toJson();
    const ServerCapabilities back = ServerCapabilities::fromJson(o);
    QCOMPARE(back.build, caps.build);
    QCOMPARE(back.supportsFlashAttnValue, caps.supportsFlashAttnValue);
    QCOMPARE(back.supportsAlias, caps.supportsAlias);
    QCOMPARE(back.supportsJinja, caps.supportsJinja);
    QCOMPARE(back.supportsCacheTypeK, caps.supportsCacheTypeK);
    QCOMPARE(back.supportsCacheTypeV, caps.supportsCacheTypeV);
}

QTEST_MAIN(TestCapabilities)
#include "test_capabilities.moc"