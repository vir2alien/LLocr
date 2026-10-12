#include "app/ProblemLog.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickTextDocument>

namespace llocr {

ProblemLog::ProblemLog(QObject *parent) : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(200);
    connect(&m_flushTimer, &QTimer::timeout, this, [this]() { flushToView(); });
}

int ProblemLog::errorCount() const
{
    int errors = 0;
    for (const Entry &entry : m_entries) {
        if (entry.severity == Error)
            ++errors;
    }
    return errors;
}

int ProblemLog::warningCount() const
{
    return m_entries.size() - errorCount();
}

QString ProblemLog::formatEntry(const Entry &entry) const
{
    const QString severity = entry.severity == Error ? QCoreApplication::translate("ProblemLog", "error") : QCoreApplication::translate("ProblemLog", "warning");
    return QCoreApplication::translate("ProblemLog", "%1: %2").arg(severity, entry.message.text());
}

QString ProblemLog::headerLine() const
{
    if (m_dropped == 0)
        return QString();
    return QCoreApplication::translate("ProblemLog", "… %1 earlier entr(y/ies) dropped").arg(m_dropped);
}

QString ProblemLog::logText() const
{
    QStringList lines;
    const QString header = headerLine();
    if (!header.isEmpty())
        lines.append(header);
    lines.append(m_lines);
    return lines.join(QLatin1Char('\n'));
}

void ProblemLog::report(const StatusMessage &message, Severity severity)
{
    if (message.isEmpty())
        return;
    m_entries.append({severity, message});
    m_lines.append(formatEntry(m_entries.last()));
    if (m_entries.size() > kMaxEntries) {
        const int excess = m_entries.size() - kMaxEntries;
        m_dropped += excess;
        m_pendingDropped += excess;
        m_entries.remove(0, excess);
        m_lines.remove(0, excess);
    }
    m_flushTimer.start();
}

void ProblemLog::clear()
{
    if (m_entries.isEmpty() && m_dropped == 0)
        return;
    m_entries.clear();
    m_lines.clear();
    m_dropped = 0;
    m_pendingDropped = 0;
    m_flushTimer.stop();
    flushToView(true);
}

void ProblemLog::copyLog()
{
    QGuiApplication::clipboard()->setText(logText());
}

void ProblemLog::attachLogView(QQuickTextDocument *view)
{
    m_sync.attach(view ? view->textDocument() : nullptr);
    flushToView(true);
}

void ProblemLog::retranslate()
{
    m_lines.clear();
    for (const Entry &entry : m_entries)
        m_lines.append(formatEntry(entry));
    m_pendingDropped = 0;
    flushToView(true);
}

void ProblemLog::flushToView(bool forceFull)
{
    m_sync.apply(m_lines, m_pendingDropped, headerLine(), forceFull);
    m_pendingDropped = 0;
    emit logChanged();
}

}  // namespace llocr
