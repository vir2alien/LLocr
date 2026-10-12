#pragma once

#include <functional>

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include "core/StatusMessage.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

// Shared scaffold of the per-role request controllers (CheckController,
// DecisionController, LayoutController): the busy flag with its NOTIFY, the
// stop flag honoured across both the resolve and the in-flight phases, and
// the ensureConnectionReady error handling. Subclasses own the model call,
// the result watcher and the finished signal.
class ModelRequestController : public QObject
{
    Q_OBJECT

public:
    bool busy() const { return m_busy; }
    void stop();

signals:
    void busyChanged();
    void statusRequested(const StatusMessage &message);

protected:
    ModelRequestController(RuntimeController &runtime, const char *trContext, QObject *parent = nullptr);

    bool stopRequested() const { return m_stopRequested; }
    const char *trContext() const { return m_trContext; }

    // Runs `start` with the connection resolved for the role; the
    // stopped-while-resolving case is reported and ends the run. The
    // not-configured case is reported through statusRequested and handed to
    // `fail`, which decides what the request resolves to — ending the run is
    // the subclass's call there (the layout queue keeps going).
    void
    beginRequest(ConnectionRole role, const StatusMessage &stoppedWhileResolving, const std::function<void(const ResolvedConnection &)> &start, const std::function<void(const StatusMessage &)> &fail);

    // Result of a watcher whose request was aborted before it produced one.
    template <typename Result> Result watcherResult(const QFutureWatcher<Result> &watcher) const
    {
        return watcher.future().resultCount() > 0 ? watcher.result() : Result::makeError(StatusMessage::translate(m_trContext, "No response"));
    }

    void setBusy(bool busy);
    void finish() { setBusy(false); }

    virtual void abortRequest() = 0;
    virtual void resetRun() {}

private:
    RuntimeController &m_runtime;
    const char *m_trContext;
    bool m_busy = false;
    bool m_stopRequested = false;
};

}  // namespace llocr
