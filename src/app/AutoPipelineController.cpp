#include "app/AutoPipelineController.h"

#include <QTimer>

#include <utility>

namespace llocr {

AutoPipelineController::AutoPipelineController(Deps deps, QObject *parent) : QObject(parent), m_deps(std::move(deps))
{
    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, [this]() {
        // The stage did not report its stop in time: the run ends anyway.
        if (m_running && m_stopping)
            finish(false);
    });
}

void AutoPipelineController::setStages(QList<AutoPipelineStage *> stages)
{
    if (m_running)
        return;
    for (const QPointer<AutoPipelineStage> &stage : m_stages) {
        if (stage)
            disconnect(stage.data(), nullptr, this, nullptr);
    }
    m_stages.clear();
    for (AutoPipelineStage *stage : stages) {
        if (!stage)
            continue;
        m_stages.append(stage);
        connect(stage, &AutoPipelineStage::finished, this, &AutoPipelineController::onStageFinished);
        connect(stage, &AutoPipelineStage::failed, this, &AutoPipelineController::onStageFailed);
    }
}

bool AutoPipelineController::canStart() const
{
    return !m_running && m_deps.idle() && !m_deps.scopePages().isEmpty();
}

QString AutoPipelineController::blockedReason() const
{
    if (m_running)
        return QString();
    if (!m_deps.idle())
        return StatusMessage::translate("AutoPipelineController", "Another job is running").text();
    if (m_deps.scopePages().isEmpty())
        return StatusMessage::translate("AutoPipelineController", "No pages to process").text();
    return QString();
}

QString AutoPipelineController::stageTitle() const
{
    if (!m_current)
        return QString();
    return m_current->title();
}

int AutoPipelineController::progressDone() const
{
    return m_current ? m_current->progressDone() : 0;
}

int AutoPipelineController::progressTotal() const
{
    return m_current ? m_current->progressTotal() : 0;
}

bool AutoPipelineController::start()
{
    if (m_running)
        return false;
    if (!m_deps.idle()) {
        emit statusRequested(StatusMessage::translate("AutoPipelineController", "Another job is running"));
        return false;
    }
    const QList<int> pages = m_deps.scopePages();
    if (pages.isEmpty()) {
        emit statusRequested(StatusMessage::translate("AutoPipelineController", "No pages to process"));
        return false;
    }
    m_scope = pages;
    m_skips.clear();
    m_failures.clear();
    m_stopping = false;
    m_running = true;
    emit stateChanged();
    runStage(0);
    return true;
}

void AutoPipelineController::stop()
{
    if (!m_running)
        return;
    m_stopping = true;
    if (!m_current) {
        finish(false);
        return;
    }
    // The stage aborts the work in flight and reports when it is done; the
    // grace period is the backstop for a stage that never reports.
    m_stopTimer.start(m_stopGraceMs);
    m_current->stop();
}

void AutoPipelineController::runStage(int index)
{
    if (!m_running)
        return;
    if (index >= m_stages.size()) {
        finish(true);
        return;
    }
    m_stageIndex = index;
    emit stateChanged();

    AutoPipelineStage *stage = m_stages.at(index).data();
    if (!stage) {
        // The stage was destroyed before the run reached it.
        QTimer::singleShot(0, this, [this, index]() { runStage(index + 1); });
        return;
    }

    const QString reason = stage->skipReason(m_scope);
    if (!reason.isEmpty()) {
        m_skips.append(QStringLiteral("%1 — %2").arg(stage->title(), reason));
        QTimer::singleShot(0, this, [this, index]() { runStage(index + 1); });
        return;
    }

    m_current = stage;
    emit stateChanged();
    stage->start(m_scope);
}

void AutoPipelineController::onStageFinished()
{
    if (!m_running || sender() != m_current.data())
        return;
    m_current = nullptr;
    if (m_stopping) {
        m_stopTimer.stop();
        finish(false);
        return;
    }
    runStage(m_stageIndex + 1);
}

void AutoPipelineController::onStageFailed(const StatusMessage &message)
{
    if (!m_running || sender() != m_current.data())
        return;
    m_current = nullptr;
    m_failures.append(message.text());
    emit statusRequested(message);
    if (m_stopping) {
        m_stopTimer.stop();
        finish(false);
        return;
    }
    runStage(m_stageIndex + 1);
}

void AutoPipelineController::finish(bool ok)
{
    m_stopTimer.stop();
    m_running = false;
    m_stopping = false;
    m_current = nullptr;
    m_stageIndex = -1;
    emit stateChanged();
    reportSummary();
    emit finished(ok);
}

void AutoPipelineController::reportSummary()
{
    QList<StatusMessage> parts;
    for (const QString &skip : m_skips)
        parts.append(StatusMessage::literal(skip));
    for (const QString &failure : m_failures)
        parts.append(StatusMessage::literal(failure));
    if (parts.isEmpty())
        return;
    emit statusRequested(StatusMessage::join(parts, QStringLiteral("; ")));
}

}  // namespace llocr
