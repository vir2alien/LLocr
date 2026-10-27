#include "app/ModelRequestController.h"

namespace llocr {

ModelRequestController::ModelRequestController(RuntimeController &runtime, const char *trContext, QObject *parent) : QObject(parent), m_runtime(runtime), m_trContext(trContext) {}

void ModelRequestController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    abortRequest();
    emit statusRequested(StatusMessage::translate(m_trContext, "Stopping…"));
}

void ModelRequestController::beginRequest(ConnectionRole role,
                                          const StatusMessage &stoppedWhileResolving,
                                          const std::function<void(const ResolvedConnection &)> &start,
                                          const std::function<void(const StatusMessage &)> &fail)
{
    m_stopRequested = false;
    m_runtime.ensureConnectionReady(this, role, [this, stoppedWhileResolving, start, fail](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (m_stopRequested) {
            emit statusRequested(stoppedWhileResolving);
            setBusy(false);
            return;
        }
        if (!conn.error.isEmpty() || conn.baseUrl.isEmpty()) {
            const StatusMessage message = conn.error.isEmpty() ? StatusMessage::translate(m_trContext, "Connection is not configured.") : StatusMessage::literal(conn.error);
            emit statusRequested(message);
            fail(message);
            return;
        }
        start(conn);
    });
}

void ModelRequestController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (!busy)
        resetRun();
    emit busyChanged();
}

}  // namespace llocr
