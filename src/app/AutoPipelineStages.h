#pragma once

#include <functional>

#include <QString>

#include "app/AutoPipelineStage.h"
#include "app/BlockQueue.h"
#include "app/BlockWorkList.h"
#include "app/DecisionQueue.h"
#include "app/RecheckQueue.h"
#include "app/RemedyQueue.h"

namespace llocr {

class LayoutController;

// The markup stage: the layout pass over the scope. The prompt, the parser
// and the orientation check live in the facade, so the stage asks for the
// pass and is told when the whole pass is over — including the second pass
// of a page the markup turned (ADR 246). The scope filter lets the facade
// narrow the pass (the continue mode's unmarked pages) for both the skip
// decision and the run.
class AutoMarkupStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Runs the markup pass over the pages and calls done(ok) when the pass
    // is over; ok is false when the pass could not complete.
    using RunPass = std::function<void(const AutoPipelineStage::Scope &, const std::function<void(bool)> &)>;
    using ScopeFilter = std::function<AutoPipelineStage::Scope(const AutoPipelineStage::Scope &)>;

    explicit AutoMarkupStage(LayoutController &layout, RunPass runPass, ScopeFilter scopeFilter = {}, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    LayoutController &m_layout;
    RunPass m_runPass;
    ScopeFilter m_scopeFilter;
    bool m_running = false;
};

// The recognition stage: every block of the scope that has no text yet.
class AutoRecognitionStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Whether the model for this task is configured — the stage skips
    // itself when it is not.
    using ModelAvailable = std::function<bool()>;

    AutoRecognitionStage(RecheckQueue &queue, BlockWorkList &workList, ModelAvailable modelAvailable, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    RecheckQueue &m_queue;
    BlockWorkList &m_workList;
    ModelAvailable m_modelAvailable;
    bool m_running = false;
    // The work list collected in skipReason; the run stage asks the reason
    // and starts back to back, so the collection happens once (docs/11 §10).
    mutable QList<BlockWorkList::Task> m_cachedWork;
    mutable bool m_workCached = false;
    // The work list of skipReason, or a fresh collection when the stage was
    // started without its reason being asked first.
    QList<BlockWorkList::Task> collectedWork(const AutoPipelineStage::Scope &pages);
};

// The verification stage: the blocks of the enabled types that have no
// verdict on their effective layer (ADR 164). The same stage serves the
// second check — after the fix, the correction layer is the effective one
// and its blocks have no verdict (ADR 257). The two checks differ in their
// title: the check of the corrected blocks is a stage of its own.
class AutoVerificationStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Whether the model for this task is configured — the stage skips
    // itself when it is not.
    using ModelAvailable = std::function<bool()>;

    AutoVerificationStage(DecisionQueue &queue, BlockWorkList &workList, ModelAvailable modelAvailable, QString titleKey, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    DecisionQueue &m_queue;
    BlockWorkList &m_workList;
    ModelAvailable m_modelAvailable;
    bool m_running = false;
    // The work list collected in skipReason; the run stage asks the reason
    // and starts back to back, so the collection happens once (docs/11 §10).
    mutable QList<BlockWorkList::Task> m_cachedWork;
    mutable bool m_workCached = false;
    // The work list of skipReason, or a fresh collection when the stage was
    // started without its reason being asked first.
    QList<BlockWorkList::Task> collectedWork(const AutoPipelineStage::Scope &pages);
};

// The fix stage: the blocks that wait for the fixer (ADR 188).
class AutoFixStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    AutoFixStage(RemedyQueue &queue, BlockWorkList &workList, AutoVerificationStage::ModelAvailable modelAvailable, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    RemedyQueue &m_queue;
    BlockWorkList &m_workList;
    AutoVerificationStage::ModelAvailable m_modelAvailable;
    bool m_running = false;
    // The work list collected in skipReason; the run stage asks the reason
    // and starts back to back, so the collection happens once (docs/11 §10).
    mutable QList<BlockWorkList::Task> m_cachedWork;
    mutable bool m_workCached = false;
    // The work list of skipReason, or a fresh collection when the stage was
    // started without its reason being asked first.
    QList<BlockWorkList::Task> collectedWork(const AutoPipelineStage::Scope &pages);
};

}  // namespace llocr
