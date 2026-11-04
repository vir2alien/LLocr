#pragma once

#include <functional>
#include <QImage>
#include <QObject>
#include <QString>

#include "app/BlockWorkList.h"
#include "app/CheckController.h"
#include "app/DocumentModel.h"
#include "app/FixController.h"
#include "app/VerificationPromptStore.h"
#include "core/CheckResult.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class BlockRunGate;
class RequestProfileStore;
class SettingsStore;

// The re-recognition and the remedy queues over one work-list runner: the
// recheck sends the queued blocks back through the blockRecognition role, the
// remedy runs the fix or the read protocol over them (ADR 163, ADR 259). The
// decision role has its own queue now (DecisionQueue); the gate keeps the
// three roles from running over each other.
class VerificationQueueController : public QObject
{
    Q_OBJECT

public:
    // One block of the work list: the page and the box on it. The work-list
    // vocabulary lives on BlockWorkList; the queue runs the lists it collects.
    using Task = BlockWorkList::Task;
    using TypeFilter = BlockWorkList::TypeFilter;

    struct Deps {
        DocumentModel &document;
        VerificationPromptStore &verification;
        RequestProfileStore &checkRequestProfiles;
        RequestProfileStore &fixRequestProfiles;
        SettingsStore &settings;
        RuntimeController &runtime;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        std::function<bool()> layoutBusy;
        // The decision queue runs on the same gate: a recheck or a remedy
        // must not start under a decision run, and the other way round.
        BlockRunGate &gate;
    };

    explicit VerificationQueueController(Deps deps, QObject *parent = nullptr);

    bool checkBusy() const { return m_check.busy() || m_fix.busy(); }
    bool queueActive() const { return m_verifyQueueActive; }
    bool finished() const { return m_checkFinished; }
    bool recheckPhase() const { return m_phase == Phase::Recheck; }
    bool fixPhase() const { return m_phase == Phase::Fix; }
    bool readPhase() const { return m_phase == Phase::Read; }
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

    // The work lists, collected over an explicit page set: the blocks the
    // recognition pass reads (no text yet), the blocks the check judges
    // (the enabled types and the duplicate suspects) and the blocks that
    // wait for the fixer.
    QList<Task> unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> enabledBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> fixableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;

public slots:
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
    // The read task (ADR 259): the same work list as the fix — the blocks that
    // wait for a remedy — but the model writes the block from the image
    // instead of correcting the recognized text. The single block form takes
    // any block with text.
    void readBlock(int pageIndex, int boxIndex);
    void readScopeBlocks(const QList<int> &pages);
    // The queues over an explicit work list — the stages of the auto
    // pipeline collect the blocks themselves (ADR 257).
    void recognizeBlocks(const QList<Task> &tasks);
    void fixBlocks(const QList<Task> &tasks);
    void stop();

signals:
    void stateChanged();
    // A run took the work list: the readouts that describe "the run" reset
    // here, not on the per-request state changes that follow.
    void runStarted();
    void statusRequested(const llocr::StatusMessage &message);
    // One signal per result source: the block-recognition transcription
    // (blockRecognized), the fix outcome (blockFixed) and the read outcome
    // (blockRead) drive different box transitions (ADR 164, ADR 260). The
    // decision verdict travels on DecisionQueue::blockVerified.
    void blockRecognized(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void blockFixed(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void blockRead(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void problemReported(const llocr::StatusMessage &message);
    // The queue ran out of work. The auto pipeline waits for it to move on
    // to the next stage (ADR 257).
    void queueFinished();

private:
    enum class Phase {
        Recheck,  ///< the block-recognition role re-runs the queued blocks
        Fix,      ///< the fix protocol corrects the queued blocks
        Read,     ///< the read protocol writes the queued blocks from the image
    };

    void startRecheckQueue(const QList<Task> &tasks);
    void startFixQueue(const QList<Task> &tasks);
    void startReadQueue(const QList<Task> &tasks);
    void startRemedyQueue(const QList<Task> &tasks, Phase phase);
    void startNextVerify();
    void skipCurrentTask();
    void finishVerifyQueue();

    Deps m_deps;
    BlockWorkList m_workList;
    CheckController m_check;
    FixController m_fix;

    Phase m_phase = Phase::Recheck;
    QList<Task> m_verifyQueue;
    int m_verifyPage = -1;
    int m_verifyBoxIndex = -1;
    bool m_verifyQueueActive = false;
    bool m_checkFinished = false;
    int m_verifyTotal = 0;
    int m_verifyDone = 0;
    // What the read run did with the blocks it read: the reader's answers are
    // the run's output, and the run says how many blocks it rewrote, how many
    // it confirmed and how many it could not read (ADR 261).
    int m_readRewritten = 0;
    int m_readConfirmed = 0;
    int m_readUnreadable = 0;
    StatusMessage m_checkError;
};

}  // namespace llocr
