#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace llocr {

struct GpuAdapter {
    QString name;
    qint64 dedicatedBytes = 0;
    quint32 vendorId = 0;
};

// The machine's GPU adapters (best first) and total RAM — the input to the
// wizard's backend recommendation and the quantization memory budget.
struct HardwareReport {
    QList<GpuAdapter> adapters;
    qint64 ramBytes = 0;

    bool hasGpu() const { return !adapters.isEmpty(); }
    // An integrated adapter can report zero dedicated memory while sharing
    // system RAM; such a machine is budgeted like a CPU-only one.
    qint64 primaryGpuMemoryBytes() const { return hasGpu() ? adapters.first().dedicatedBytes : 0; }
    qint64 memoryBudgetBytes() const;
};

class HardwareInfo : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString gpuName READ gpuName CONSTANT)
    Q_PROPERTY(QString vramLabel READ vramLabel CONSTANT)
    Q_PROPERTY(QString ramLabel READ ramLabel CONSTANT)
    // VRAM when a GPU reports dedicated memory, total RAM otherwise.
    Q_PROPERTY(QString memoryLabel READ memoryLabel CONSTANT)
    Q_PROPERTY(qint64 budgetBytes READ budgetBytes CONSTANT)

public:
    explicit HardwareInfo(QObject *parent = nullptr);

    static HardwareReport detect();

    static QString recommendedBackendFor(const QString &osTag, const QList<GpuAdapter> &adapters);

    QString gpuName() const { return m_report.hasGpu() ? m_report.adapters.first().name : QString(); }
    QString vramLabel() const;
    QString ramLabel() const;
    QString memoryLabel() const;
    qint64 budgetBytes() const { return m_report.memoryBudgetBytes(); }

private:
    // One implementation per platform (HardwareInfo_win/_mac/_linux), the
    // ProcessGuard pattern.
    static QList<GpuAdapter> detectAdapters();

    HardwareReport m_report;
};

}  // namespace llocr
