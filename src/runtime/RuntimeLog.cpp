#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QUrl>

#include "app/SettingsStore.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeLog.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

RuntimeLog::RuntimeLog(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(200);
    connect(&m_flushTimer, &QTimer::timeout, this,
            [this]() { emit serverLogChanged(); });
}

void RuntimeLog::setServer(LlamaServerProcess *server)
{
    if (m_server == server)
        return;
    if (m_server)
        disconnect(m_server, nullptr, this, nullptr);
    m_server = server;
    m_flushTimer.stop();
    if (m_server) {
        connect(m_server, &LlamaServerProcess::logLineAppended, this,
                [this](const QString &) { m_flushTimer.start(); });
        connect(m_server, &QObject::destroyed, this,
                [this]() { m_server = nullptr; });
    }
    emit serverLogChanged();
}

QString RuntimeLog::serverLog() const
{
    if (!m_server)
        return QString();
    return m_server->ringBuffer(2000).join(QStringLiteral("\n"));
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
    QGuiApplication::clipboard()->setText(serverLog());
}

void RuntimeLog::clearServerLog()
{
    if (m_server)
        m_server->clearLog();
    m_flushTimer.stop();
    emit serverLogChanged();
}

void RuntimeLog::openServerLogFolder()
{
    const QString dir = serverLogDir();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

}  // namespace llocr