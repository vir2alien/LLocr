#include "app/VerificationQueueController.h"

#include <QTimer>

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/BoxTransitions.h"
#include "core/ModelProfiles.h"
#include "parsers/BlockStyle.h"

namespace llocr {

namespace {
// The language the prompts name ({{LANGUAGE}}). The user's choice and
// the auto-detection land in the next step.
const QString kDocumentLanguage = QStringLiteral("Russian");
}  // namespace

VerificationQueueController::VerificationQueueController(Deps deps, QObject *parent)
    : QObject(parent), m_deps(std::move(deps)), m_decision(m_deps.runtime, this), m_check(m_deps.checkRequestProfiles, m_deps.runtime, nullptr),
      m_fix(m_deps.fixRequestProfiles, m_deps.runtime, nullptr)
{
    connect(&m_decision, &DecisionController::judgeFinished, this, [this](const DecisionResult &result) {
        CheckResult verdict;
        if (!result.ok) {
            // A failed decision request is not a verdict: the block stays
            // unchecked and the error is reported, instead of the block being
            // silently re-recognized on a broken stage.
            verdict = CheckResult::makeError(result.errorMessage);
            if (!result.errorMessage.isEmpty() && result.errorMessage != m_checkError) {
                m_checkError = result.errorMessage;
                emit problemReported(result.errorMessage);
            }
        } else {
            verdict.status = result.routedStatus(matchThresholdFor(m_judgedType));
            verdict.confidence = result.probability;
        }
        deliverVerdict(verdict);
    });
    connect(&m_decision, &DecisionController::busyChanged, this, &VerificationQueueController::stateChanged);
    connect(&m_decision, &DecisionController::statusRequested, this, &VerificationQueueController::statusRequested);

    connect(&m_check, &CheckController::checkFinished, this, [this](const CheckResult &result) {
        if (result.status == CheckStatus::Failed && !result.errorMessage.isEmpty() && result.errorMessage != m_checkError) {
            m_checkError = result.errorMessage;
            emit problemReported(result.errorMessage);
        }
        const int page = m_verifyPage;
        const int box = m_verifyBoxIndex;
        emit blockRecognized(page, box, result);
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
    });
    connect(&m_check, &CheckController::busyChanged, this, &VerificationQueueController::stateChanged);
    connect(&m_check, &CheckController::statusRequested, this, &VerificationQueueController::statusRequested);

    connect(&m_fix, &FixController::fixFinished, this, [this](const CheckResult &result) {
        if (result.status == CheckStatus::Failed && !result.errorMessage.isEmpty() && result.errorMessage != m_checkError) {
            m_checkError = result.errorMessage;
            emit problemReported(result.errorMessage);
        }
        const int page = m_verifyPage;
        const int box = m_verifyBoxIndex;
        emit blockFixed(page, box, result);
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
    });
    connect(&m_fix, &FixController::busyChanged, this, &VerificationQueueController::stateChanged);
    connect(&m_fix, &FixController::statusRequested, this, &VerificationQueueController::statusRequested);
}

void VerificationQueueController::checkBlock(int pageIndex, int boxIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized) {
        return;
    }
    startVerifyQueue({{pageIndex, boxIndex}});
}

void VerificationQueueController::checkPageEnabledBlocks(int pageIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;

    int answered = 0;
    int rejected = 0;
    QList<int> candidates;
    collectEnabledBoxes(pageIndex, candidates, answered, rejected);

    QList<Task> tasks;
    for (const int box : std::as_const(candidates))
        tasks.append({pageIndex, box});
    startOrReportAnswered(tasks, answered, rejected);
}

void VerificationQueueController::checkAllEnabledBlocks()
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;

    QList<Task> tasks;
    int answered = 0;
    int rejected = 0;
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        QList<int> candidates;
        int pageAnswered = 0;
        int pageRejected = 0;
        collectEnabledBoxes(p, candidates, pageAnswered, pageRejected);
        answered += pageAnswered;
        rejected += pageRejected;
        for (const int box : std::as_const(candidates))
            tasks.append({p, box});
    }
    startOrReportAnswered(tasks, answered, rejected);
}

