#pragma once

#include <functional>
#include <QImage>
#include <QObject>
#include <QString>

#include "app/CheckController.h"
#include "app/DecisionController.h"
#include "app/DocumentModel.h"
#include "app/FixController.h"
#include "app/VerificationPromptStore.h"
#include "core/CheckResult.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;
class SettingsStore;

// The two-phase verification queue (ADR 145, ADR 163). Phase 1 sends every
// queued block to the decision model, which answers probabilities instead of
// generating text: the v6 routing (docs/21) marks a block Ok when both the
// noul and the choice head agree, sends an empty/unreadable crop to a human
// (Review) and everything else to Mismatch — the threshold is per block type
// when the profile ships the map. The fix is a separate action (ADR 178): the
// user sends the mismatched blocks through the fix task's OK / FIX / REVIEW
// protocol with the «Fix» button, so one check run costs one server role
// switch.
class VerificationQueueController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        VerificationPromptStore &verification;
        RequestProfileStore &checkRequestProfiles;
        RequestProfileStore &fixRequestProfiles;
        SettingsStore &settings;
        RuntimeController &runtime;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        std::function<bool()> recognitionBusy;
    };

    explicit VerificationQueueController(Deps deps, QObject *parent = nullptr);

    bool checkBusy() const { return m_check.busy() || m_decision.busy() || m_fix.busy(); }
    bool queueActive() const { return m_verifyQueueActive; }
    bool finished() const { return m_checkFinished; }
    bool decisionPhase() const { return m_phase == Phase::Decision; }
    bool recheckPhase() const { return m_phase == Phase::Recheck; }
    bool fixPhase() const { return m_phase == Phase::Fix; }
    int progressDone() const { return m_verifyDone; }
    int progressTotal() const { return m_verifyTotal; }
    QString errorMessage() const { return m_checkError.text(); }

    bool pageVerificationSupported(int pageIndex) const;
    bool allPageVerificationSupported() const;

    // Problem blocks = the statuses that ask for a re-run: the decision stage
    // rejected the text (Mismatch) or the block could not be transcribed
    // (Review). Re-recognized blocks have no verdict yet and are re-judged by
    // the next verification run instead.
    bool pageRecheckSupported(int pageIndex) const;
    bool allPageRecheckSupported() const;

    // Blocks created by the layout pass have no text yet; this collects them
    // for their block-recognition pass.
    bool pageBlocksRecognitionSupported(int pageIndex) const;

public slots:
    void checkBlock(int pageIndex, int boxIndex);
    void checkPageEnabledBlocks(int pageIndex);
    void checkAllEnabledBlocks();
    // The toolbar's scoped spellings: the same collections over an explicit
    // page set (the thumbnail selection).
    void checkPagesEnabledBlocks(const QList<int> &pages);
    // Re-recognition runs the queue straight in its second phase. The single
    // block form takes any status; the page/all forms pick the problem blocks.
    void recheckBlock(int pageIndex, int boxIndex);
    void recheckPageProblemBlocks(int pageIndex);
    void recheckAllProblemBlocks();
    // Recognize every block of the page that has no text yet (the layout-pass
    // blocks); image placeholders are skipped.
    void recheckPageBlocks(int pageIndex);
    void recheckPagesBlocks(const QList<int> &pages);
    // The fix pipeline (ADR 163): a single block regardless of status (the
    // entry guards on the block having text), or every Mismatch/Review block
    // of the document.
    void fixBlock(int pageIndex, int boxIndex);
    void fixAllProblemBlocks();
    void fixPagesProblemBlocks(const QList<int> &pages);
    void stop();

signals:
    void stateChanged();
    void statusRequested(const llocr::StatusMessage &message);
    // One signal per result source: the decision verdict (blockVerified), the
    // block-recognition transcription (blockRecognized) and the fix outcome
    // (blockFixed) drive different box transitions (ADR 164).
    void verificationStarted(int pageIndex, int boxIndex);
    void blockVerified(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void blockRecognized(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void blockFixed(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void problemReported(const llocr::StatusMessage &message);

private:
    enum class Phase {
        Decision,  ///< the decision model judges every queued block
        Recheck,   ///< the block-recognition role re-runs the queued blocks
        Fix,       ///< the fix protocol corrects the queued blocks
    };

    struct VerifyTask {
        int page;
        int box;
    };

    void startVerifyQueue(const QList<VerifyTask> &tasks);
    void startRecheckQueue(const QList<VerifyTask> &tasks);
    void startFixQueue(const QList<VerifyTask> &tasks);
    void startOrReportAnswered(const QList<VerifyTask> &tasks, int answered);
    void startNextVerify();
    void skipCurrentTask();
    void finishVerifyQueue();
    void collectEnabledBoxes(int pageIndex, QList<int> &out, int &answered) const;
    void collectProblemBoxes(int pageIndex, QList<VerifyTask> &out) const;
    void collectUnrecognizedBoxes(int pageIndex, QList<VerifyTask> &out) const;
    QString decisionQuestion(const QString &blockType) const;
    QString decisionNotation(const QString &blockType) const;
    QJsonObject decisionQuestions(const QString &blockType) const;
    QString decisionCriteriaTrue() const;
    QString decisionCriteriaFalse() const;
    double matchThresholdFor(const QString &blockType) const;

    Deps m_deps;
    DecisionController m_decision;
    CheckController m_check;
    FixController m_fix;

    Phase m_phase = Phase::Decision;
    QList<VerifyTask> m_verifyQueue;
    int m_verifyPage = -1;
    int m_verifyBoxIndex = -1;
    bool m_verifyQueueActive = false;
    bool m_checkFinished = false;
    int m_verifyTotal = 0;
    int m_verifyDone = 0;
    double m_matchThreshold = 0.5;
    const ModelProfiles::Role *m_decisionRole = nullptr;  // resolved per run: v6 thresholds live on the role
    QString m_judgedType;                                 // the block type of the verdict in flight
    StatusMessage m_checkError;
};

}  // namespace llocr
