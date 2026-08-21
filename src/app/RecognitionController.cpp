#include "app/RecognitionController.h"

#include <QDebug>

#include "app/SettingsStore.h"
#include "core/ProviderConfig.h"

namespace llocr {

RecognitionController::RecognitionController(SettingsStore &settings,
                                             ImageProvider imageProvider,
                                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
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
    m_prompt = prompt;
    m_stopRequested = false;
    m_recognizeAll = false;
    setBusy(true);
    recognizePage(index);
}

void RecognitionController::startAll(int totalPages, const QString& prompt)
{
    if (m_busy)
        return;
    m_totalPages = totalPages;
    m_prompt = prompt;
    m_stopRequested = false;
    m_recognizeAll = true;
    setBusy(true);
    recognizePage(0);
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
    const OcrRequest request = buildRequest(image, m_prompt);
    m_watcher.setFuture(m_provider->recognize(request, buildConfig()));
}

ProviderConfig RecognitionController::buildConfig() const
{
    ProviderConfig config;
    config.apiKey = m_settings.apiKey();
    config.baseUrl = m_settings.baseUrl();
    config.maxTokens = m_settings.maxTokens();
    config.modelName = m_settings.modelName();
    config.parserId = m_settings.parserId();
    config.prompt = m_prompt;
    config.temperature = m_settings.temperature();
    config.timeoutMs = m_settings.connectionTimeoutMs();
    return config;
}

OcrRequest RecognitionController::buildRequest(const QImage &image, const QString &prompt) const
{
    OcrRequest request;
    request.image = image;
    request.prompt = prompt;
    request.modelId = m_settings.modelName();
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
    emit statusRequested(tr("Stopping…"));
}

void RecognitionController::finishRun()
{
    m_recognizingIndex = -1;
    m_recognizeAll = false;
    setBusy(false);
    emit runFinished();
}

void RecognitionController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}  // namespace llocr