#include "app/FixController.h"

#include "config/RequestProfileStore.h"

namespace llocr {

FixController::FixController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : ModelRequestController(runtime, "FixController", parent), m_requestProfiles(requestProfiles), m_model(std::make_unique<FixModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (stopRequested()) {
            emit statusRequested(StatusMessage::translate(trContext(), "Fix stopped."));
            finish();
            return;
        }
        emit fixFinished(watcherResult(m_watcher));
        finish();
    });
}

void FixController::abortRequest()
{
    if (m_model)
        m_model->abort();
}

QList<RequestParameter> FixController::requestParameters() const
{
    return m_requestProfiles.activeProfile().parameters;
}

void FixController::fixBlock(const QImage &image, const QString &recognizedText, const QString &systemPrompt, const QString &typePrompt)
{
    if (busy() || image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::Fix,
        StatusMessage::translate(trContext(), "Stopped before the fix started."),
        [this, image, recognizedText, systemPrompt, typePrompt](const ResolvedConnection &conn) {
            CheckRequest request;
            request.image = image;
            request.systemPrompt = systemPrompt;
            request.typePrompt = typePrompt;
            request.recognizedText = recognizedText;
            request.modelId = conn.modelId;
            request.parameters = requestParameters();
            request.task = CheckRequest::Task::Fix;

            m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &message) {
            emit fixFinished(CheckResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
