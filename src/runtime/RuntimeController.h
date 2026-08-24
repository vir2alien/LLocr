#pragma once

#include <QFuture>
#include <QFutureInterface>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "runtime/ConnectionMode.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeState.h"

namespace llocr {

class SettingsStore;

// Facade over every managed-runtime concern (process, downloads, installs,
// model selection). A single instance is created in main.cpp — before the QML
// engine loads — and lives for the whole process (ADR 36). QML must NOT
// instantiate it; it only consumes the already-registered singleton.
//
// Recognition goes through exactly one async entry point:
// ensureConnectionReady(). In External it resolves immediately from settings;
// in Managed (Stage G-core) it starts the server, waits for /health, and
// returns the computed ResolvedConnection. Stage A ships the External path and
// the skeleton state/properties.
class RuntimeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int busyState READ busyStateInt NOTIFY busyStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool configValid READ configValid NOTIFY configValidChanged)
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY lockedOutChanged)
    Q_PROPERTY(QString serverLog READ serverLog NOTIFY serverLogChanged)

public:
    explicit RuntimeController(SettingsStore &settings, QObject *parent = nullptr);

    // --- QML-visible state ----------------------------------------------
    RuntimeState state() const { return m_state; }
    AppBusyState busyState() const { return m_busyState; }
    QString statusMessage() const { return m_statusMessage; }
    bool configValid() const { return m_configValid; }
    bool lockedOut() const { return m_lockedOut; }
    /// Ring-buffer tail of the managed server log, for the log window.
    QString serverLog() const;

    // --- ARM-coordinated resolution --------------------------------------
    // External:  resolves immediately from SettingsStore.
    // Managed:   (Stage G-core) starts the process, waits for /health.
    //            Concurrent callers share one future.
    QFuture<ResolvedConnection> ensureConnectionReady();

    /// Cancels a pending startup (called by RecognitionController::stop() while
    /// the app is in StartingRuntime). No-op in External.
    void cancelPendingStart();

    /// Runs an independent self-test (Stage G-core; master wizard and the
    /// "Check" button use it). Placeholder returns NotConfigured in External.
    QFuture<ResolvedConnection> runSelfTest();

    // --- Wiring helpers --------------------------------------------------
    void setSingleInstanceHeld(bool held);

    // --- Stage B: managed server lifecycle (Runtime settings tab) --------
    /// Starts the managed llama-server with the current launch/* settings.
    /// Empty on success; an error message otherwise. Requires a valid binary.
    Q_INVOKABLE QString startServer();
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void restartServer();
    /// Runs the locator probe synchronously and stores the summary in
    /// `probeResult` (Stage B UI). Returns the same summary.
    Q_INVOKABLE QString probeRuntimePath(const QString &path);
    /// Auto-discovers a llama-server binary via RuntimeLocator::autoDiscover()
    /// and returns the found path (or an empty string).
    Q_INVOKABLE QString autoDiscoverPath();
    /// Blocking shutdown (main.cpp ~aboutToQuit path). §5.5.
    void shutdownSync();

    static ConnectionMode modeFromSettings(const SettingsStore &settings);

private:
    int stateInt() const { return static_cast<int>(m_state); }
    int busyStateInt() const { return static_cast<int>(m_busyState); }

    void setState(RuntimeState next);
    void setBusyState(AppBusyState next);
    void setStatusMessage(const QString &msg);

    // External path: build ResolvedConnection directly from settings.
    ResolvedConnection resolveExternal() const;

    // Constructs the LlamaServerProcess options from the current settings.
    class LlamaServerProcess *m_server = nullptr;

    SettingsStore &m_settings;

    RuntimeState m_state = RuntimeState::NotConfigured;
    AppBusyState m_busyState = AppBusyState::Idle;
    QString m_statusMessage;
    bool m_configValid = false;
    bool m_lockedOut = false;

signals:
    void stateChanged();
    void busyStateChanged();
    void statusMessageChanged();
    void configValidChanged();
    void lockedOutChanged();
    void serverLogChanged();
};

}  // namespace llocr