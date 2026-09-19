#include "app/CheckController.h"

#include <QFutureWatcher>

#include "app/RequestProfileStore.h"
#include "models/QwenGeneralModel.h"

namespace llocr {

CheckController::CheckController(RequestProfileStore &requestProfiles,
                                 RuntimeController &runtime,
                                 QObject *parent)
    : QObject(parent)
    , m_requestProfiles(requestProfiles)
    , m_runtime(runtime)
    , m_model(std::make_unique<QwenGeneralModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        const CheckResult result = m_watcher.future().resultCount() > 0
                                       ? m_watcher.result()
                                       : CheckResult::makeError(tr("No response"));
        emit checkFinished(result.success, result.text, result.errorMessage);
        setBusy(false);
    });
}

QList<RequestParameter> CheckController::requestParameters() const
{
    // The check request parameters live in the validate request profile
    // (Settings → Check model → Request).
    return m_requestProfiles.activeProfile().parameters;
}

ConnectionConfig CheckController::buildConfig(const ResolvedConnection &conn) const
{
    ConnectionConfig config;
    config.apiKey = conn.apiKey;
    config.baseUrl = conn.baseUrl;
    config.timeoutMs = conn.timeoutMs;
    return config;
}

void CheckController::checkBlock(const QImage &image, const QString &recognizedText,
                                 const QString &prompt)
{
    if (m_busy || image.isNull())
        return;

    setBusy(true);

    m_runtime.ensureConnectionReady(this, ConnectionRole::Check,
                                    [this, image, recognizedText, prompt](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (conn.baseUrl.isEmpty()) {
            emit statusRequested(conn.error.isEmpty()
                                     ? tr("Connection is not configured.")
                                     : conn.error);
            emit checkFinished(false, QString(), conn.error);
            setBusy(false);
            return;
        }

        CheckRequest request;
        request.image = image;
        request.recognizedText = recognizedText;
        request.prompt = prompt;
        request.modelId = conn.modelId;
        request.parameters = requestParameters();

        m_watcher.setFuture(m_model->check(request, buildConfig(conn)));
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