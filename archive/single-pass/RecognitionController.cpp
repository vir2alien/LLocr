#include "app/RecognitionController.h"

#include <QFutureWatcher>

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/ConnectionConfig.h"
#include "core/ModelProfiles.h"
#include "models/OcrModel.h"

namespace llocr {

RecognitionController::RecognitionController(SettingsStore &settings,
                                             RuntimeController &runtime,
                                             RequestProfileStore &requestProfiles,
                                             RequestProfileStore &rawPageOcrProfiles,
                                             ImageProvider imageProvider,
                                             QObject *parent,
                                             SkipResolver skipPage)
    : ModelRequestController(runtime, "RecognitionController", parent), m_settings(settings), m_requestProfiles(requestProfiles), m_rawPageOcrProfiles(rawPageOcrProfiles),
      m_imageProvider(imageProvider), m_skipPage(std::move(skipPage))
{
    connect(&m_watcher, &QFutureWatcher<OcrResult>::finished, this, &RecognitionController::onRecognitionFinished);
}

void RecognitionController::startCurrent(int index, int totalPages, ConnectionRole task)
{
    if (busy())
        return;
    m_task = task;
    m_totalPages = totalPages;
    m_startIndex = index;
    m_recognizeAll = false;
    resolveModel();
    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::startAll(int totalPages, ConnectionRole task)
{
    if (busy())
        return;
    m_task = task;
    m_totalPages = totalPages;
    m_startIndex = 0;
    m_recognizeAll = true;
    resolveModel();

    m_startIndex = firstPageToRecognize(0);
    if (m_startIndex < 0) {
        emit statusRequested(StatusMessage::join({StatusMessage::translate(trContext(), "Nothing to recognize:"), skipReport()}, QStringLiteral(" ")));
        m_recognizeAll = false;
        return;
    }

    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::startPages(const QList<int> &pages, ConnectionRole task)
{
    if (busy() || pages.isEmpty())
        return;
    m_task = task;
    m_pageQueue = pages;
    m_queuePos = 0;
    m_recognizeAll = false;
    resolveModel();
    setBusy(true);
    ensureConnectionReady();
}

void RecognitionController::resolveModel()
{
    m_skippedUnreadable = 0;
    m_skippedRecognized = 0;
    const QString role = connectionRoleName(m_task);
    const QString recipeId = m_settings.requestProfileIdForRole(role);
    if (m_model && m_modelId == recipeId && m_modelRole == role)
        return;
    m_model = OcrModel::create(recipeId, role);
    m_modelId = recipeId;
    m_modelRole = role;
}

StatusMessage RecognitionController::skipReport() const
{
    QList<StatusMessage> parts;
    if (m_skippedRecognized > 0) {
        parts.append(StatusMessage::translate(trContext(), "%1 page(s) already recognized").arg(m_skippedRecognized));
    }
    if (m_skippedUnreadable > 0) {
        parts.append(StatusMessage::translate(trContext(), "%1 unreadable page(s)").arg(m_skippedUnreadable));
    }
    return StatusMessage::join(parts, QStringLiteral(", "));
}

int RecognitionController::firstPageToRecognize(int from)
{
    m_skippedUnreadable = 0;
    m_skippedRecognized = 0;
    for (int index = from; index < m_totalPages; ++index) {
        const PageSkip skip = m_skipPage ? m_skipPage(index, m_recognizeAll) : PageSkip::None;
        if (skip == PageSkip::None)
            return index;
        if (skip == PageSkip::Unreadable)
            ++m_skippedUnreadable;
        else
            ++m_skippedRecognized;
    }
    return -1;
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
    beginRequest(
        m_task,
        StatusMessage::translate(trContext(), "Stopped before recognition started."),
        [this](const ResolvedConnection &conn) {
            m_connection = conn;
            if (!m_pageQueue.isEmpty())
                recognizeNextScopePage();
            else
                recognizePage(m_startIndex);
        },
        [this](const StatusMessage &) { finish(); });
}

void RecognitionController::recognizePage(int index)
{
    while (index >= 0 && index < m_totalPages) {
        const PageSkip skip = m_skipPage ? m_skipPage(index, m_recognizeAll) : PageSkip::None;
        if (skip == PageSkip::None)
            break;
        if (skip == PageSkip::Unreadable)
            ++m_skippedUnreadable;
        else
            ++m_skippedRecognized;
        if (!m_recognizeAll) {
            emit statusRequested(StatusMessage::translate(trContext(),
                                                          "Page %1 is a blank replacement for an unreadable page; "
                                                          "recognition skipped.")
                                     .arg(index + 1));
            finish();
            return;
        }
        ++index;
    }
    if (index < 0 || index >= m_totalPages) {
        emit statusRequested(StatusMessage::join({StatusMessage::translate(trContext(), "Recognition finished."), skipReport()}, QStringLiteral(" ")));
        finish();
        return;
    }

    launchPageRequest(index);
}

// Pops the next queue entry the skip resolver allows, or finishes the run.
void RecognitionController::recognizeNextScopePage()
{
    while (m_queuePos < m_pageQueue.size()) {
        const int index = m_pageQueue.at(m_queuePos++);
        const PageSkip skip = m_skipPage ? m_skipPage(index, false) : PageSkip::None;
        if (skip == PageSkip::None) {
            launchPageRequest(index);
            return;
        }
        if (skip == PageSkip::Unreadable)
            ++m_skippedUnreadable;
        else
            ++m_skippedRecognized;
    }
    emit statusRequested(StatusMessage::join({StatusMessage::translate(trContext(), "Recognition finished."), skipReport()}, QStringLiteral(" ")));
    finish();
}

bool RecognitionController::launchPageRequest(int index)
{
    m_recognizingIndex = index;
    if (!m_pageQueue.isEmpty())
        emit statusRequested(StatusMessage::translate(trContext(), "Recognizing page %1 (%2 of %3)…").arg(index + 1).arg(m_queuePos).arg(m_pageQueue.size()));
    else
        emit statusRequested(StatusMessage::translate(trContext(), "Recognizing page %1 of %2…").arg(index + 1).arg(m_totalPages));

    QString imageError;
    const QImage image = m_imageProvider(index, imageError);
    if (image.isNull()) {
        emit statusRequested(StatusMessage::translate(trContext(), "Error on page %1: %2")
                                 .arg(index + 1)
                                 .arg(imageError.isEmpty() ? StatusMessage::translate(trContext(), "the page has no image to recognize").text() : imageError));
        finish();
        return false;
    }
    const OcrRequest request = buildRequest(image, m_connection);
    m_watcher.setFuture(m_model->recognize(request, m_connection.toConnectionConfig()));
    return true;
}

OcrRequest RecognitionController::buildRequest(const QImage &image, const ResolvedConnection &conn) const
{
    OcrRequest request;
    request.image = image;
    request.prompt = promptText();
    request.modelId = conn.modelId;
    request.parameters = (m_task == ConnectionRole::RawPageOcr ? m_rawPageOcrProfiles : m_requestProfiles).activeProfile().parameters;
    return request;
}

void RecognitionController::onRecognitionFinished()
{
    if (m_recognizingIndex < 0)
        return;

    const OcrResult raw = watcherResult(m_watcher);
    const int index = m_recognizingIndex;

    if (!raw.success) {
        if (stopRequested())
            emit statusRequested(StatusMessage::translate(trContext(), "Stopped at page %1.").arg(index + 1));
        else {
            emit statusRequested(StatusMessage::translate(trContext(), "Error on page %1: %2").arg(index + 1).arg(raw.errorMessage.text()));
        }
        finish();
        return;
    }

    emit rawResultReady(index, m_task, raw);

    if (stopRequested()) {
        emit statusRequested(StatusMessage::translate(trContext(), "Stopped after page %1.").arg(index + 1));
        finish();
        return;
    }

    if (!m_pageQueue.isEmpty()) {
        recognizeNextScopePage();
        return;
    }

    if (m_recognizeAll) {
        const int next = index + 1;
        if (next < m_totalPages) {
            recognizePage(next);
            return;
        }
    }

    emit statusRequested(StatusMessage::join({StatusMessage::translate(trContext(), "Recognition finished."), skipReport()}, QStringLiteral(" ")));
    finish();
}

void RecognitionController::abortRequest()
{
    if (m_model)
        m_model->abort();
    // A multi-page run outlives one request: a stop must also cancel a server
    // start still pending under the resolve phase.
    runtime().cancelPendingStart();
}

void RecognitionController::resetRun()
{
    m_recognizingIndex = -1;
    m_recognizeAll = false;
    m_pageQueue.clear();
    m_queuePos = 0;
}

}  // namespace llocr