void VerificationQueueController::checkPagesEnabledBlocks(const QList<int> &pages)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;

    QList<Task> tasks;
    int answered = 0;
    int rejected = 0;
    for (const int p : pages) {
        QList<int> candidates;
        int pageAnswered = 0;
        int pageRejected = 0;
        collectEnabledBoxes(p, candidates, pageAnswered, pageRejected);
        answered += pageAnswered;
        rejected += pageRejected;
        for (const int box : std::as_const(candidates))
            tasks.append({p, box});
    }
    startOrReportAnswered(tasks, answered, rejected);
}

QList<VerificationQueueController::Task> VerificationQueueController::unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        collectUnrecognizedBoxes(page, tasks, filter);
    }
    return tasks;
}

QList<VerificationQueueController::Task> VerificationQueueController::enabledBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        QList<int> candidates;
        int answered = 0;
        int rejected = 0;
        collectEnabledBoxes(page, candidates, answered, rejected, filter);
        for (const int box : std::as_const(candidates))
            tasks.append({page, box});
    }
    return tasks;
}

QList<VerificationQueueController::Task> VerificationQueueController::fixableBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        collectFixBoxes(page, tasks, filter);
    }
    return tasks;
}

void VerificationQueueController::collectEnabledBoxes(int pageIndex, QList<int> &out, int &answered, int &rejected, const TypeFilter &filter) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (filter && !filter(box.label))
            continue;
        if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
            continue;
        // A formula the app rejected on its LaTeX syntax is not asked about
        // (ADR 194): the answer would be about a text that cannot be
        // rendered. It waits for a new recognition instead.
        if (BoxTransitions::effectiveCheckStatus(box) == BoxCheckStatus::InvalidSyntax) {
            ++rejected;
            continue;
        }
        // A block with a verdict on its effective layer has been answered;
        // a re-recognized block (fresh text, verdicts erased) is asked again.
        if (BoxTransitions::effectiveCheckStatus(box) != BoxCheckStatus::NotChecked) {
            ++answered;
            continue;
        }
        if (BoxTransitions::effectiveText(box).isEmpty())
            continue;
        out.append(i);
    }
}

void VerificationQueueController::collectProblemBoxes(int pageIndex, QList<Task> &out) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoxCheckStatus status = BoxTransitions::effectiveCheckStatus(page.boxes.at(i));
        // A formula the app rejected on its LaTeX syntax (ADR 194) needs a new
        // recognition more than a re-check does: the text itself is broken.
        if (status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review || status == BoxCheckStatus::InvalidSyntax)
            out.append({pageIndex, i});
    }
}

// The blocks the fixer still has to answer (ADR 188): the decision model left
// them open and the fixer has not looked at this candidate yet. The same set
// the toolbar badge counts, so the number is the work.
void VerificationQueueController::collectFixBoxes(int pageIndex, QList<Task> &out, const TypeFilter &filter) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    for (int i = 0; i < page.boxes.size(); ++i) {
        if (filter && !filter(page.boxes.at(i).label))
            continue;
        if (BoxTransitions::needsFix(page.boxes.at(i)))
            out.append({pageIndex, i});
    }
}

// The layout pass creates text blocks with no text; a legacy project may
// still carry a correction-only block. Image placeholders are never
// transcribed.
void VerificationQueueController::collectUnrecognizedBoxes(int pageIndex, QList<Task> &out, const TypeFilter &filter) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (filter && !filter(box.label))
            continue;
        if (box.checkStatus != BoxCheckStatus::NotChecked || !box.text.isEmpty() || !box.correctedText.isEmpty())
            continue;
        if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
            continue;
        out.append({pageIndex, i});
    }
}

void VerificationQueueController::startOrReportAnswered(const QList<Task> &tasks, int answered, int rejected)
{
    if (!tasks.isEmpty()) {
        startVerifyQueue(tasks);
        return;
    }
    if (answered == 0 && rejected == 0)
        return;
    // A formula the app rejected on its LaTeX syntax is named apart from the
    // blocks that were already verified: it was not asked about (ADR 194).
    QList<StatusMessage> parts;
    parts.append(StatusMessage::translate("VerificationQueueController", "Nothing to check:"));
    if (answered > 0)
        parts.append(StatusMessage::translate("VerificationQueueController", "%1 block(s) already verified").arg(answered));
    if (rejected > 0)
        parts.append(StatusMessage::translate("VerificationQueueController", "%1 formula(s) rejected: invalid LaTeX syntax").arg(rejected));
    emit statusRequested(StatusMessage::join(parts, QStringLiteral(" ")));
}

