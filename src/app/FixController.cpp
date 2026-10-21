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

void FixController::fixBlock(const QImage &image, const QString &recognizedText, const QString &systemPrompt, const QString &typePrompt, const QString &blockType, int maxOutput)
{
    if (busy() || image.isNull())
        return;

    setBusy(true);

    beginRequest(
        ConnectionRole::Fix,
        StatusMessage::translate(trContext(), "Stopped before the fix started."),
        [this, image, recognizedText, systemPrompt, typePrompt, blockType, maxOutput](const ResolvedConnection &conn) {
            CheckRequest request;
            request.image = image;
            request.systemPrompt = systemPrompt;
            request.typePrompt = typePrompt;
            request.recognizedText = recognizedText;
            request.blockType = blockType;
            request.modelId = conn.modelId;
            request.parameters = requestParameters();
            // The per-block budget overrides the profile's max_tokens only
            // where the profile ships a per-block entry; a user-edited value
            // stays untouched for the other types.
            if (maxOutput > 0) {
                for (RequestParameter &parameter : request.parameters) {
                    if (parameter.name == QLatin1String("max_tokens")) {
                        parameter.value = maxOutput;
                        break;
                    }
                }
            }
            request.task = CheckRequest::Task::Fix;

            m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &message) {
            emit fixFinished(CheckResult::makeError(message));
            finish();
        });
}

}  // namespace llocr
