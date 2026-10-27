#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QUrl>

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/RequestJournal.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeLog.h"

namespace llocr {

RuntimeLog::RuntimeLog(SettingsStore &settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(200);
    connect(&m_flushTimer, &QTimer::timeout, this, [this]() { flushToView(); });
    // The external runtime has no process to read a log from: its trace is
    // the client's own journal, and it grows with every request.
    connect(&RequestJournal::instance(), &RequestJournal::changed, this, [this]() { flushToView(); });
}

bool RuntimeLog::externalMode() const
{
    return m_settings.mode() == ConnectionMode::External;
}

void RuntimeLog::setServer(LlamaServerProcess *server)
{
    if (m_server == server)
        return;
    if (m_server)
        disconnect(m_server, nullptr, this, nullptr);
    m_server = server;
    m_flushTimer.stop();
    m_lines.clear();
    m_pendingDropped = 0;
    if (m_server) {
        m_lines = m_server->ringBuffer(kMaxLines);
        connect(m_server, &LlamaServerProcess::logLineAppended, this, [this](const QString &line) {
            // appendLine() skips blank lines, so an empty payload can only be
            // clearLog()'s reset marker.
            if (line.isEmpty()) {
                m_lines.clear();
                m_pendingDropped = 0;
                m_flushTimer.stop();
                flushToView();
                return;
            }
            m_lines.append(line);
            if (m_lines.size() > kMaxLines) {
                m_pendingDropped += m_lines.size() - kMaxLines;
                m_lines.remove(0, m_lines.size() - kMaxLines);
            }
            m_flushTimer.start();
        });
        connect(m_server, &QObject::destroyed, this, [this]() { m_server = nullptr; });
    }
    flushToView();
}

QString RuntimeLog::serverLog() const
{
    if (externalMode())
        return RequestJournal::instance().lines().join(QLatin1Char('\n'));
    return m_lines.join(QLatin1Char('\n'));
}

int RuntimeLog::lineCount() const
{
    if (externalMode())
        return RequestJournal::instance().count();
    return m_lines.size();
}

QString RuntimeLog::serverLogPath() const
{
    if (externalMode())
        return QString();  // the client's journal lives in memory, not in a file
    return m_server ? m_server->logFilePath() : QString();
}

QString RuntimeLog::serverLogDir() const
{
    if (m_server) {
        const QString fp = m_server->logFilePath();
        if (!fp.isEmpty())
            return QFileInfo(fp).absolutePath();
    }
    const RuntimePaths p(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    return p.logsDir();
}

void RuntimeLog::copyServerLog()
{
    QGuiApplication::clipboard()->setText(serverLog());
}

void RuntimeLog::clearServerLog()
{
    if (externalMode()) {
        RequestJournal::instance().clear();
        return;
    }
    if (m_server) {
        // The empty-line marker from clearLog() resets the mirror synchronously.
        m_server->clearLog();
        return;
    }
    // No live process to emit the marker (stopped or external runtime) —
    // reset the mirror directly or the window would keep the stale lines.
    m_lines.clear();
    m_pendingDropped = 0;
    m_flushTimer.stop();
    flushToView();
}

void RuntimeLog::openServerLogFolder()
{
    const QString dir = serverLogDir();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void RuntimeLog::attachLogView(QQuickTextDocument *view)
{
    m_sync.attach(view ? view->textDocument() : nullptr);
    flushToView();
}

void RuntimeLog::flushToView()
{
    if (externalMode())
        m_sync.apply(RequestJournal::instance().lines(), 0, QString());
    else
        m_sync.apply(m_lines, m_pendingDropped, QString());
    m_pendingDropped = 0;
    emit serverLogChanged();
}

}  // namespace llocr
