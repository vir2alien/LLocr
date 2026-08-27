#pragma once

#include <QFuture>
#include <QFutureInterface>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

#include "runtime/ConnectionMode.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeState.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

class SettingsStore;
class OpenAiProvider;

// Facade over every managed-runtime concern (process, downloads, installs,
// model selection). A single instance is created in main.cpp — before the QML
// engine loads — and lives for the whole process (ADR 36). QML must NOT
// instantiate it; it only consumes the already-registered singleton.
//
// Recognition goes through exactly one async entry point:
// ensureConnectionReady(). In External it resolves immediately from settings;
// in Managed (Stage G-core) it starts the server, waits for /health, queries
// /v1/models, verifies the alias, and returns the computed ResolvedConnection.
class RuntimeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int busyState READ busyStateInt NOTIFY busyStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(int loadProgressPercent READ loadProgressPercent NOTIFY loadProgressChanged)
    Q_PROPERTY(bool configValid READ configValid NOTIFY configValidChanged)
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY lockedOutChanged)
    Q_PROPERTY(QString serverLog READ serverLog NOTIFY serverLogChanged)
    Q_PROPERTY(bool selftestRunning READ selftestRunning NOTIFY selftestFinished)
    Q_PROPERTY(bool selftestOk READ selftestOk NOTIFY selftestFinished)
    Q_PROPERTY(QString selftestMessage READ selftestMessage NOTIFY selftestFinished)

public:
    explicit RuntimeController(SettingsStore &settings, QObject *parent = nullptr);

    // --- QML-visible state ----------------------------------------------
    RuntimeState state() const { return m_state; }
    AppBusyState busyState() const { return m_busyState; }
    QString statusMessage() const { return m_statusMessage; }
    /// Model-load progress of the managed server (0..100), or -1 when not
    /// loading / unknown. Diffuses §H.7 stderr classification to QML.
    int loadProgressPercent() const { return m_loadProgressPercent; }
    bool configValid() const { return m_configValid; }
    bool lockedOut() const { return m_lockedOut; }
    /// Ring-buffer tail of the managed server log, for the log window.
    QString serverLog() const;

    /// Absolute path to the managed server's rolling log file (logs/…).
    Q_INVOKABLE QString serverLogPath() const;
    /// Absolute path of the logs directory (parent of serverLogPath()).
    Q_INVOKABLE QString serverLogDir() const;

    /// Copies the whole ring-buffer tail to the system clipboard (H.1).
    Q_INVOKABLE void copyServerLog();
    /// Converts a file URL (e.g. FileDialog.selectedFile) to a local path.
    /// Non-file URLs pass through unchanged.
    Q_INVOKABLE static QString localPath(const QUrl &url)
    {
        return url.isLocalFile() ? url.toLocalFile() : url.toString();
    }
    /// Clears the in-memory live log view (ring buffer). §H.1 log window.
    Q_INVOKABLE void clearServerLog();
    /// Opens the logs/ directory in the platform file manager (H.1).
    Q_INVOKABLE void openServerLogFolder();

    // ------- §H.2 memory estimation --------------------------------------
    /// Best-effort RAM estimate for launching a managed model at ctxSize/…
    /// Returns a QVariantMap (modelBytes, kvCacheBytes, totalBytes,
    /// systemRamBytes, valid, error). Never throws.
    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath,
                                                int ctxSize);

    /// §1.4 `canRecognize` (the mode/runtime part; `documentLoaded` is supplied
    /// by the caller). External is always eligible; Managed needs a Ready
    /// server, or a Stopped-but-startable one (configValid + auto/manual start).
    Q_INVOKABLE bool canRecognize(bool documentLoaded) const;

    // --- ARM-coordinated resolution --------------------------------------
    // External:  resolves immediately from SettingsStore (called synchronously
    //            on the caller's thread, ADR 26).
    // Managed:   starts the process, waits for /health + /v1/models, computes
    //            the ResolvedConnection, then invokes onResolved. Concurrent
    //            callers share the in-flight resolve: each callback is queued
    //            and all are invoked once the single resolve completes (3.3).
    void ensureConnectionReady(const std::function<void(const ResolvedConnection &)> &onResolved);

    /// Cancels a pending startup (called by RecognitionController::stop() while
    /// the app is in StartingRuntime). Interrupts the start wait, completes any
    /// pending resolve with an error, and stops a still-starting server. No-op
    /// in External or when nothing is pending.
    void cancelPendingStart();

    /// Runs an independent self-test (used by the master wizard "Check" button).
    /// Starts the managed server if needed, waits for readiness, then issues one
    /// real OCR request against a built-in test image and returns the text. In
    /// External this reports NotConfigured (there is nothing to self-test here).
    QFuture<SelfTestResult> runSelfTest();

    /// QML-friendly variant: starts the self-test and reports progress via the
    /// `selftest*` properties / `selftestFinished` signal (QFuture is unusable
    /// from QML). No-op while already running.
    Q_INVOKABLE void runSelfTestQml();

    bool selftestRunning() const { return m_selftestRunning; }
    bool selftestOk() const { return m_selftestOk; }
    QString selftestMessage() const { return m_selftestMessage; }

    // --- Wiring helpers --------------------------------------------------
    void setSingleInstanceHeld(bool held);

    // --- Stage B: managed server lifecycle (Runtime settings tab) --------
    /// Starts the managed llama-server with the current launch/* settings.
    /// Empty on success; an error message otherwise. Requires a valid binary.
    Q_INVOKABLE QString startServer();
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void restartServer();
    /// Runs the locator probe synchronously; the summary is surfaced via
    /// statusMessage (there is no dedicated probeResult property). Returns
    /// the same summary.
    Q_INVOKABLE QString probeRuntimePath(const QString &path);
    /// Auto-discovers a llama-server binary via RuntimeLocator::autoDiscover()
    /// and returns the found path (or an empty string).
    Q_INVOKABLE QString autoDiscoverPath();
    /// Shell-escaped command line for the managed launch (wizard preview). Empty
    /// in External mode or when the binary cannot be probed. Delegates to
    /// ServerLaunchConfig::toDisplayCommand() — single source of truth (3.2).
    Q_INVOKABLE QString launchCommandPreview();
    /// Blocking shutdown (main.cpp ~aboutToQuit path). §5.5.
    void shutdownSync();

    static ConnectionMode modeFromSettings(const SettingsStore &settings);

