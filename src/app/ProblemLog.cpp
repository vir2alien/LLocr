#include "app/ProblemLog.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

namespace llocr {

ProblemLog::ProblemLog(QObject *parent)
    : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(200);
    connect(&m_flushTimer, &QTimer::timeout, this, &ProblemLog::flush);
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

QString ProblemLog::logText() const
{
    QStringList lines;
    if (m_dropped > 0) {
        lines.append(QCoreApplication::translate("ProblemLog", "… %1 earlier entr(y/ies) dropped")
                         .arg(m_dropped));
    }
    for (const Entry &entry : m_entries) {
        lines.append(QCoreApplication::translate("ProblemLog", "%1: %2")
                         .arg(entry.severity == Error
                                  ? QCoreApplication::translate("ProblemLog", "error")
                                  : QCoreApplication::translate("ProblemLog", "warning"),
                              entry.message.text()));
    }
    return lines.join(QLatin1Char('\n'));
}

void ProblemLog::report(const StatusMessage &message, Severity severity)
{
    if (message.isEmpty())
        return;
    m_entries.append({severity, message});
    if (m_entries.size() > kMaxEntries) {
        m_dropped += m_entries.size() - kMaxEntries;
        m_entries.remove(0, m_entries.size() - kMaxEntries);
    }
    m_flushTimer.start();
}

void ProblemLog::flush()
{
    emit logChanged();
}

void ProblemLog::clear()
{
    if (m_entries.isEmpty() && m_dropped == 0)
        return;
    m_entries.clear();
    m_dropped = 0;
    m_flushTimer.stop();
    emit logChanged();
}

void ProblemLog::copyLog()
{
    QGuiApplication::clipboard()->setText(logText());
}

void ProblemLog::retranslate()
{
    // The entries hold keys, not renderings (ADR 114), so a language switch is
    // a re-read — but the log window is only listening, so say so.
    emit logChanged();
}

}  // namespace llocr
