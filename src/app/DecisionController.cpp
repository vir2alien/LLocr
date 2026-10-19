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

void DecisionController::judgeBlock(const QImage &image,
                                    const QString &blockType,
                                    const QString &recognizedText,
                                    const QString &notation,
                                    const QString &question,
                                    const QString &criteriaTrue,
                                    const QString &criteriaFalse,
                                    const QJsonObject &questions)
{
    if (busy() || image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::Decision,
        StatusMessage::translate(trContext(), "Stopped before the decision check started."),
        [this, image, blockType, recognizedText, notation, question, criteriaTrue, criteriaFalse, questions](const ResolvedConnection &conn) {
            DecisionRequest request;
            request.image = image;
            request.blockType = blockType;
            request.recognizedText = recognizedText;
            request.notation = notation;
            request.question = question;
            request.criteriaTrue = criteriaTrue;
            request.criteriaFalse = criteriaFalse;
            request.questions = questions;

            m_watcher.setFuture(m_model->judge(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &message) {
            emit judgeFinished(DecisionResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
