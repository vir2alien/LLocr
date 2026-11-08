#include "app/RemedyQueue.h"

#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "core/CheckRequest.h"
#include "core/ModelProfiles.h"

namespace llocr {

RemedyQueue::RemedyQueue(Deps deps, QObject *parent)
    : BlockQueue({deps.document, deps.gate, deps.cropProvider}, parent), m_deps(std::move(deps)), m_fix(m_deps.fixRequestProfiles, m_deps.settings, m_deps.runtime, nullptr)
{
    connect(&m_fix, &FixController::fixFinished, this, [this](const CheckResult &result) {
        if (result.status == CheckStatus::Failed && !result.errorMessage.isEmpty())
            reportNewError(result.errorMessage);
        const int page = taskPage();
        const int box = taskBox();
        // The reader has its own outcomes (ADR 260): the block was written
        // from the image, not corrected.
        if (m_protocol == Protocol::Read) {
            // What the reader did with the block: the run's output is the
            // count of rewritten, confirmed and unreadable blocks (ADR 261).
            switch (result.status) {
            case CheckStatus::Fixed:
                ++m_readRewritten;
                break;
            case CheckStatus::Ok:
                ++m_readConfirmed;
                break;
            case CheckStatus::Review:
                ++m_readUnreadable;
                break;
            case CheckStatus::Failed:
            case CheckStatus::Recognized:
            case CheckStatus::Mismatch:
                break;
            }
            emit blockRead(page, box, result);
        } else {
            emit blockFixed(page, box, result);
        }
        advance();
    });
    connect(&m_fix, &FixController::busyChanged, this, &RemedyQueue::stateChanged);
    connect(&m_fix, &FixController::statusRequested, this, &RemedyQueue::statusRequested);
}

// The protocol commits only when the run takes the list: a refused call must
// not re-route the results of a run still in flight on the other protocol.
// The first task starts one event-loop turn later (the queue's own deferred
// start), so the protocol is in place before any result can arrive.
bool RemedyQueue::runFix(const QList<Task> &tasks, const PrepareTask &prepare)
{
    if (!run(tasks, prepare))
        return false;
    m_protocol = Protocol::Fix;
    return true;
}

bool RemedyQueue::runRead(const QList<Task> &tasks, const PrepareTask &prepare)
{
    if (!run(tasks, prepare))
        return false;
    m_protocol = Protocol::Read;
    return true;
}

bool RemedyQueue::requestBusy() const
{
    return m_fix.busy();
}

void RemedyQueue::beginRun()
{
    m_readRewritten = 0;
    m_readConfirmed = 0;
    m_readUnreadable = 0;
}

void RemedyQueue::startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text)
{
    if (m_protocol == Protocol::Read) {
        // The reader's prompts live in their own profile (ADR 259): the
        // protocol is the action's choice, not the model's. The shared
        // wording is the default; a model profile may ship its own system
        // prompt, because a wording is a calibration like the thresholds
        // (ADR 267).
        const ModelProfiles::PromptProfile *readProfile = ModelProfiles::promptProfile(QString::fromLatin1(ModelProfiles::kReadPromptProfileId));
        if (!readProfile) {
            reportNewError(StatusMessage::translate("RemedyQueue", "The read prompts (%1) are missing: the block was not sent.").arg(QString::fromLatin1(ModelProfiles::kReadPromptProfileId)));
            advance();
            return;
        }
        // The reader gets the image, the system prompt and the block prompt —
        // and nothing else: the recognized text of the block is not part of
        // the request (ADR 266). The shared profile carries the default
        // wording; a model profile may ship its own system prompt and block
        // prompts, because a wording is a calibration like the thresholds
        // (ADR 267).
        QString systemPrompt = readProfile->prompts.systemPrompt;
        const ModelProfiles::Role *fixRole = ModelProfiles::roleFor(m_deps.fixRequestProfiles.activeProfileId(), QStringLiteral("fix"));
        if (fixRole && !fixRole->readSystemPrompt.isEmpty())
            systemPrompt = fixRole->readSystemPrompt;
        const QString userPrompt = ModelProfiles::readBlockPromptFor(fixRole, *readProfile, box.label);
        const int maxOutput = ModelProfiles::maxOutputForBlockRole(readProfile->prompts, box.label);
        m_fix.fixBlock(crop, text, systemPrompt, userPrompt, box.label, maxOutput, CheckRequest::FixProtocol::ReadBlock);
        return;
    }

    // The fix protocol judges the block's current text; without one there
    // is no candidate to confirm or correct — the task is skipped.
    if (text.isEmpty()) {
        advance();
        return;
    }
    const QString modelId = m_deps.fixRequestProfiles.activeProfileId();
    const QList<ModelProfiles::Profile> &profiles = ModelProfiles::instance();
    const QString userPrompt = ModelProfiles::blockPromptFor(profiles, modelId, QStringLiteral("fix"), box.label);
    QString systemPrompt = ModelProfiles::systemPromptFor(profiles, modelId, QStringLiteral("fix"));
    const int maxOutput = ModelProfiles::maxOutputForBlock(profiles, modelId, QStringLiteral("fix"), box.label);
    // The fix protocol judges the block's current text; the candidate is
    // the block's own text — a formula was brought to its canonical LaTeX
    // form when it was recognized (ADR 194), so the fixer sees the same
    // bytes the checker saw.
    m_fix.fixBlock(crop, text, systemPrompt, userPrompt, box.label, maxOutput);
}

void RemedyQueue::stopRequest()
{
    m_fix.stop();
}

// The read run says what it did: the reader's answers are the run's output,
// and without the count a run that only confirmed the blocks looks like a run
// that did nothing (ADR 261).
void RemedyQueue::finishRun()
{
    if (m_protocol == Protocol::Read && progressTotal() > 0) {
        emit statusRequested(StatusMessage::translate("RemedyQueue", "Blocks read: %1 rewritten, %2 confirmed, %3 unreadable.").arg(m_readRewritten).arg(m_readConfirmed).arg(m_readUnreadable));
    }
}

}  // namespace llocr
