#include "app/VerificationQueueController.h"

#include <QStringList>
#include <QTimer>

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "parsers/BlockStyle.h"

namespace llocr {

namespace {
// The generic yes/no question when the decision profile ships none (the
// shipped clef-flash profile carries its own wording).
constexpr const char *kFallbackQuestion = "The state is a text fragment recognized from the attached image by an OCR "
                                          "model. Does the image show the same text?";

// The text the decision stage judges: a re-recognized block answers for its
// corrected variant, everything else for the recognized text.
QString effectiveBoxText(const BoundingBox &box)
{
    return box.correctedText.isEmpty() ? box.text : box.correctedText;
}
}  // namespace

QString VerificationQueueController::composeBlockUserPrompt(const QString &typePrompt, const QString &recheckPrompt, const QString &existingText)
{
    if (existingText.isEmpty())
        return typePrompt;
    QString recheck = recheckPrompt;
    recheck.replace(QStringLiteral("{text}"), existingText);
    return typePrompt.isEmpty() ? recheck : typePrompt + QStringLiteral("\n\n") + recheck;
}

VerificationQueueController::VerificationQueueController(Deps deps, QObject *parent)
    : QObject(parent), m_deps(std::move(deps)), m_decision(m_deps.runtime, this), m_check(m_deps.checkRequestProfiles, m_deps.runtime, nullptr)
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
            verdict.status = result.probability >= m_matchThreshold ? CheckStatus::Ok : CheckStatus::Mismatch;
            verdict.confidence = result.probability;
        }
        const int page = m_verifyPage;
        const int box = m_verifyBoxIndex;
        emit blockChecked(page, box, verdict);
        ++m_verifyDone;
        if (verdict.status == CheckStatus::Mismatch && (m_runRecheck == RecheckForce || (m_runRecheck == RecheckFromSetting && m_autoRecheck)))
            m_recheckQueue.append({page, box});
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
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
        emit blockChecked(page, box, result);
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
    });
    connect(&m_check, &CheckController::busyChanged, this, &VerificationQueueController::stateChanged);
    connect(&m_check, &CheckController::statusRequested, this, &VerificationQueueController::statusRequested);
}

void VerificationQueueController::checkBlock(int pageIndex, int boxIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized) {
        return;
    }
    startVerifyQueue({{pageIndex, boxIndex}});
}

void VerificationQueueController::checkPageEnabledBlocks(int pageIndex, int recheck)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;

    int answered = 0;
    QList<int> candidates;
    collectEnabledBoxes(pageIndex, candidates, answered);

    QList<VerifyTask> tasks;
    for (const int box : std::as_const(candidates))
        tasks.append({pageIndex, box});
    startOrReportAnswered(tasks, answered, recheck);
}

void VerificationQueueController::checkAllEnabledBlocks(int recheck)
{
    if (m_deps.recognitionBusy() || checkBusy() || m_verifyQueueActive)
        return;

    QList<VerifyTask> tasks;
    int answered = 0;
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        QList<int> candidates;
        int pageAnswered = 0;
        collectEnabledBoxes(p, candidates, pageAnswered);
        answered += pageAnswered;
        for (const int box : std::as_const(candidates))
            tasks.append({p, box});
    }
    startOrReportAnswered(tasks, answered, recheck);
}

void VerificationQueueController::collectEnabledBoxes(int pageIndex, QList<int> &out, int &answered) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (!m_deps.verification.isTypeEnabled(box.label) && !box.duplicateSuspect)
            continue;
        // A re-recognized block (two text variants) is judged again on its
        // corrected text by the next run; Ok/Mismatch/Review have a verdict.
        if (box.checkStatus != BoxCheckStatus::NotChecked && box.checkStatus != BoxCheckStatus::ReRecognized) {
            ++answered;
            continue;
        }
        if (effectiveBoxText(box).isEmpty())
            continue;
        out.append(i);
    }
}

void VerificationQueueController::collectProblemBoxes(int pageIndex, QList<VerifyTask> &out) const
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    const DocumentPage &docPage = m_deps.document.page(pageIndex);
    if (!docPage.recognized || docPage.result.pages.isEmpty())
        return;
    const OcrPage &page = docPage.result.pages.first();
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoxCheckStatus status = page.boxes.at(i).checkStatus;
        if (status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review)
            out.append({pageIndex, i});
    }
}

