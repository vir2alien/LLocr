#pragma once

#include <functional>

#include <QString>

#include "app/AutoPipelineStage.h"
#include "app/VerificationQueueController.h"

namespace llocr {

class LayoutController;

// The markup stage: the layout pass over the scope. The prompt, the parser
// and the orientation check live in the facade, so the stage asks for the
// pass and is told when the whole pass is over — including the second pass
// of a page the markup turned (ADR 246).
class AutoMarkupStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Runs the markup pass over the pages and calls done(ok) when the pass
    // is over; ok is false when the pass could not complete.
    using RunPass = std::function<void(const AutoPipelineStage::Scope &, const std::function<void(bool)> &)>;

    explicit AutoMarkupStage(LayoutController &layout, RunPass runPass, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    LayoutController &m_layout;
    RunPass m_runPass;
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

    AutoRecognitionStage(VerificationQueueController &queue, ModelAvailable modelAvailable, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    ModelAvailable m_modelAvailable;
    bool m_running = false;
};

// The verification stage: the blocks of the enabled types that have no
// verdict on their effective layer (ADR 164). The same stage serves the
// second check — after the fix, the correction layer is the effective one
// and its blocks have no verdict (ADR 257). The id follows the title key:
// the check of the corrected blocks is a stage of its own, so it does not
// carry the id of the first check.
class AutoVerificationStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Whether the model for this task is configured — the stage skips
    // itself when it is not.
    using ModelAvailable = std::function<bool()>;

    AutoVerificationStage(VerificationQueueController &queue, ModelAvailable modelAvailable, QString titleKey, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    ModelAvailable m_modelAvailable;
    bool m_running = false;
};

// The fix stage: the blocks that wait for the fixer (ADR 188).
class AutoFixStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    AutoFixStage(VerificationQueueController &queue, AutoVerificationStage::ModelAvailable modelAvailable, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    AutoVerificationStage::ModelAvailable m_modelAvailable;
    bool m_running = false;
};

}  // namespace llocr
