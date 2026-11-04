#pragma once

#include <functional>
#include <QPointer>

namespace llocr {

class BlockQueue;

// The one-run-at-a-time rule for the block queues: a run starts only when no
// other queue holds the gate and no external job does. The external holders
// are the markup pass and a stopped request still winding down — a new run
// must not start under either (the queue runs and the markup are mutually
// exclusive, ADR 74).
class BlockRunGate
{
    Q_DISABLE_COPY_MOVE(BlockRunGate)

public:
    BlockRunGate() = default;
    void setExternalBusy(std::function<bool()> busy);
    // Takes the gate for the queue, or refuses: the gate is held or an
    // external job is busy. A refusal is not an error — the caller reports it.
    bool tryAcquire(BlockQueue *owner);
    void release(BlockQueue *owner);

private:
    std::function<bool()> m_externalBusy;
    QPointer<BlockQueue> m_holder;
};

}  // namespace llocr
