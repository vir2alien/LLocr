#include "app/DecisionController.h"

namespace llocr {

DecisionController::DecisionController(RuntimeController &runtime, QObject *parent) : ModelRequestController(runtime, "DecisionController", parent), m_model(std::make_unique<DecisionModel>())
{
    connect(&m_watcher, &QFutureWatcher<DecisionResult>::finished, this, [this]() {
        if (stopRequested()) {
            emit statusRequested(StatusMessage::translate(trContext(), "Decision check stopped."));
            finish();
            return;
        }
        emit judgeFinished(watcherResult(m_watcher));
        finish();
    });
}

void DecisionController::abortRequest()
{
    if (m_model)
        m_model->abort();
}

void DecisionController::judgeVisual(const QImage &crop, const QImage &renderImage, const QString &recognizedText)
{
    DecisionRequest request;
    request.visualDiff = true;
    request.image = crop;
    request.renderImage = renderImage;
    request.recognizedText = recognizedText;
    ask(request);
}

void DecisionController::ask(const DecisionRequest &request)
{
    if (busy() || request.image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::Decision,
        StatusMessage::translate(trContext(), "Stopped before the decision check started."),
        [this, request](const ResolvedConnection &conn) { m_watcher.setFuture(m_model->judge(request, conn.toConnectionConfig())); },
        [this](const StatusMessage &message) {
            emit judgeFinished(DecisionResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
