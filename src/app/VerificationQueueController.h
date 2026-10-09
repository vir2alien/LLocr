#pragma once

#include <functional>
#include <QImage>
#include <QObject>
#include <QString>

#include "app/CheckController.h"
#include "app/DecisionController.h"
#include "app/DocumentModel.h"
#include "app/VerificationPromptStore.h"
#include "core/CheckResult.h"
#include "core/StatusMessage.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;
class SettingsStore;

// The two-phase verification queue (ADR 145). Phase 1 sends every queued
// block to the decision model, which answers a probability instead of
// generating text: at/above the threshold the block is marked Ok, below it —
// Mismatch. With automatic re-recognition enabled, phase 2 re-runs the
// mismatched blocks through the block-recognition role right away, so one
// verification run costs at most two server role switches.
class VerificationQueueController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        VerificationPromptStore &verification;
        RequestProfileStore &checkRequestProfiles;
        SettingsStore &settings;
        RuntimeController &runtime;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        std::function<bool()> recognitionBusy;
    };

    explicit VerificationQueueController(Deps deps, QObject *parent = nullptr);

    bool checkBusy() const { return m_check.busy() || m_decision.busy(); }
    bool queueActive() const { return m_verifyQueueActive; }
    bool finished() const { return m_checkFinished; }
    bool decisionPhase() const { return m_phase == Phase::Decision; }
    int progressDone() const { return m_verifyDone; }
    int progressTotal() const { return m_verifyTotal; }
    QString errorMessage() const { return m_checkError.text(); }

    bool pageVerificationSupported(int pageIndex) const;
    bool allPageVerificationSupported() const;

    // Problem blocks = the statuses that ask for a re-run: the decision stage
    // rejected the text (Mismatch) or the block could not be transcribed
    // (Review). Fixed blocks were already re-recognized.
    bool pageRecheckSupported(int pageIndex) const;
    bool allPageRecheckSupported() const;

public slots:
    void checkBlock(int pageIndex, int boxIndex);
    void checkPageEnabledBlocks(int pageIndex);
    void checkAllEnabledBlocks();
    // Re-recognition runs the queue straight in its second phase. The single
    // block form takes any status; the page/all forms pick the problem blocks.
    void recheckBlock(int pageIndex, int boxIndex);
    void recheckPageProblemBlocks(int pageIndex);
    void recheckAllProblemBlocks();
    void stop();

signals:
    void stateChanged();
    void statusRequested(const llocr::StatusMessage &message);
    void blockChecked(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void problemReported(const llocr::StatusMessage &message);

private:
    enum class Phase {
        Decision,  ///< the decision model judges every queued block
        Recheck,   ///< the block-recognition role re-runs the mismatches
    };

    struct VerifyTask {
        int page;
        int box;
    };

    void startVerifyQueue(const QList<VerifyTask> &tasks);
    void startRecheckQueue(const QList<VerifyTask> &tasks);
    void startOrReportAnswered(const QList<VerifyTask> &tasks, int answered);
    void startNextVerify();
    void finishVerifyQueue();
    void collectEnabledBoxes(int pageIndex, QList<int> &out, int &answered) const;
    void collectProblemBoxes(int pageIndex, QList<VerifyTask> &out) const;
    QString decisionQuestion() const;

    Deps m_deps;
    DecisionController m_decision;
    CheckController m_check;

    Phase m_phase = Phase::Decision;
    QList<VerifyTask> m_verifyQueue;
    QList<VerifyTask> m_recheckQueue;
    int m_verifyPage = -1;
    int m_verifyBoxIndex = -1;
    bool m_verifyQueueActive = false;
    bool m_checkFinished = false;
    int m_verifyTotal = 0;
    int m_verifyDone = 0;
    double m_matchThreshold = 0.5;
    bool m_autoRecheck = false;
    StatusMessage m_checkError;
};

}  // namespace llocr
