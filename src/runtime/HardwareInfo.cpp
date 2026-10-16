#include "runtime/HardwareInfo.h"

#include <algorithm>

#include "core/ByteSize.h"
#include "runtime/ModelMemoryEstimator.h"

namespace llocr {
namespace {

constexpr quint32 kNvidiaVendorId = 0x10DE;
constexpr double kMemoryBudgetFactor = 0.9;

}  // namespace

qint64 HardwareReport::memoryBudgetBytes() const
{
    const qint64 memory = primaryGpuMemoryBytes() > 0 ? primaryGpuMemoryBytes() : ramBytes;
    if (memory <= 0)
        return 0;
    return static_cast<qint64>(static_cast<double>(memory) * kMemoryBudgetFactor);
}

HardwareInfo::HardwareInfo(QObject *parent) : QObject(parent), m_report(detect()) {}

HardwareReport HardwareInfo::detect()
{
    static const HardwareReport report = [] {
        HardwareReport r;
        r.adapters = detectAdapters();
        r.ramBytes = systemPhysicalRamBytes();
        return r;
    }();
    return report;
}

QString HardwareInfo::recommendedBackendFor(const QString &osTag, const QList<GpuAdapter> &adapters)
{
    if (osTag != QLatin1String("win") && osTag != QLatin1String("linux"))
        return QStringLiteral("cpu");
    const bool nvidia = std::any_of(adapters.cbegin(), adapters.cend(), [](const GpuAdapter &a) { return a.vendorId == kNvidiaVendorId; });
    if (nvidia)
        return QStringLiteral("cuda");
    if (!adapters.isEmpty())
        return QStringLiteral("vulkan");
    return QStringLiteral("cpu");
}

QString HardwareInfo::vramLabel() const
{
    return formatBytes(m_report.primaryGpuMemoryBytes());
}

QString HardwareInfo::ramLabel() const
{
    return formatBytes(m_report.ramBytes);
}

QString HardwareInfo::memoryLabel() const
{
    return m_report.primaryGpuMemoryBytes() > 0 ? vramLabel() : ramLabel();
}

}  // namespace llocr
