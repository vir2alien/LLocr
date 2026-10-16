#include <QtTest>

#include "runtime/HardwareInfo.h"

using namespace llocr;

namespace {

GpuAdapter adapter(const QString &name, qint64 bytes, quint32 vendor)
{
    return {name, bytes, vendor};
}

}  // namespace

class TestHardwareInfo : public QObject
{
    Q_OBJECT

private slots:
    void backendFollowsAdapters();
    void budgetPrefersDedicatedMemory();
    void detectReportsThisMachine();
};

void TestHardwareInfo::backendFollowsAdapters()
{
    const quint32 nvidia = 0x10DE;
    const quint32 amd = 0x1002;
    const quint32 intel = 0x8086;

    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("win"), {adapter(QStringLiteral("RTX 4090"), 24LL << 30, nvidia)}), QStringLiteral("cuda"));
    // Hybrid laptop: the AMD card has more memory, an NVIDIA one still means
    // CUDA.
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("win"), {adapter(QStringLiteral("RX 7900"), 24LL << 30, amd), adapter(QStringLiteral("RTX 3050"), 8LL << 30, nvidia)}),
             QStringLiteral("cuda"));
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("win"), {adapter(QStringLiteral("RX 7900"), 24LL << 30, amd)}), QStringLiteral("vulkan"));
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("win"), {adapter(QStringLiteral("Iris Xe"), 0, intel)}), QStringLiteral("vulkan"));
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("win"), {}), QStringLiteral("cpu"));
    // Linux carries no adapter enumeration — the CPU default stands.
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("linux"), {}), QStringLiteral("cpu"));
    // Other platforms are not this function's to decide.
    QCOMPARE(HardwareInfo::recommendedBackendFor(QStringLiteral("macos"), {adapter(QStringLiteral("Apple M3"), 18LL << 30, 0)}), QStringLiteral("cpu"));
}

void TestHardwareInfo::budgetPrefersDedicatedMemory()
{
    const qint64 vram = 8LL << 30;
    const qint64 ram = 64LL << 30;

    HardwareReport discrete;
    discrete.adapters = {adapter(QStringLiteral("RTX 4090"), vram, 0x10DE)};
    discrete.ramBytes = ram;
    QCOMPARE(discrete.memoryBudgetBytes(), qint64(vram * 0.9));

    // An integrated adapter reports no dedicated memory and shares system
    // RAM — the machine is budgeted like a CPU-only one.
    HardwareReport integrated;
    integrated.adapters = {adapter(QStringLiteral("Iris Xe"), 0, 0x8086)};
    integrated.ramBytes = ram;
    QVERIFY(integrated.hasGpu());
    QCOMPARE(integrated.primaryGpuMemoryBytes(), 0);
    QCOMPARE(integrated.memoryBudgetBytes(), qint64(ram * 0.9));

    HardwareReport cpuOnly;
    cpuOnly.ramBytes = ram;
    QCOMPARE(cpuOnly.memoryBudgetBytes(), qint64(ram * 0.9));

    HardwareReport nothing;
    QCOMPARE(nothing.memoryBudgetBytes(), 0);
}

void TestHardwareInfo::detectReportsThisMachine()
{
    const HardwareReport report = HardwareInfo::detect();
    QVERIFY(report.ramBytes > 0);
    if (report.hasGpu()) {
        QVERIFY(!report.adapters.first().name.isEmpty());
        QVERIFY(report.adapters.first().dedicatedBytes >= 0);
        QVERIFY(report.memoryBudgetBytes() > 0);
    }
    // Repeat calls are served from the cache and agree.
    const HardwareReport again = HardwareInfo::detect();
    QCOMPARE(again.ramBytes, report.ramBytes);
    QCOMPARE(again.adapters.size(), report.adapters.size());

    HardwareInfo info;
    QVERIFY(!info.ramLabel().isEmpty());
    QCOMPARE(info.budgetBytes(), report.memoryBudgetBytes());
}

QTEST_MAIN(TestHardwareInfo)
#include "test_hardware_info.moc"
