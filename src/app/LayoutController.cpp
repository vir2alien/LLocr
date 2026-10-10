#include "app/LayoutController.h"

#include <QFutureWatcher>
#include <QTimer>

#include "config/RequestProfileStore.h"
#include "models/GeneralPurposeModel.h"

namespace llocr {

namespace {

// TeleOCR's layout pass is calibrated on square 1036x1036 inputs (upstream
// resizes the page hard, without keeping the aspect). Normalized block
// coordinates are invariant to that squeeze, so matching the training input
// costs nothing and keeps the boxes accurate.
constexpr int kLayoutImageSize = 1036;

}  // namespace

LayoutController::LayoutController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : QObject(parent), m_requestProfiles(requestProfiles), m_runtime(runtime), m_model(std::make_unique<GeneralPurposeModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("LayoutController", "Markup stopped."));
            setBusy(false);
            return;
        }
        const CheckResult result = m_watcher.future().resultCount() > 0 ? m_watcher.result() : CheckResult::makeError(StatusMessage::translate("LayoutController", "No response"));
        if (result.status == CheckStatus::Failed) {
            emit statusRequested(result.errorMessage);
            emit layoutFinished(m_currentPage, QString());
        } else {
            // An empty reply means the model produced no usable markup; the
            // caller reports it as a failed page instead of wiping the blocks.
            emit layoutFinished(m_currentPage, result.status == CheckStatus::Fixed ? result.text : QString());
        }
        startNext();
    });
}

void LayoutController::layoutPages(const QList<int> &pages, const ImageProvider &imageProvider, const QString &systemPrompt, const QString &prompt)
{
    if (m_busy || pages.isEmpty() || !imageProvider)
        return;

    m_imageProvider = imageProvider;
    m_systemPrompt = systemPrompt;
    m_prompt = prompt;
    m_queue = pages;
    m_stopRequested = false;
    setBusy(true);
    startNext();
}

void LayoutController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    m_queue.clear();
    if (m_model)
        m_model->abort();
    emit statusRequested(StatusMessage::translate("LayoutController", "Stopping…"));
}

void LayoutController::startNext()
{
    if (m_queue.isEmpty()) {
        setBusy(false);
        return;
    }
    m_currentPage = m_queue.takeFirst();

    const QImage image = m_imageProvider(m_currentPage);
    if (image.isNull()) {
        emit statusRequested(StatusMessage::translate("LayoutController", "Page %1 could not be rendered for markup.").arg(m_currentPage + 1));
        emit layoutFinished(m_currentPage, QString());
        QTimer::singleShot(0, this, [this]() { startNext(); });
        return;
    }

    m_runtime.ensureConnectionReady(this, ConnectionRole::Layout, [this, image](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (m_stopRequested) {
            emit statusRequested(StatusMessage::translate("LayoutController", "Stopped before markup started."));
            setBusy(false);
            return;
        }
        if (conn.baseUrl.isEmpty()) {
            const StatusMessage message = conn.error.isEmpty() ? StatusMessage::translate("LayoutController", "Connection is not configured.") : StatusMessage::literal(conn.error);
            emit statusRequested(message);
            emit layoutFinished(m_currentPage, QString());
            QTimer::singleShot(0, this, [this]() { startNext(); });
            return;
        }

        CheckRequest request;
        request.image = image.scaled(kLayoutImageSize, kLayoutImageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        request.systemPrompt = m_systemPrompt;
        request.typePrompt = m_prompt;
        request.modelId = conn.modelId;
        request.parameters = m_requestProfiles.activeProfile().parameters;

        m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
    });
}

void LayoutController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (!busy) {
        m_currentPage = -1;
        m_imageProvider = nullptr;
    }
    emit busyChanged();
}

}  // namespace llocr
