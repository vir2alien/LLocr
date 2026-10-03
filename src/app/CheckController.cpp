#include "app/CheckController.h"

#include <QFutureWatcher>

#include "config/RequestProfileStore.h"
#include "models/GeneralPurposeModel.h"

namespace llocr {

CheckController::CheckController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : QObject(parent), m_requestProfiles(requestProfiles), m_runtime(runtime), m_model(std::make_unique<GeneralPurposeModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("CheckController", "Check stopped."));
            setBusy(false);
            return;
        }
        const CheckResult result = m_watcher.future().resultCount() > 0 ? m_watcher.result() : CheckResult::makeError(StatusMessage::translate("CheckController", "No response"));
        emit checkFinished(result);
        setBusy(false);
    });
}

void CheckController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_model)
        m_model->abort();
    m_runtime.cancelPendingStart();
    emit statusRequested(StatusMessage::translate("CheckController", "Stopping…"));
}

QList<RequestParameter> CheckController::requestParameters() const
{
    return m_requestProfiles.activeProfile().parameters;
}

void CheckController::checkBlock(const QImage &image, const QString &recognizedText, const QString &systemPrompt, const QString &typePrompt)
{
    if (m_busy || image.isNull())
        return;

    setBusy(true);
    m_stopRequested = false;

    m_runtime.ensureConnectionReady(this, ConnectionRole::Check, [this, image, recognizedText, systemPrompt, typePrompt](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("CheckController", "Stopped before check started."));
            setBusy(false);
            return;
        }
        if (conn.baseUrl.isEmpty()) {
            const StatusMessage message = conn.error.isEmpty() ? StatusMessage::translate("CheckController", "Connection is not configured.") : StatusMessage::literal(conn.error);
            emit statusRequested(message);
            emit checkFinished(CheckResult::makeError(message));
            setBusy(false);
            return;
        }

        CheckRequest request;
        request.image = image;
        request.recognizedText = recognizedText;
        request.systemPrompt = systemPrompt;
        request.typePrompt = typePrompt;
        request.modelId = conn.modelId;
        request.parameters = requestParameters();

        m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
    });
}

void CheckController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}  // namespace llocr
