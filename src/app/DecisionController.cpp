#include "app/DecisionController.h"

#include <QFutureWatcher>

namespace llocr {

DecisionController::DecisionController(RuntimeController &runtime, QObject *parent) : QObject(parent), m_runtime(runtime), m_model(std::make_unique<DecisionModel>())
{
    connect(&m_watcher, &QFutureWatcher<DecisionResult>::finished, this, [this]() {
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("DecisionController", "Decision check stopped."));
            setBusy(false);
            return;
        }
        const DecisionResult result = m_watcher.future().resultCount() > 0 ? m_watcher.result() : DecisionResult::makeError(StatusMessage::translate("DecisionController", "No response"));
        emit judgeFinished(result);
        setBusy(false);
    });
}

void DecisionController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_model)
        m_model->abort();
    emit statusRequested(StatusMessage::translate("DecisionController", "Stopping…"));
}

void DecisionController::judgeBlock(const QImage &image, const QString &recognizedText, const QString &question)
{
    if (m_busy || image.isNull())
        return;

    setBusy(true);
    m_stopRequested = false;

    m_runtime.ensureConnectionReady(this, ConnectionRole::Decision, [this, image, recognizedText, question](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("DecisionController", "Stopped before the decision check started."));
            setBusy(false);
            return;
        }
        if (conn.baseUrl.isEmpty()) {
            const StatusMessage message = conn.error.isEmpty() ? StatusMessage::translate("DecisionController", "Connection is not configured.") : StatusMessage::literal(conn.error);
            emit statusRequested(message);
            emit judgeFinished(DecisionResult::makeError(message));
            setBusy(false);
            return;
        }

        DecisionRequest request;
        request.image = image;
        request.stateText = recognizedText;
        request.question = question;

        m_watcher.setFuture(m_model->judge(request, conn.toConnectionConfig()));
    });
}

void DecisionController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}  // namespace llocr