// The layout pass creates text blocks with no text; a re-recognition that
// already answered leaves the text in correctedText. Image placeholders are
// never transcribed.
void VerificationQueueController::collectUnrecognizedBoxes(int pageIndex, QList<VerifyTask> &out) const
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
        if (box.checkStatus != BoxCheckStatus::NotChecked || !box.text.isEmpty() || !box.correctedText.isEmpty())
            continue;
        if (blockStyleForLabel(box.label, modelId).style == BlockStyle::ImagePlaceholder)
            continue;
        out.append({pageIndex, i});
    }
}

void VerificationQueueController::startOrReportAnswered(const QList<VerifyTask> &tasks, int answered, int recheck)
{
    if (!tasks.isEmpty()) {
        startVerifyQueue(tasks, recheck);
        return;
    }
    if (answered == 0)
        return;
    emit statusRequested(StatusMessage::join(
        {StatusMessage::translate("VerificationQueueController", "Nothing to check:"), StatusMessage::translate("VerificationQueueController", "%1 block(s) already verified").arg(answered)},
        QStringLiteral(" ")));
}

void VerificationQueueController::stop()
{
    m_verifyQueue.clear();
    m_recheckQueue.clear();
    if (m_verifyQueueActive) {
        m_verifyQueueActive = false;
        m_verifyPage = -1;
        m_verifyBoxIndex = -1;
        emit stateChanged();
    }
    if (m_decision.busy())
        m_decision.stop();
    if (m_check.busy())
        m_check.stop();
}

bool VerificationQueueController::pageVerificationSupported(int pageIndex) const
{
    if (!m_deps.document.isValidIndex(pageIndex) || !m_deps.document.page(pageIndex).recognized || m_deps.document.page(pageIndex).result.pages.isEmpty())
        return false;
    if (m_deps.recognitionBusy())
        return false;
    const OcrPage &page = m_deps.document.page(pageIndex).result.pages.first();
    for (const BoundingBox &box : std::as_const(page.boxes)) {
        if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !effectiveBoxText(box).isEmpty())
            return true;
    }
    return false;
}

bool VerificationQueueController::allPageVerificationSupported() const
{
    if (m_deps.recognitionBusy())
        return false;
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        const DocumentPage &docPage = m_deps.document.page(p);
        if (!docPage.recognized || docPage.result.pages.isEmpty())
            continue;
        const OcrPage &page = docPage.result.pages.first();
        for (const BoundingBox &box : std::as_const(page.boxes)) {
            if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !effectiveBoxText(box).isEmpty())
                return true;
        }
    }
    return false;
}

bool VerificationQueueController::pageRecheckSupported(int pageIndex) const
{
    QList<VerifyTask> tasks;
    collectProblemBoxes(pageIndex, tasks);
    return !tasks.isEmpty();
}

bool VerificationQueueController::pageBlocksRecognitionSupported(int pageIndex) const
{
    if (m_deps.recognitionBusy())
        return false;
    QList<VerifyTask> tasks;
    collectUnrecognizedBoxes(pageIndex, tasks);
    return !tasks.isEmpty();
}

bool VerificationQueueController::allPageRecheckSupported() const
{
    for (int p = 0; p < m_deps.document.pageCount(); ++p) {
        QList<VerifyTask> tasks;
        collectProblemBoxes(p, tasks);
        if (!tasks.isEmpty())
            return true;
    }
    return false;
}

