#pragma once

#include <QObject>
#include <QString>

#include <QQmlEngine>

namespace llocr {

class LlamaServerProcess;
class SettingsStore;

// Live view of the managed server's log (ring-buffer tail + rolling file) for
// the log window. Extracted from the RuntimeController facade (§ review 3.4):
// the log window binds to a small dedicated surface instead of ~6 members on a
// ~20-property singleton.
//
// RuntimeController owns the LlamaServerProcess and pushes it here on every
// (re)spawn via setServer(); this view follows restarts automatically. With no
// server attached, serverLog() is empty and the log directory falls back to the
// configured logs path.
class RuntimeLog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString serverLog READ serverLog NOTIFY serverLogChanged)

public:
    explicit RuntimeLog(SettingsStore &settings, QObject *parent = nullptr);

    /// Ring-buffer tail of the managed server log, for the log window.
    QString serverLog() const;

    /// Absolute path to the managed server's rolling log file (logs/…).
    Q_INVOKABLE QString serverLogPath() const;
    /// Absolute path of the logs directory (parent of serverLogPath()).
    Q_INVOKABLE QString serverLogDir() const;
    /// Copies the whole ring-buffer tail to the system clipboard (H.1).
    Q_INVOKABLE void copyServerLog();
    /// Clears the in-memory live log view (ring buffer). §H.1 log window.
    Q_INVOKABLE void clearServerLog();
    /// Opens the logs/ directory in the platform file manager (H.1).
    Q_INVOKABLE void openServerLogFolder();

    /// Attaches the live server (called by RuntimeController on each spawn);
    /// passing nullptr detaches. Re-emits serverLogChanged so the view refreshes.
    void setServer(LlamaServerProcess *server);

signals:
    void serverLogChanged();

private:
    SettingsStore &m_settings;
    LlamaServerProcess *m_server = nullptr;
};

}  // namespace llocr