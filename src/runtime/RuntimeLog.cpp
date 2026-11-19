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
    // The journal holds the requests the client sent, to the local server and
    // to the external endpoints alike.
    connect(&RequestJournal::instance(), &RequestJournal::changed, this, [this]() { flushJournalToView(); });
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
    return m_lines.join(QLatin1Char('\n'));
}

QString RuntimeLog::journalLog() const
{
    return RequestJournal::instance().lines().join(QLatin1Char('\n'));
}

int RuntimeLog::lineCount() const
{
    return m_lines.size();
}

int RuntimeLog::journalLineCount() const
{
    return RequestJournal::instance().count();
}

QString RuntimeLog::serverLogPath() const
{
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
    const QString server = serverLog();
    const QString journal = journalLog();
    const QString text = server.isEmpty() ? journal : (journal.isEmpty() ? server : server + QLatin1String("\n\n") + journal);
    QGuiApplication::clipboard()->setText(text);
}

void RuntimeLog::clearServerLog()
{
    RequestJournal::instance().clear();
    if (m_server) {
        m_server->clearLog();
        m_lines.clear();
        m_pendingDropped = 0;
        flushToView();
        return;
    }
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

void RuntimeLog::attachJournalView(QQuickTextDocument *view)
{
    m_journalSync.attach(view ? view->textDocument() : nullptr);
    flushJournalToView();
}

void RuntimeLog::setLogViewVisible(bool visible)
{
    if (m_viewVisible == visible)
        return;
    m_viewVisible = visible;
    if (visible) {
        m_sync.setPaused(false);
        m_journalSync.setPaused(false);
        flushToView();
        flushJournalToView();
        return;
    }
    m_sync.setPaused(true);
    m_journalSync.setPaused(true);
}

void RuntimeLog::flushToView()
{
    m_sync.apply(m_lines, m_pendingDropped, QString());
    m_pendingDropped = 0;
    emit serverLogChanged();
}

void RuntimeLog::flushJournalToView()
{
    m_journalSync.apply(RequestJournal::instance().lines(), 0, QString());
    emit serverLogChanged();
}

}  // namespace llocr
