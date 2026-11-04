#include "app/BlockQueue.h"

#include <QTimer>

#include "core/BoxTransitions.h"

namespace llocr {

BlockQueue::BlockQueue(Deps deps, QObject *parent) : QObject(parent), m_deps(std::move(deps)) {}

bool BlockQueue::run(const QList<Task> &tasks)
{
    if (m_active || tasks.isEmpty())
        return false;
    if (!m_deps.gate.tryAcquire(this))
        return false;

    ++m_runGeneration;
    m_error = StatusMessage();
    m_finished = false;
    beginRun();
    m_queue = tasks;
    m_page = -1;
    m_boxIndex = -1;
    m_total = tasks.size();
    m_done = 0;
    m_active = true;
    // The run-start readouts (the facade resets its error line, finished flag
    // and progress source here) must be fresh by the time the state change
    // announces the run: the single notify carries the new run's values, as
    // the old controller's single start-time state change did.
    emit runStarted();
    emit stateChanged();

    QTimer::singleShot(0, this, [this]() { startNext(); });
    return true;
}

void BlockQueue::stop()
{
    m_queue.clear();
    if (m_active) {
        m_active = false;
        m_page = -1;
        m_boxIndex = -1;
        emit stateChanged();
        m_deps.gate.release(this);
        // The waiter has to hear it: a stage that waits for the queue would
        // wait for a work list that is gone (ADR 257). The gate is free by
        // then — a stage that starts the next queue inside this signal must
        // not be refused by its own run.
        emit finished();
    }
    if (requestBusy())
        stopRequest();
}

void BlockQueue::startNext()
{
    if (!m_active)
        return;

    if (m_queue.isEmpty()) {
        finish();
        return;
    }

    const Task task = m_queue.takeFirst();
    m_page = task.page;
    m_boxIndex = task.box;
    if (!m_deps.document.isValidIndex(m_page)) {
        advance();
        return;
    }
    const DocumentPage &docPage = m_deps.document.page(m_page);
    if (!docPage.recognized || docPage.result.pages.isEmpty()) {
        advance();
        return;
    }
    const QList<BoundingBox> &boxes = docPage.result.pages.first().boxes;
    if (m_boxIndex < 0 || m_boxIndex >= boxes.size()) {
        advance();
        return;
    }
    const BoundingBox &box = boxes.at(m_boxIndex);
    const QString text = BoxTransitions::effectiveText(box);
    const QImage crop = m_deps.cropProvider(m_page, m_boxIndex);
    if (crop.isNull()) {
        advance();
        return;
    }

    startTask(task, box, crop, text);
}

// The shared tail of every result, requested or produced locally: the
// progress moves and the next task starts.
void BlockQueue::advance()
{
    ++m_done;
    emit stateChanged();
    QTimer::singleShot(0, this, [this]() { startNext(); });
}

// An error the run has not reported yet reaches the problem log once; the
// same error from the next block is not reported again.
bool BlockQueue::reportNewError(const StatusMessage &error)
{
    if (error.isEmpty() || error == m_error)
        return false;
    m_error = error;
    emit problemReported(error);
    return true;
}

void BlockQueue::finish()
{
    if (!m_active)
        return;
    m_active = false;
    m_queue.clear();
    m_page = -1;
    m_boxIndex = -1;
    m_finished = true;
    finishRun();
    // The gate is released and the waiter hears it before the state change:
    // a stage that starts the next queue inside finished() must not be
    // refused by the run that just ended, and the facade's finished readout
    // (the check-finished flag) must be set by the time the final notify
    // reaches QML.
    m_deps.gate.release(this);
    emit finished();
    emit stateChanged();
}

}  // namespace llocr
