#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "runtime/RuntimeState.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

class LlamaServerProcess : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString program;          // absolute path to llama-server
        QStringList arguments;
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
    ~LlamaServerProcess() override;

    void setOptions(const Options &opts);
    QString start();
    void stop(unsigned graceMs = 5000);
    void shutdownSync(unsigned baseTimeoutMs = 5000);

    void retranslate();

    bool isRunning() const;
    RuntimeState state() const;
    QString statusMessage() const;
    QString lastError() const;
    int loadProgressPercent() const { return m_loadPercent; }

    QStringList ringBuffer(int maxLines = -1) const;
    QString logFilePath() const;
    void clearLog();

    int startCount() const;
    int restartCount() const;
    int resolvedPort() const;
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
    void tryModelsFallback();
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void markFailed(const QString &reason);
    bool rotateLogIfNeeded();  // true if the log file was rotated this call
    void appendLine(const QString &line);
    void closeLogFile();
    void writeOwnerJson();
    void clearOwnerJson();
    void setState(RuntimeState next);
    void setStatus(const QString &status);
    void appendLogFile(const QString &line);

private:
    Options m_opts;

    QProcess m_process;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_healthTimer = nullptr;
    QTimer *m_killTimer = nullptr;
    bool m_modelsProbed = false;

    QFile m_logFile;
    QTextStream m_logStream;
    int m_linesSinceRotateCheck = 0;

    QElapsedTimer m_elapsed;
    QElapsedTimer m_restartWindow;
    int m_restartWindowCount = 0;

    bool m_healthReached = false;
    bool m_healthInFlight = false;
    bool m_autoRestartScheduled = false;
    bool m_stopRequested = false;
    int m_attemptsTotal = 0;
    int m_loadPercent = -1;

    QString m_healthUrl;
    int m_port = 0;
    QString m_lineBuffer;
    QStringList m_ring;
    int m_ringMaxLines = 2000;

    QString m_status = QStringLiteral("Idle");
    QString m_lastError;
    RuntimeState m_state = RuntimeState::Stopped;
};

}  // namespace llocr