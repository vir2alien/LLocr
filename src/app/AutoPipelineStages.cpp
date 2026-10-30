#include "app/AutoPipelineStages.h"

#include "app/LayoutController.h"

#include <QPointer>

#include <utility>

namespace llocr {

namespace {

StatusMessage stageMessage(const QString &key)
{
    return StatusMessage::translate("AutoPipelineStage", key);
}

QString stageText(const QString &key)
{
    return stageMessage(key).text();
}

// The id names the stage in the run. The check of the corrected blocks is a
// stage of its own, not a second run of the first check, so it carries its
// own id.
QString verificationStageId(const QString &titleKey)
{
    return titleKey == QLatin1String("Verification of the corrected blocks") ? QStringLiteral("recheck") : QStringLiteral("verification");
}

}  // namespace

AutoMarkupStage::AutoMarkupStage(LayoutController &layout, RunPass runPass, QObject *parent)
    : AutoPipelineStage(QStringLiteral("markup"), QStringLiteral("Markup"), parent), m_layout(layout), m_runPass(std::move(runPass))
{
}

QString AutoMarkupStage::skipReason(const AutoPipelineStage::Scope &) const
{
    if (!m_layout.available())
        return stageText(QStringLiteral("No model for this task"));
    return QString();
}

void AutoMarkupStage::start(const AutoPipelineStage::Scope &pages)
{
    m_running = true;
    // The stage can be destroyed while the pass is in flight, so the callback
    // holds a guarded pointer and not the raw one.
    const QPointer<AutoMarkupStage> self(this);
    m_runPass(pages, [self](bool ok) {
        if (!self || !self->m_running)
            return;
        self->m_running = false;
        if (ok) {
            emit self->finished();
            return;
        }
        emit self->failed(stageMessage(QStringLiteral("The markup pass failed")));
    });
}

void AutoMarkupStage::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_layout.stop();
    emit finished();
}

int AutoMarkupStage::progressDone() const
{
    return m_layout.progressDone();
}

int AutoMarkupStage::progressTotal() const
{
    return m_layout.progressTotal();
}

AutoRecognitionStage::AutoRecognitionStage(VerificationQueueController &queue, ModelAvailable modelAvailable, QObject *parent)
    : AutoPipelineStage(QStringLiteral("recognition"), QStringLiteral("Block recognition"), parent), m_queue(queue), m_modelAvailable(std::move(modelAvailable))
{
    connect(&m_queue, &VerificationQueueController::queueFinished, this, [this]() {
        if (!m_running)
            return;
        m_running = false;
        emit finished();
    });
}

QString AutoRecognitionStage::skipReason(const AutoPipelineStage::Scope &pages) const
{
    if (m_modelAvailable && !m_modelAvailable())
        return stageText(QStringLiteral("No model for this task"));
    if (m_queue.unrecognizedBlocks(pages).isEmpty())
        return stageText(QStringLiteral("Nothing to recognize"));
    return QString();
}

void AutoRecognitionStage::start(const AutoPipelineStage::Scope &pages)
{
    m_running = true;
    m_queue.recognizeBlocks(m_queue.unrecognizedBlocks(pages));
    if (!m_queue.queueActive()) {
        m_running = false;
        emit finished();
    }
}

void AutoRecognitionStage::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_queue.stop();
    emit finished();
}

int AutoRecognitionStage::progressDone() const
{
    return m_queue.progressDone();
}

int AutoRecognitionStage::progressTotal() const
{
    return m_queue.progressTotal();
}

AutoVerificationStage::AutoVerificationStage(VerificationQueueController &queue, ModelAvailable modelAvailable, QString titleKey, QObject *parent)
    : AutoPipelineStage(verificationStageId(titleKey), titleKey, parent), m_queue(queue), m_modelAvailable(std::move(modelAvailable))
{
    connect(&m_queue, &VerificationQueueController::queueFinished, this, [this]() {
        if (!m_running)
            return;
        m_running = false;
        emit finished();
    });
}

QString AutoVerificationStage::skipReason(const AutoPipelineStage::Scope &pages) const
{
    if (m_modelAvailable && !m_modelAvailable())
        return stageText(QStringLiteral("No model for this task"));
    if (m_queue.enabledBlocks(pages).isEmpty())
        return stageText(QStringLiteral("Nothing to check"));
    return QString();
}

void AutoVerificationStage::start(const AutoPipelineStage::Scope &pages)
{
    m_running = true;
    m_queue.checkBlocks(m_queue.enabledBlocks(pages));
    if (!m_queue.queueActive()) {
        m_running = false;
        emit finished();
    }
}

void AutoVerificationStage::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_queue.stop();
    emit finished();
}

int AutoVerificationStage::progressDone() const
{
    return m_queue.progressDone();
}

int AutoVerificationStage::progressTotal() const
{
    return m_queue.progressTotal();
}

AutoFixStage::AutoFixStage(VerificationQueueController &queue, AutoVerificationStage::ModelAvailable modelAvailable, QObject *parent)
    : AutoPipelineStage(QStringLiteral("fix"), QStringLiteral("Fix"), parent), m_queue(queue), m_modelAvailable(std::move(modelAvailable))
{
    connect(&m_queue, &VerificationQueueController::queueFinished, this, [this]() {
        if (!m_running)
            return;
        m_running = false;
        emit finished();
    });
}

QString AutoFixStage::skipReason(const AutoPipelineStage::Scope &pages) const
{
    if (m_modelAvailable && !m_modelAvailable())
        return stageText(QStringLiteral("No model for this task"));
    if (m_queue.fixableBlocks(pages).isEmpty())
        return stageText(QStringLiteral("Nothing to fix"));
    return QString();
}

void AutoFixStage::start(const AutoPipelineStage::Scope &pages)
{
    m_running = true;
    m_queue.fixBlocks(m_queue.fixableBlocks(pages));
    if (!m_queue.queueActive()) {
        m_running = false;
        emit finished();
    }
}

void AutoFixStage::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_queue.stop();
    emit finished();
}

int AutoFixStage::progressDone() const
{
    return m_queue.progressDone();
}

int AutoFixStage::progressTotal() const
{
    return m_queue.progressTotal();
}

}  // namespace llocr