void VerificationQueueController::stop()
{
    m_verifyQueue.clear();
    if (m_verifyQueueActive) {
        m_verifyQueueActive = false;
        m_verifyPage = -1;
        m_verifyBoxIndex = -1;
        emit stateChanged();
        // The waiter has to hear it: a stage that waits for the queue would
        // wait for a work list that is gone (ADR 257).
        emit queueFinished();
    }
    if (m_decision.busy())
        m_decision.stop();
    if (m_check.busy())
        m_check.stop();
    if (m_fix.busy())
        m_fix.stop();
}

bool VerificationQueueController::pageVerificationSupported(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized || m_deps.document.page(pageIndex).result.pages.isEmpty())
        return false;
    if (m_deps.layoutBusy())
        return false;
    const OcrPage &page = m_deps.document.page(pageIndex).result.pages.first();
    for (const BoundingBox &box : std::as_const(page.boxes)) {
        if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !BoxTransitions::effectiveText(box).isEmpty())
            return true;
    }
    return false;
}

bool VerificationQueueController::allPageVerificationSupported() const
{
    if (m_deps.layoutBusy())
        return false;
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        const DocumentPage &docPage = m_deps.document.page(p);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &page = docPage.result.pages.first();
        for (const BoundingBox &box : std::as_const(page.boxes)) {
            if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !BoxTransitions::effectiveText(box).isEmpty())
                return true;
        }
    }
    return false;
}

bool VerificationQueueController::pageRecheckSupported(int pageIndex) const
{
    QList<Task> tasks;
    collectProblemBoxes(pageIndex, tasks);
    return !tasks.isEmpty();
}

bool VerificationQueueController::pageBlocksRecognitionSupported(int pageIndex) const
{
    if (m_deps.layoutBusy())
        return false;
    QList<Task> tasks;
    collectUnrecognizedBoxes(pageIndex, tasks);
    return !tasks.isEmpty();
}

bool VerificationQueueController::allPageRecheckSupported() const
{
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        QList<Task> tasks;
        collectProblemBoxes(p, tasks);
        if (!tasks.isEmpty())
            return true;
    }
    return false;
}

void VerificationQueueController::startVerifyQueue(const QList<Task> &tasks)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    if (tasks.isEmpty())
        return;

    m_checkError = StatusMessage();
    m_checkFinished = false;
    m_phase = Phase::Decision;
    m_matchThreshold = m_deps.settings.decisionMatchThreshold();
    m_decisionRole = ModelProfiles::roleFor(m_deps.settings.decisionRequestProfileId(), QStringLiteral("decision"));
    m_verifyQueue = tasks;
    m_verifyPage = -1;
    m_verifyBoxIndex = -1;
    m_verifyTotal = tasks.size();
    m_verifyDone = 0;
    m_verifyQueueActive = true;
    emit stateChanged();

    QTimer::singleShot(0, this, [this]() { startNextVerify(); });
}

void VerificationQueueController::recheckBlock(int pageIndex, int boxIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized) {
        return;
    }
    startRecheckQueue({{pageIndex, boxIndex}});
}

