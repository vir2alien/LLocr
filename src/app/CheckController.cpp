#include "app/CheckController.h"

#include "config/RequestProfileStore.h"

namespace llocr {

CheckController::CheckController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : ModelRequestController(runtime, "CheckController", parent), m_requestProfiles(requestProfiles), m_model(std::make_unique<BlockRecognitionModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (stopRequested()) {
            emit statusRequested(StatusMessage::translate(trContext(), "Check stopped."));
            finish();
            return;
        }
        emit checkFinished(watcherResult(m_watcher));
        finish();
    });
}

void CheckController::abortRequest()
{
    if (m_model)
        m_model->abort();
}

QList<RequestParameter> CheckController::requestParameters() const
{
    return m_requestProfiles.activeProfile().parameters;
}

void CheckController::checkBlock(const QImage &image, const QString &systemPrompt, const QString &typePrompt)
{
    if (busy() || image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::BlockRecognition,
        StatusMessage::translate(trContext(), "Stopped before check started."),
        [this, image, systemPrompt, typePrompt](const ResolvedConnection &conn) {
            CheckRequest request;
            request.image = image;
            request.systemPrompt = systemPrompt;
            request.typePrompt = typePrompt;
            request.modelId = conn.modelId;
            request.parameters = requestParameters();

            m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &message) {
            emit checkFinished(CheckResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
