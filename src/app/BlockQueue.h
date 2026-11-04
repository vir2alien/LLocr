#pragma once

#include <functional>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include "app/BlockRunGate.h"
#include "app/BlockWorkList.h"
#include "app/DocumentModel.h"
#include "core/CheckResult.h"
#include "core/StatusMessage.h"

namespace llocr {

// The sequential runner over a block work list: one task at a time, with the
// guards, the progress and the stop every block queue shares (ADR 257). A
// role queue (decision, recheck, remedy) adds the request it drives and the
// signal its results travel on; the document is never mutated here — the
// results go out as signals (ADR 86).
class BlockQueue : public QObject
{
    Q_OBJECT

public:
    // One block of the work list: the page and the box on it. The work-list
    // vocabulary lives on BlockWorkList; the queue runs the lists it collects.
    using Task = BlockWorkList::Task;

    struct Deps {
        DocumentModel &document;
        BlockRunGate &gate;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
    };

    BlockQueue(Deps deps, QObject *parent = nullptr);

    // Takes the work list if no other run holds the gate; a refusal is not an
    // error — the caller reports it.
    bool run(const QList<Task> &tasks);
    // The run stops and the waiter hears it (ADR 257): the finished signal
    // fires even though nothing was delivered.
    void stop();

    bool active() const { return m_active; }
    // The request of the run in flight: it stays up while a stopped request
    // is winding down, and a new run must not start under it.
    virtual bool requestBusy() const = 0;
    bool busy() const { return m_active || requestBusy(); }
    // The last run completed on its own; a stop leaves the flag alone.
    bool finishedFlag() const { return m_finished; }
    int progressDone() const { return m_done; }
    int progressTotal() const { return m_total; }
    QString errorMessage() const { return m_error.text(); }

signals:
    void stateChanged();
    // A run took the work list: the readouts that describe "the run" (the
    // error line, the finished flag, the progress source) reset here, not on
    // the per-request state changes that follow.
    void runStarted();
    void statusRequested(const llocr::StatusMessage &message);
    void problemReported(const llocr::StatusMessage &message);
    void finished();

protected:
    // The task passed the queue's guards: a valid page, a box in range, a
    // crop, the effective text. The subclass resolves the prompts and sends
    // the request; the result comes back through its own signal.
    virtual void startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text) = 0;
    virtual void stopRequest() = 0;
    // The run's own tail, before the state change and the finished signal —
    // the summary a role owes its user (the read run's counts, ADR 261).
    virtual void finishRun() {}
    // The per-run state a role resolves once (thresholds, counters).
    virtual void beginRun() {}

    void advance();
    bool reportNewError(const StatusMessage &error);
    int taskPage() const { return m_page; }
    int taskBox() const { return m_boxIndex; }

private:
    void startNext();
    void finish();

    Deps m_deps;
    QList<Task> m_queue;
    int m_page = -1;
    int m_boxIndex = -1;
    bool m_active = false;
    bool m_finished = false;
    int m_total = 0;
    int m_done = 0;
    StatusMessage m_error;
};

}  // namespace llocr