void VerificationQueueController::recheckPageProblemBlocks(int pageIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    QList<Task> tasks;
    collectProblemBoxes(pageIndex, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::recheckPageBlocks(int pageIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    QList<Task> tasks;
    collectUnrecognizedBoxes(pageIndex, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::recheckPagesBlocks(const QList<int> &pages)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    QList<Task> tasks;
    for (const int p : pages)
        collectUnrecognizedBoxes(p, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::recheckAllProblemBlocks()
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    QList<Task> tasks;
    for (int p = 0; p < m_deps.document.pageCount(); ++p)
        collectProblemBoxes(p, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::fixBlock(int pageIndex, int boxIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized) {
        return;
    }
    startFixQueue({{pageIndex, boxIndex}});
}

void VerificationQueueController::fixAllProblemBlocks()
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    QList<Task> tasks;
    for (int p = 0; p < m_deps.document.pageCount(); ++p)
        collectFixBoxes(p, tasks);
    startFixQueue(tasks);
}

void VerificationQueueController::fixPagesProblemBlocks(const QList<int> &pages)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    QList<Task> tasks;
    for (const int p : pages)
        collectFixBoxes(p, tasks);
    startFixQueue(tasks);
}

void VerificationQueueController::recognizeBlocks(const QList<Task> &tasks)
{
    startRecheckQueue(tasks);
}

void VerificationQueueController::checkBlocks(const QList<Task> &tasks)
{
    startVerifyQueue(tasks);
}

void VerificationQueueController::fixBlocks(const QList<Task> &tasks)
{
    startFixQueue(tasks);
}

void VerificationQueueController::startRecheckQueue(const QList<Task> &tasks)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    if (tasks.isEmpty())
        return;

    m_checkError = StatusMessage();
    m_checkFinished = false;
    m_phase = Phase::Recheck;
    m_verifyQueue = tasks;
    m_verifyPage = -1;
    m_verifyBoxIndex = -1;
    m_verifyTotal = tasks.size();
    m_verifyDone = 0;
    m_verifyQueueActive = true;
    emit stateChanged();

    QTimer::singleShot(0, this, [this]() { startNextVerify(); });
}

void VerificationQueueController::startFixQueue(const QList<Task> &tasks)
{
    if (m_deps.layoutBusy() || checkBusy() || m_verifyQueueActive)
        return;
    if (tasks.isEmpty())
        return;

    m_checkError = StatusMessage();
    m_checkFinished = false;
    m_phase = Phase::Fix;
    m_verifyQueue = tasks;
    m_verifyPage = -1;
    m_verifyBoxIndex = -1;
    m_verifyTotal = tasks.size();
    m_verifyDone = 0;
    m_verifyQueueActive = true;
    emit stateChanged();

    QTimer::singleShot(0, this, [this]() { startNextVerify(); });
}

void VerificationQueueController::startNextVerify()
{
    if (!m_verifyQueueActive)
        return;

    if (m_verifyQueue.isEmpty()) {
        finishVerifyQueue();
        return;
    }

    const Task task = m_verifyQueue.takeFirst();
    m_verifyPage = task.page;
    m_verifyBoxIndex = task.box;
    if (!m_deps.document.isValidIndex(m_verifyPage)) {
        skipCurrentTask();
        return;
    }
    const DocumentPage &docPage = m_deps.document.page(m_verifyPage);
    if (!docPage.recognized || docPage.result.pages.isEmpty()) {
        skipCurrentTask();
        return;
    }
    const QList<BoundingBox> &boxes = docPage.result.pages.first().boxes;
    if (m_verifyBoxIndex < 0 || m_verifyBoxIndex >= boxes.size()) {
        skipCurrentTask();
        return;
    }
    const BoundingBox &box = boxes.at(m_verifyBoxIndex);
    const QString text = BoxTransitions::effectiveText(box);
    const QImage crop = m_deps.cropProvider(m_verifyPage, m_verifyBoxIndex);
    if (crop.isNull()) {
        skipCurrentTask();
        return;
    }

    if (m_phase == Phase::Decision) {
        // The old verdict of the layer being judged is erased while the
        // request is in flight: a failed run leaves the block gray instead of
        // showing a stale verdict (ADR 164).
        emit verificationStarted(m_verifyPage, m_verifyBoxIndex);
        // The candidate is the block's own text: a formula was brought to its
        // canonical LaTeX form when it was recognized (ADR 194), so the wire
        // carries the same bytes the panel shows and the export writes.
        m_judgedType = box.label;
        // The visual-diff decision (docs/31) reads the candidate twice:
        // as the state text and as the picture it paints. Without a
        // renderer the run would be blind on the second leg, so the block
        // goes to human eyes instead of being judged on half the input.
        if (!m_deps.renderCandidate) {
            deliverVerdict(CheckResult{
                CheckStatus::Review, {}, StatusMessage::translate("VerificationQueueController", "No candidate renderer: the visual-diff decision model needs the candidate as an image."), {}, -1.0});
            return;
        }
        // The state carries the block's own text (the upstream contract:
        // the OCR text goes to the model as it is), but the picture is
        // painted from the text the page shows — a formula as display
        // math, a table as a grid. The raw block text would reach the
        // renderer as literal LaTeX, which no model can read as a
        // formula (ADR 237).
        const QString displayText = m_deps.displayTextProvider ? m_deps.displayTextProvider(m_verifyPage, m_verifyBoxIndex) : QString();
        const QString candidate = displayText.isEmpty() ? text : displayText;
        m_deps.renderCandidate(candidate, [this, crop, text](bool ok, const QImage &rendered, const QString &error) {
            // The queue may have been stopped while the render was in
            // flight; a late image belongs to no task anymore.
            if (!m_verifyQueueActive)
                return;
            if (!ok || rendered.isNull()) {
                deliverVerdict(CheckResult{CheckStatus::Review, {}, StatusMessage::translate("VerificationQueueController", "The candidate render failed: %1").arg(error), {}, -1.0});
                return;
            }
            m_decision.judgeVisual(crop, rendered, text);
        });
        return;
    }

    if (m_phase == Phase::Fix) {
        // The fix protocol judges the block's current text; without one there
        // is no candidate to confirm or correct — the task is skipped.
        if (text.isEmpty()) {
            skipCurrentTask();
            return;
        }
        const QString modelId = m_deps.fixRequestProfiles.activeProfileId();
        const QList<ModelProfiles::Profile> &profiles = ModelProfiles::instance();
        const QString userPrompt = ModelProfiles::blockPromptFor(profiles, modelId, QStringLiteral("fix"), box.label);
        QString systemPrompt = ModelProfiles::systemPromptFor(profiles, modelId, QStringLiteral("fix"));
        systemPrompt.replace(QStringLiteral("{{LANGUAGE}}"), kDocumentLanguage);
        const int maxOutput = ModelProfiles::maxOutputForBlock(profiles, modelId, QStringLiteral("fix"), box.label);
        // The fix protocol judges the block's current text; the candidate is
        // the block's own text — a formula was brought to its canonical LaTeX
        // form when it was recognized (ADR 194), so the fixer sees the same
        // bytes the checker saw.
        m_fix.fixBlock(crop, text, systemPrompt, userPrompt, box.label, maxOutput);
        return;
    }

    // The transcription wording lives entirely in the model's profile
    // (systemPrompt + blockPrompts on the blockRecognition role). Per
    // ADR 163 the block's previous text is not sent — presenting a candidate
    // and getting confirm/fix/refuse back is the fix task's job.
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    const QString userPrompt = ModelProfiles::blockPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"), box.label);
    const QString systemPrompt = ModelProfiles::systemPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"));
    m_check.checkBlock(crop, systemPrompt, userPrompt);
}

double VerificationQueueController::matchThresholdFor(const QString &blockType) const
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

// The shared tail of every decision result, requested or produced locally (a
// failed render lands on Review without a model request): the verdict reaches
// the document, the progress moves, the next task starts.
void VerificationQueueController::deliverVerdict(const CheckResult &verdict)
{
    const int page = m_verifyPage;
    const int box = m_verifyBoxIndex;
    emit blockVerified(page, box, verdict);
    ++m_verifyDone;
    emit stateChanged();
    QTimer::singleShot(0, this, [this]() { startNextVerify(); });
}

void VerificationQueueController::skipCurrentTask()
{
    ++m_verifyDone;
    emit stateChanged();
    QTimer::singleShot(0, this, [this]() { startNextVerify(); });
}

void VerificationQueueController::finishVerifyQueue()
{
    if (!m_verifyQueueActive)
        return;
    m_verifyQueueActive = false;
    m_verifyQueue.clear();
    m_verifyPage = -1;
    m_verifyBoxIndex = -1;
    m_checkFinished = true;
    emit stateChanged();
    emit queueFinished();
}

}  // namespace llocr
