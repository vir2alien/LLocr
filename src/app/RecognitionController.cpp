#include "app/RecognitionController.h"

#include <QDebug>
#include <QFutureWatcher>

#include "app/SettingsStore.h"
#include "core/ProviderConfig.h"

namespace llocr {

RecognitionController::RecognitionController(SettingsStore &settings,
                                             RuntimeController &runtime,
                                             ImageProvider imageProvider,
                                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_runtime(runtime)
    , m_imageProvider(imageProvider)
{
    m_provider = std::make_unique<OpenAiProvider>();

    connect(&m_watcher, &QFutureWatcher<OcrResult>::finished, this,
            &RecognitionController::onRecognitionFinished);
}

void RecognitionController::startCurrent(int index, int totalPages, const QString& prompt)
{
    if (m_busy)
        return;
    m_totalPages = totalPages;
    m_startIndex = index;
    m_prompt = prompt;
    m_stopRequested = false;
    m_recognizeAll = false;
    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::startAll(int totalPages, const QString& prompt)
{
    if (m_busy)
        return;
    m_totalPages = totalPages;
    m_startIndex = 0;
    m_prompt = prompt;
    m_stopRequested = false;
    m_recognizeAll = true;
    setBusy(true);
    ensureConnectionReady();
}

// Single async entry point (ADR 37): the runtime resolves the connection
// (External immediate, Managed later — Stage G-core), then pages flow.
void RecognitionController::ensureConnectionReady()
{
    m_connectionReady = false;
    const QFuture<ResolvedConnection> future = m_runtime.ensureConnectionReady();
    auto *watcher = new QFutureWatcher<ResolvedConnection>(this);
    connect(watcher, &QFutureWatcher<ResolvedConnection>::finished, this,
            [this, watcher]() {
                const ResolvedConnection conn = watcher->result();
                watcher->deleteLater();
                if (!m_busy)
                    return;  // stopped while resolving
                if (conn.baseUrl.isEmpty()) {
                    emit statusRequested(tr("Connection is not configured."));
                    finishRun();
                    return;
                }
                m_connection = conn;
                m_connectionReady = true;
                recognizePage(m_startIndex);
            });
    watcher->setFuture(future);
}

void RecognitionController::recognizePage(int index)
{
    if (index < 0 || index >= m_totalPages) {
        finishRun();
        return;
    }

    m_recognizingIndex = index;
    emit statusRequested(tr("Recognizing page %1 of %2…").arg(index + 1).arg(m_totalPages));

    const QImage image = m_imageProvider(index);
    const OcrRequest request = buildRequest(image, m_connection);
    m_watcher.setFuture(m_provider->recognize(request, buildConfig(m_connection)));
}

ProviderConfig RecognitionController::buildConfig(const ResolvedConnection &conn) const
{
    ProviderConfig config;
    config.apiKey = conn.apiKey;
    config.baseUrl = conn.baseUrl;
    config.timeoutMs = conn.timeoutMs;
    return config;
}

OcrRequest RecognitionController::buildRequest(const QImage &image,
                                               const ResolvedConnection &conn) const
{
    OcrRequest request;
    request.image = image;
    request.prompt = m_prompt;
    request.modelId = conn.modelId;
    request.temperature = m_settings.temperature();
    request.maxTokens = m_settings.maxTokens();
    request.dryMultiplier = m_settings.dryMultiplier();
    request.dryBase = m_settings.dryBase();
    request.dryAllowedLength = m_settings.dryAllowedLength();
    request.dryPenaltyLastN = m_settings.dryPenaltyLastN();
    return request;
}

void RecognitionController::onRecognitionFinished()
{
    if (m_recognizingIndex < 0)
        return;

    const OcrResult raw = m_watcher.future().resultCount() > 0
                              ? m_watcher.result()
                              : OcrResult::makeError(tr("No response"));
    const int index = m_recognizingIndex;

    if (!raw.success) {
        if (m_stopRequested)
            emit statusRequested(tr("Stopped at page %1.").arg(index + 1));
        else {
            emit statusRequested(tr("Error on page %1: %2").arg(index + 1).arg(raw.errorMessage));
            qDebug() << tr("Error on page %1: %2").arg(index + 1).arg(raw.errorMessage);
        }
        finishRun();
        return;
    }

    emit rawResultReady(index, raw);

    if (m_stopRequested) {
        emit statusRequested(tr("Stopped after page %1.").arg(index + 1));
        finishRun();
        return;
    }

    if (m_recognizeAll) {
        const int next = index + 1;
        if (next < m_totalPages) {
            recognizePage(next);
            return;
        }
    }

    emit statusRequested(tr("Done."));
    finishRun();
}

void RecognitionController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_provider)
        m_provider->abort();
    m_runtime.cancelPendingStart();
    emit statusRequested(tr("Stopping…"));
}

void RecognitionController::finishRun()
{
    m_recognizingIndex = -1;
    m_recognizeAll = false;
    m_connectionReady = false;
    setBusy(false);
}

void RecognitionController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}  // namespace llocr