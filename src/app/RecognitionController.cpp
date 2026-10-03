#include "app/RecognitionController.h"

#include <QFutureWatcher>

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/ConnectionConfig.h"
#include "core/ModelProfiles.h"
#include "models/OcrModel.h"

namespace llocr {

RecognitionController::RecognitionController(
    SettingsStore &settings, RuntimeController &runtime, RequestProfileStore &requestProfiles, ImageProvider imageProvider, QObject *parent, std::function<bool(int)> skipPage)
    : QObject(parent), m_settings(settings), m_runtime(runtime), m_requestProfiles(requestProfiles), m_imageProvider(imageProvider), m_skipPage(std::move(skipPage))
{
    connect(&m_watcher, &QFutureWatcher<OcrResult>::finished, this, &RecognitionController::onRecognitionFinished);
}

void RecognitionController::startCurrent(int index, int totalPages)
{
    if (m_busy)
        return;
    m_totalPages = totalPages;
    m_startIndex = index;
    m_stopRequested = false;
    m_recognizeAll = false;
    resolveModel();
    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::startAll(int totalPages)
{
    if (m_busy)
        return;
    m_totalPages = totalPages;
    m_startIndex = 0;
    m_stopRequested = false;
    m_recognizeAll = true;
    resolveModel();
    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::resolveModel()
{
    m_skippedPages = 0;
    const QString recipeId = m_settings.modelRecipeId();
    if (m_model && m_modelId == recipeId)
        return;
    m_model = OcrModel::create(recipeId);
    m_modelId = recipeId;
}

QString RecognitionController::promptText() const
{
    if (!m_model)
        return QString();
    const QList<ModelProfiles::Prompt> variants = m_model->promptVariants();
    return variants.isEmpty() ? QString() : variants.constFirst().text;
}

void RecognitionController::ensureConnectionReady()
{
    m_runtime.ensureConnectionReady(this, [this](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (conn.baseUrl.isEmpty()) {
            if (m_stopRequested)
                emit statusRequested(StatusMessage::translate("RecognitionController", "Stopped before recognition started."));
            else
                emit statusRequested(conn.error.isEmpty() ? StatusMessage::translate("RecognitionController", "Connection is not configured.") : StatusMessage::literal(conn.error));
            finishRun();
            return;
        }
        m_connection = conn;
        recognizePage(m_startIndex);
    });
}

void RecognitionController::recognizePage(int index)
{
    while (index >= 0 && index < m_totalPages && m_skipPage && m_skipPage(index)) {
        ++m_skippedPages;
        if (!m_recognizeAll) {
            emit statusRequested(StatusMessage::translate("RecognitionController",
                                                          "Page %1 is a blank replacement for an unreadable page; "
                                                          "recognition skipped.")
                                     .arg(index + 1));
            finishRun();
            return;
        }
        ++index;
    }
    if (index == m_totalPages && m_skippedPages > 0)
        emit statusRequested(StatusMessage::translate("RecognitionController", "Recognition finished. Skipped %1 unreadable page(s).").arg(m_skippedPages));
    if (index < 0 || index >= m_totalPages) {
        finishRun();
        return;
    }

    m_recognizingIndex = index;
    emit statusRequested(StatusMessage::translate("RecognitionController", "Recognizing page %1 of %2…").arg(index + 1).arg(m_totalPages));

    QString imageError;
    const QImage image = m_imageProvider(index, imageError);
    if (image.isNull()) {
        emit statusRequested(StatusMessage::translate("RecognitionController", "Error on page %1: %2")
                                 .arg(index + 1)
                                 .arg(imageError.isEmpty() ? StatusMessage::translate("RecognitionController", "the page has no image to recognize").text() : imageError));
        finishRun();
        return;
    }
    const OcrRequest request = buildRequest(image, m_connection);
    m_watcher.setFuture(m_model->recognize(request, m_connection.toConnectionConfig()));
}

OcrRequest RecognitionController::buildRequest(const QImage &image, const ResolvedConnection &conn) const
{
    OcrRequest request;
    request.image = image;
    request.prompt = promptText();
    request.modelId = conn.modelId;
    request.parameters = m_requestProfiles.activeProfile().parameters;
    return request;
}

void RecognitionController::onRecognitionFinished()
{
    if (m_recognizingIndex < 0)
        return;

    const OcrResult raw = m_watcher.future().resultCount() > 0 ? m_watcher.result() : OcrResult::makeError(StatusMessage::translate("RecognitionController", "No response"));
    const int index = m_recognizingIndex;

    if (!raw.success) {
        if (m_stopRequested)
            emit statusRequested(StatusMessage::translate("RecognitionController", "Stopped at page %1.").arg(index + 1));
        else {
            emit statusRequested(StatusMessage::translate("RecognitionController", "Error on page %1: %2").arg(index + 1).arg(raw.errorMessage.text()));
        }
        finishRun();
        return;
    }

    emit rawResultReady(index, raw);

    if (m_stopRequested) {
        emit statusRequested(StatusMessage::translate("RecognitionController", "Stopped after page %1.").arg(index + 1));
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

    emit statusRequested(m_skippedPages > 0 ? StatusMessage::translate("RecognitionController", "Recognition finished. Skipped %1 unreadable page(s).").arg(m_skippedPages)
                                            : StatusMessage::translate("RecognitionController", "Recognition finished."));
    finishRun();
}

void RecognitionController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_model)
        m_model->abort();
    m_runtime.cancelPendingStart();
    emit statusRequested(StatusMessage::translate("RecognitionController", "Stopping…"));
}

void RecognitionController::finishRun()
{
    m_recognizingIndex = -1;
    m_recognizeAll = false;
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