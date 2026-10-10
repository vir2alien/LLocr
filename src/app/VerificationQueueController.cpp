#include "app/VerificationQueueController.h"

#include <QTimer>

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "parsers/BlockStyle.h"

namespace llocr {

namespace {
// The generic yes/no question when the decision profile ships none (the
// shipped d1-3b profile carries its own wording).
constexpr const char *kFallbackQuestion = "The state is a text fragment recognized from the attached image by an OCR "
                                          "model. Does the image show exactly this text?";
}  // namespace

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
        } else if (result.probability >= m_matchThreshold) {
            verdict.status = CheckStatus::Ok;
        } else {
            verdict.status = CheckStatus::Mismatch;
        }
        const int page = m_verifyPage;
        const int box = m_verifyBoxIndex;
        emit blockChecked(page, box, verdict);
        ++m_verifyDone;
        if (verdict.status == CheckStatus::Mismatch && m_autoRecheck)
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

void VerificationQueueController::checkPageEnabledBlocks(int pageIndex)
{
    if (!m_deps.document.isValidIndex(pageIndex))
        return;

    int answered = 0;
    QList<int> candidates;
    collectEnabledBoxes(pageIndex, candidates, answered);

    QList<VerifyTask> tasks;
    for (const int box : std::as_const(candidates))
        tasks.append({pageIndex, box});
    startOrReportAnswered(tasks, answered);
}

void VerificationQueueController::checkAllEnabledBlocks()
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
    startOrReportAnswered(tasks, answered);
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
        if (box.text.isEmpty())
            continue;
        if (box.checkStatus != BoxCheckStatus::NotChecked) {
            ++answered;
            continue;
        }
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

void VerificationQueueController::startOrReportAnswered(const QList<VerifyTask> &tasks, int answered)
{
    if (!tasks.isEmpty()) {
        startVerifyQueue(tasks);
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
        if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !box.text.isEmpty())
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
            if ((m_deps.verification.isTypeEnabled(box.label) || box.duplicateSuspect) && !box.text.isEmpty())
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

void VerificationQueueController::startVerifyQueue(const QList<VerifyTask> &tasks)
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
    const DocumentPage &docPage = m_deps.document.page(m_verifyPage);
    if (!docPage.recognized || docPage.result.pages.isEmpty()) {
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
        return;
    }
    const QList<BoundingBox> &boxes = docPage.result.pages.first().boxes;
    if (m_verifyBoxIndex < 0 || m_verifyBoxIndex >= boxes.size()) {
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
        return;
    }
    const QString text = boxes.at(m_verifyBoxIndex).text;
    const QImage crop = m_deps.cropProvider(m_verifyPage, m_verifyBoxIndex);
    if (crop.isNull()) {
        ++m_verifyDone;
        emit stateChanged();
        QTimer::singleShot(0, this, [this]() { startNextVerify(); });
        return;
    }

    if (m_phase == Phase::Decision) {
        m_decision.judgeBlock(crop, text, decisionQuestion());
        return;
    }

    const BoundingBox &box = boxes.at(m_verifyBoxIndex);
    // The transcription wording lives entirely in the model's profile
    // (systemPrompt + blockPrompts on the blockRecognition role); the block
    // is recognized from scratch, the old text is not part of the request.
    const QString modelId = m_deps.checkRequestProfiles.activeProfileId();
    const QString typePrompt = ModelProfiles::blockPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"), box.label);
    const QString systemPrompt = ModelProfiles::systemPromptFor(ModelProfiles::instance(), modelId, QStringLiteral("blockRecognition"));
    m_check.checkBlock(crop, systemPrompt, typePrompt);
}

QString VerificationQueueController::decisionQuestion() const
{
    const QString question = ModelProfiles::systemPromptFor(ModelProfiles::instance(), m_deps.settings.decisionRequestProfileId(), QStringLiteral("decision"));
    return question.isEmpty() ? StatusMessage::translate("VerificationQueueController", kFallbackQuestion).text() : question;
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