void VerificationQueueController::startVerifyQueue(const QList<VerifyTask> &tasks, int recheck)
{
    if (m_deps.recognitionBusy() || checkBusy() || m_verifyQueueActive)
        return;
    if (tasks.isEmpty())
        return;

    m_checkError = StatusMessage();
    m_checkFinished = false;
    m_phase = Phase::Decision;
    m_matchThreshold = m_deps.settings.decisionMatchThreshold();
    m_autoRecheck = m_deps.settings.autoRecheck();
    m_runRecheck = recheck;
    m_verifyQueue = tasks;
    m_recheckQueue.clear();
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
    QList<VerifyTask> tasks;
    collectProblemBoxes(pageIndex, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::recheckPageBlocks(int pageIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;
    QList<VerifyTask> tasks;
    collectUnrecognizedBoxes(pageIndex, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::recheckAllProblemBlocks()
{
    if (m_deps.recognitionBusy() || checkBusy() || m_verifyQueueActive)
        return;
    QList<VerifyTask> tasks;
    for (int p = 0; p < m_deps.document.pageCount(); ++p)
        collectProblemBoxes(p, tasks);
    startRecheckQueue(tasks);
}

void VerificationQueueController::startRecheckQueue(const QList<VerifyTask> &tasks)
{
    if (m_deps.recognitionBusy() || checkBusy() || m_verifyQueueActive)
        return;
    if (tasks.isEmpty())
        return;

    m_checkError = StatusMessage();
    m_checkFinished = false;
    m_phase = Phase::Recheck;
    m_verifyQueue = tasks;
    m_recheckQueue.clear();
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
        if (m_phase == Phase::Decision && !m_recheckQueue.isEmpty()) {
            // Phase 2: the mismatches go to the block-recognition role. The
            // server is already on the decision model, so this reuses the
            // switch — one switch back instead of per-block restarts.
            m_phase = Phase::Recheck;
            m_verifyQueue = m_recheckQueue;
            m_recheckQueue.clear();
            m_verifyTotal = m_verifyQueue.size();
            m_verifyDone = 0;
            m_verifyPage = -1;
            m_verifyBoxIndex = -1;
            emit stateChanged();
        } else {
            finishVerifyQueue();
            return;
        }
    }

    const VerifyTask task = m_verifyQueue.takeFirst();
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
    const QString text = effectiveBoxText(boxes.at(m_verifyBoxIndex));
    const QImage crop = m_deps.cropProvider(m_verifyPage, m_verifyBoxIndex);
    if (crop.isNull()) {
        skipCurrentTask();
        return;
    }

    const BoundingBox &box = boxes.at(m_verifyBoxIndex);

    if (m_phase == Phase::Decision) {
        // The state carries the block's type, the candidate text and the
        // notation disclaimer; the wording is the role's question, with the
        // type's check list appended when the profile ships one, and the
        // profile's criteria (what each side of the yes/no answer means)
        // riding along.
        m_decision.judgeBlock(crop, box.label, text, decisionNotation(), decisionQuestion(box.label), decisionCriteriaTrue(), decisionCriteriaFalse());
        return;
    }

    // The transcription wording lives entirely in the model's profile
    // (systemPrompt + blockPrompts on the blockRecognition role). A block
    // that was already recognized also sends its original text, framed by the
    // role's recheckPrompt, so the model verifies and corrects it instead of
    // transcribing from scratch; a text-less block (layout markup) goes out
    // as before.
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    const QString typePrompt = ModelProfiles::blockPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"), box.label);
    const QString userPrompt = composeBlockUserPrompt(typePrompt, ModelProfiles::recheckPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition")), box.text);
    const QString systemPrompt = ModelProfiles::systemPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"));
    m_check.checkBlock(crop, systemPrompt, userPrompt);
}

QString VerificationQueueController::decisionQuestion(const QString &blockType) const
{
    const QString modelId = m_deps.settings.decisionRequestProfileId();
    const QList<ModelProfiles::Profile> &profiles = ModelProfiles::instance();
    // The ask itself; profiles predating the field fall back to the generic
    // question. The admission framing travels in the state (decisionNotation),
    // not here — a long instruction over a raw-string state was measured to
    // collapse the yes/no separation (docs/17).
    QString question = ModelProfiles::questionFor(profiles, modelId, QStringLiteral("decision"));
    if (question.isEmpty())
        question = StatusMessage::translate("VerificationQueueController", kFallbackQuestion).text();

    QStringList parts;
    parts.append(question);
    const QString typePrompt = ModelProfiles::blockPromptFor(profiles, modelId, QStringLiteral("decision"), blockType);
    if (!typePrompt.isEmpty())
        parts.append(typePrompt);
    return parts.join(QStringLiteral("\n\n"));
}

QString VerificationQueueController::decisionNotation() const
{
    const QString modelId = m_deps.settings.decisionRequestProfileId();
    const QList<ModelProfiles::Profile> &profiles = ModelProfiles::instance();
    // The state's notation disclaimer; profiles predating statePrompt fall
    // back to their system prompt.
    QString notation = ModelProfiles::statePromptFor(profiles, modelId, QStringLiteral("decision"));
    if (notation.isEmpty())
        notation = ModelProfiles::systemPromptFor(profiles, modelId, QStringLiteral("decision"));
    return notation;
}

QString VerificationQueueController::decisionCriteriaTrue() const
{
    return ModelProfiles::criteriaTrueFor(ModelProfiles::instance(), m_deps.settings.decisionRequestProfileId(), QStringLiteral("decision"));
}

QString VerificationQueueController::decisionCriteriaFalse() const
{
    return ModelProfiles::criteriaFalseFor(ModelProfiles::instance(), m_deps.settings.decisionRequestProfileId(), QStringLiteral("decision"));
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
    m_recheckQueue.clear();
    m_verifyPage = -1;
    m_verifyBoxIndex = -1;
    m_checkFinished = true;
    emit stateChanged();
}

}  // namespace llocr
