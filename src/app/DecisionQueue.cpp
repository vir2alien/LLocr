#include "app/DecisionQueue.h"

#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"

namespace llocr {

DecisionQueue::DecisionQueue(Deps deps, QObject *parent)
    : BlockQueue({deps.document, deps.gate, deps.cropProvider}, parent),
      m_deps(std::move(deps)),
      m_decision(m_deps.runtime, this)
{
    connect(&m_decision, &DecisionController::judgeFinished, this, [this](const DecisionResult &result) {
        CheckResult verdict;
        if (!result.ok) {
            // A failed decision request is not a verdict: the block stays
            // unchecked and the error is reported, instead of the block being
            // silently re-recognized on a broken stage.
            verdict = CheckResult::makeError(result.errorMessage);
            reportNewError(result.errorMessage);
        } else {
            verdict.status = result.routedStatus(matchThresholdFor(m_judgedType));
            verdict.confidence = result.probability;
        }
        deliverVerdict(verdict);
    });
    connect(&m_decision, &DecisionController::busyChanged, this, &DecisionQueue::stateChanged);
    connect(&m_decision, &DecisionController::statusRequested, this, &DecisionQueue::statusRequested);
}

bool DecisionQueue::requestBusy() const
{
    return m_decision.busy();
}

void DecisionQueue::beginRun()
{
    m_matchThreshold = m_deps.settings.decisionMatchThreshold();
    m_decisionRole = ModelProfiles::roleFor(m_deps.settings.decisionRequestProfileId(), QStringLiteral("decision"));
}

void DecisionQueue::startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text)
{
    // The candidate is the block's own text: a formula was brought to its
    // canonical LaTeX form when it was recognized (ADR 194), so the wire
    // carries the same bytes the panel shows and the export writes.
    emit verificationStarted(taskPage(), taskBox());
    m_judgedType = box.label;
    // The visual-diff decision (docs/17) reads the candidate twice: as the
    // state text and as the picture it paints. Without a renderer the run
    // would be blind on the second leg, so the block goes to human eyes
    // instead of being judged on half the input.
    if (!m_deps.renderCandidate) {
        deliverVerdict(CheckResult{
            CheckStatus::Review, {}, StatusMessage::translate("DecisionQueue", "No candidate renderer: the visual-diff decision model needs the candidate as an image."), {}, -1.0});
        return;
    }
    // The state carries the block's own text (the upstream contract: the OCR
    // text goes to the model as it is), but the picture is painted from the
    // text the page shows — a formula as display math, a table as a grid.
    // The raw block text would reach the renderer as literal LaTeX, which no
    // model can read as a formula (ADR 237).
    const QString displayText = m_deps.displayTextProvider ? m_deps.displayTextProvider(taskPage(), taskBox()) : QString();
    const QString candidate = displayText.isEmpty() ? text : displayText;
    m_deps.renderCandidate(candidate, [this, crop, text](bool ok, const QImage &rendered, const QString &error) {
        // The queue may have been stopped while the render was in flight; a
        // late image belongs to no task anymore.
        if (!active())
            return;
        if (!ok || rendered.isNull()) {
            deliverVerdict(CheckResult{CheckStatus::Review, {}, StatusMessage::translate("DecisionQueue", "The candidate render failed: %1").arg(error), {}, -1.0});
            return;
        }
        m_decision.judgeVisual(crop, rendered, text);
    });
}

void DecisionQueue::stopRequest()
{
    m_decision.stop();
}

// The shared tail of every decision result, requested or produced locally (a
// failed render lands on Review without a model request): the verdict reaches
// the document, the progress moves, the next task starts.
void DecisionQueue::deliverVerdict(const CheckResult &verdict)
{
    emit blockVerified(taskPage(), taskBox(), verdict);
    advance();
}

double DecisionQueue::matchThresholdFor(const QString &blockType) const
{
    if (!m_decisionRole)
        return m_matchThreshold;
    const QString aliasKey = m_decisionRole->blockAliases.value(blockType, blockType);
    const double byBlock = m_decisionRole->blockMatchThresholds.value(aliasKey, -1.0);
    if (byBlock >= 0)
        return byBlock;
    if (m_decisionRole->matchThreshold >= 0)
        return m_decisionRole->matchThreshold;
    return m_matchThreshold;
}

}  // namespace llocr