private:
    int stateInt() const { return static_cast<int>(m_state); }
    int busyStateInt() const { return static_cast<int>(m_busyState); }

    void setState(RuntimeState next);
    void setBusyState(AppBusyState next);
    void setStatusMessage(const QString &msg);
    void setLoadProgressPercent(int pct);

    void recomputeConfigValid();

    // External path: build ResolvedConnection directly from settings.
    ResolvedConnection resolveExternal() const;
    ResolvedConnection buildManagedConnection() const;

    // --- Managed resolve machinery (Stage G-core) ------------------------
    // Starts (or attaches to) a managed-server resolve; on completion the queued
    // callbacks (registered via ensureConnectionReady) are all invoked. Only
    // meaningful in Managed mode.
    void beginManagedResolve();
    void completeResolve(ResolvedConnection conn);
    void failResolve(const QString &message);
    void onServerStateForResolve();

    void fetchManagedModels();
    void onModelsReply(QNetworkReply *reply);

    void runSelfTestRequest(const ResolvedConnection &conn,
                            std::shared_ptr<QFutureInterface<SelfTestResult>> promise);

    // §7.5 error matrix: map a raw server line / failure to a human message.
    QString describeServerFailure() const;
    static QString translateServerLine(const QString &line);
    QString lastLogLines(int count) const;

    static QImage makeTestImage();

    // Constructs a server from the current settings (owned here).
    class LlamaServerProcess *m_server = nullptr;

    SettingsStore &m_settings;

    // In-flight managed resolve (dedup: all concurrent callers share it). Each
    // pending caller's callback is queued here and drained by completeResolve().
    std::vector<std::function<void(const ResolvedConnection &)>> m_resolveCallbacks;
    bool m_resolveInProgress = false;

    QNetworkAccessManager *m_modelsNet = nullptr;
    QString m_modelsBaseUrl;   // base url captured at resolve time

    OpenAiProvider *m_selftestProvider = nullptr;

    // QML-friendly self-test state.
    bool m_selftestRunning = false;
    bool m_selftestOk = false;
    QString m_selftestMessage;

    RuntimeState m_state = RuntimeState::NotConfigured;
    AppBusyState m_busyState = AppBusyState::Idle;
    QString m_statusMessage;
    int m_loadProgressPercent = -1;
    bool m_configValid = false;
    bool m_lockedOut = false;

signals:
    void stateChanged();
    void busyStateChanged();
    void statusMessageChanged();
    void loadProgressChanged();
    void configValidChanged();
    void lockedOutChanged();
    void serverLogChanged();
    void selftestFinished();
};

}  // namespace llocr