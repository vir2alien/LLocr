#pragma once

#include "app/BlockQueue.h"
#include "app/CheckController.h"

namespace llocr {

class RuntimeController;

// The block-recognition role's queue (ADR 146): every task re-transcribes its
// block from the crop alone. The transcription wording lives entirely in the
// model's profile (systemPrompt + blockPrompts on the blockRecognition role);
// per ADR 163 the block's previous text is not sent — presenting a candidate
// and getting confirm/fix/refuse back is the fix task's job.
class RecheckQueue : public BlockQueue
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        BlockRunGate &gate;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        RuntimeController &runtime;
        RequestProfileStore &checkRequestProfiles;
    };

    RecheckQueue(Deps deps, QObject *parent = nullptr);

    bool requestBusy() const override;

signals:
    void blockRecognized(int pageIndex, int boxIndex, const llocr::CheckResult &result);

protected:
    void startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text) override;
    void stopRequest() override;

private:
    Deps m_deps;
    CheckController m_check;
};

}  // namespace llocr
