#include <QFuture>
#include <QFutureInterface>

#include "app/SettingsStore.h"
#include "runtime/RuntimeController.h"

namespace llocr {

RuntimeController::RuntimeController(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // configValid (§1.4): has a server binary been chosen and does the model
    // file exist? In Stage A neither a binary nor a model is mandatory yet, so
    // it reflects whether a server path is configured. Stage B/C tighten this.
    m_configValid = !m_settings.serverPath().trimmed().isEmpty();

    // Keep the QML-visible configValid in sync as the user edits Settings.
    connect(&m_settings, &SettingsStore::serverPathChanged, this, [this]() {
        const bool valid = !m_settings.serverPath().trimmed().isEmpty();
        if (valid == m_configValid)
            return;
        m_configValid = valid;
        emit configValidChanged();
    });
}

void RuntimeController::setSingleInstanceHeld(bool held)
{
    if (m_lockedOut == held)
        return;
    m_lockedOut = held;
    emit lockedOutChanged();
}

void RuntimeController::setState(RuntimeState next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void RuntimeController::setBusyState(AppBusyState next)
{
    if (m_busyState == next)
        return;
    m_busyState = next;
    emit busyStateChanged();
}

void RuntimeController::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
{
    return settings.connectionMode() == QString::fromUtf8(SettingsStore::kModeManaged)
               ? ConnectionMode::Managed
               : ConnectionMode::External;
}

ResolvedConnection RuntimeController::resolveExternal() const
{
    ResolvedConnection conn;
    conn.baseUrl = m_settings.baseUrl();
    conn.apiKey = m_settings.apiKey();
    conn.modelId = m_settings.modelName();
    conn.timeoutMs = m_settings.connectionTimeoutMs();
    return conn;
}

QFuture<ResolvedConnection> RuntimeController::ensureConnectionReady()
{
    // Stage A: only the External path (ADR 26). Managed is wired in Stage G-core.
    // Cancelling a pending start is a no-op here since External resolves
    // synchronously and never transitions through StartingRuntime.
    QFutureInterface<ResolvedConnection> promise;
    promise.reportStarted();
    const ResolvedConnection conn = resolveExternal();
    promise.reportResult(conn);
    promise.reportFinished();
    return promise.future();
}

QFuture<ResolvedConnection> RuntimeController::runSelfTest()
{
    // Placeholder until Stage G-core. External has nothing to self-test here.
    return ensureConnectionReady();
}

void RuntimeController::cancelPendingStart()
{
    // Managed-only. In External there is never a pending start to cancel.
}

}  // namespace llocr