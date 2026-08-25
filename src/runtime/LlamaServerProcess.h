#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include "runtime/RuntimeState.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

// Owns one llama-server child process: spawning, stdout/stderr logging (ring
// buffer + rotating file), health-gating, controlled stop and bounded
// auto-restart (§5 Stage B task 3). It never builds the argv — the caller
// passes a fully formed ServerLaunchConfig-derived program+arguments — never
// attaches to a foreign process (ADR 33), and never uses a shell (7.1).
//
// Port: when Options.port is 0 a free loopback port is picked before spawn;
// a fixed busy port is surfaced as an error (no auto-attach).
class LlamaServerProcess : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString program;          // absolute path to llama-server
        QStringList arguments;    // full argv EXCLUDING --port (allocated here)
        QString workingDirectory;
        QString host = QStringLiteral("127.0.0.1");
        QString baseUrl;          // override health target; empty → host:port
        int port = 0;             // 0 = pick a free port
        int startupTimeoutMs = 180000;
        bool autoRestart = true;  // bounded: ≤3 restarts / 5 min
        QString logFile;          // rotating file path (5 MB × 3)
        QString ownerJsonPath;    // macOS best-effort owner record
        bool stopOnExit = true;
    };

    explicit LlamaServerProcess(const Options &opts, QObject *parent = nullptr);

    // Re-applies the launch options for the next start (settings may have
    // changed since the process object was created). No-op while running.
    void setOptions(const Options &opts);

    // Resolves the port, spawns the child, arms the health watchdog and the
    // ProcessGuard. Returns empty on success else a human-readable error.
    QString start();

    // Stops the child if running and cancels any pending restart. Uses
    // terminate → wait(graceMs) → kill. Removes the owner record. Idempotent.
    void stop(unsigned graceMs = 5000);

    /// Blocking shutdown for ~aboutToQuit (event loop is already stopped):
    /// terminate → wait ≤ baseTimeoutMs → kill → wait ≤ 2000, then owner
    /// record removal. §5.5.
    void shutdownSync(unsigned baseTimeoutMs = 5000);

    bool isRunning() const;
    RuntimeState state() const;
    QString statusMessage() const;
    QString lastError() const;

    /// Model-load progress parsed from stderr (llama.cpp prints
    /// "loading tensors, NN%" / "load_tensors: NN%"). -1 while unknown/not
    /// loading, 0..100 during a load. §H.7 task 2.
    int loadProgressPercent() const { return m_loadPercent; }

    QStringList ringBuffer(int maxLines = -1) const;
    QString logFilePath() const;
    /// Clears the in-memory ring buffer (live log view) without touching the
    /// rolling file. Emits logLineAppended so the UI view refreshes.
    void clearLog();

    int startCount() const;
    int restartCount() const;
    int resolvedPort() const;

    /// Picks a free loopback port, retrying on a racing allocator. Returns 0
    /// (and sets *error) on failure. Never attaches to an existing listener.
    static int pickFreePort(QString *error = nullptr);

signals:
    void stateChanged();
    void statusMessageChanged();
    void logLineAppended(QString line);
    void healthReached();
    void loadProgressChanged();

private:
    void spawn();
    void classifyLine(const QString &line);
    static int parseLoadPercent(const QString &line);
    void armHealthPolling();
    void onHealthReply(QNetworkReply *reply);
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void markFailed(const QString &reason);
    void rotateLogIfNeeded();
    void appendLine(const QString &line);
    void writeOwnerJson();
    void clearOwnerJson();
    void setState(RuntimeState next);
    void setStatus(const QString &status);
    void appendLogFile(const QString &line);

    Options m_opts;

    QProcess m_process;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_healthTimer = nullptr;

    QElapsedTimer m_elapsed;
    QElapsedTimer m_restartWindow;
    int m_restartWindowCount = 0;

    bool m_healthReached = false;
    bool m_autoRestartScheduled = false;
    bool m_stopRequested = false;
    int m_attemptsTotal = 0;
    int m_loadPercent = -1;

    QString m_healthUrl;
    QString m_lineBuffer;
    QStringList m_ring;
    int m_ringMaxLines = 2000;

    QString m_status = QStringLiteral("Idle");
    QString m_lastError;
    RuntimeState m_state = RuntimeState::Stopped;
};

}  // namespace llocr