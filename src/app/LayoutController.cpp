#include "app/LayoutController.h"

#include <QTimer>

#include "config/RequestProfileStore.h"

namespace llocr {

namespace {

// TeleOCR's layout pass is calibrated on square 1036x1036 inputs (upstream
// resizes the page hard, without keeping the aspect). Normalized block
// coordinates are invariant to that squeeze, so matching the training input
// costs nothing and keeps the boxes accurate.
constexpr int kLayoutImageSize = 1036;

}  // namespace

LayoutController::LayoutController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent)
    : ModelRequestController(runtime, "LayoutController", parent), m_requestProfiles(requestProfiles), m_model(std::make_unique<GeneralPurposeModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        ++m_progressDone;
        emit progressChanged();
        if (stopRequested()) {
            emit statusRequested(StatusMessage::translate(trContext(), "Markup stopped."));
            finish();
            return;
        }
        const CheckResult result = watcherResult(m_watcher);
        if (result.status == CheckStatus::Failed) {
            emit statusRequested(result.errorMessage);
            emit layoutFinished(m_currentPage, QString());
        } else {
            // An empty reply means the model produced no usable markup; the
            // caller reports it as a failed page instead of wiping the blocks.
            emit layoutFinished(m_currentPage, result.status == CheckStatus::ReRecognized ? result.text : QString());
        }
        startNext();
    });
}

void LayoutController::layoutPages(const QList<int> &pages, const ImageProvider &imageProvider, const QString &systemPrompt, const QString &prompt)
{
    if (busy() || pages.isEmpty() || !imageProvider)
        return;

    m_imageProvider = imageProvider;
    m_systemPrompt = systemPrompt;
    m_prompt = prompt;
    m_queue = pages;
    m_progressDone = 0;
    m_progressTotal = pages.size();
    emit progressChanged();
    setBusy(true);
    startNext();
}

void LayoutController::stop()
{
    m_queue.clear();
    ModelRequestController::stop();
}

void LayoutController::startNext()
{
    if (m_queue.isEmpty()) {
        finish();
        return;
    }
    m_currentPage = m_queue.takeFirst();

    const QImage image = m_imageProvider(m_currentPage);
    if (image.isNull()) {
        ++m_progressDone;
        emit progressChanged();
        emit statusRequested(StatusMessage::translate(trContext(), "Page %1 could not be rendered for markup.").arg(m_currentPage + 1));
        emit layoutFinished(m_currentPage, QString());
        QTimer::singleShot(0, this, [this]() { startNext(); });
        return;
    }

    beginRequest(
        ConnectionRole::Layout,
        StatusMessage::translate(trContext(), "Stopped before markup started."),
        [this, image](const ResolvedConnection &conn) {
            CheckRequest request;
            request.image = image.scaled(kLayoutImageSize, kLayoutImageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            request.systemPrompt = m_systemPrompt;
            request.typePrompt = m_prompt;
            request.modelId = conn.modelId;
            request.parameters = m_requestProfiles.activeProfile().parameters;

            m_watcher.setFuture(m_model->check(request, conn.toConnectionConfig()));
        },
        [this](const StatusMessage &) {
            ++m_progressDone;
            emit progressChanged();
            emit layoutFinished(m_currentPage, QString());
            QTimer::singleShot(0, this, [this]() { startNext(); });
        });
}

void LayoutController::abortRequest()
{
    if (m_model)
        m_model->abort();
}

void LayoutController::resetRun()
{
    m_currentPage = -1;
    m_imageProvider = nullptr;
}

}  // namespace llocr
