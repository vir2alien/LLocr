#pragma once

#include "app/BlockQueue.h"
#include "app/FixController.h"

namespace llocr {

class RuntimeController;
class SettingsStore;

// The remedy queue (ADR 163, ADR 259): the fix and the read are the same
// remedy over the same work list — the blocks that wait for a hand. Only the
// protocol differs: the fix corrects the recognized text (OK / FIX / REVIEW),
// the read writes the block from the image alone (ADR 260 — the protocol
// rides the request, not the model).
class RemedyQueue : public BlockQueue
{
    Q_OBJECT

public:
    enum class Protocol {
        Fix,   ///< the fix protocol corrects the queued blocks
        Read,  ///< the read protocol writes the queued blocks from the image
    };

    struct Deps {
        DocumentModel &document;
        BlockRunGate &gate;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        RuntimeController &runtime;
        SettingsStore &settings;
        RequestProfileStore &fixRequestProfiles;
    };

    RemedyQueue(Deps deps, QObject *parent = nullptr);

    // The fix pipeline (ADR 163): the Mismatch/Review blocks go through the
    // fix task's OK / FIX / REVIEW protocol.
    bool runFix(const QList<Task> &tasks, const PrepareTask &prepare = {});
    // The read task (ADR 259): the model writes the block from the image.
    bool runRead(const QList<Task> &tasks, const PrepareTask &prepare = {});

    bool requestBusy() const override;
    // Which remedy is in flight: the header maps the stop button by it
    // (ADR 171), and the read speaks in the frame while the fix does not
    // (ADR 240, ADR 261).
    bool fixActive() const { return active() && m_protocol == Protocol::Fix; }
    bool readActive() const { return active() && m_protocol == Protocol::Read; }

signals:
    void blockFixed(int pageIndex, int boxIndex, const llocr::CheckResult &result);
    void blockRead(int pageIndex, int boxIndex, const llocr::CheckResult &result);

protected:
    void beginRun() override;
    void startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text) override;
    void stopRequest() override;
    void finishRun() override;

private:
    Deps m_deps;
    FixController m_fix;
    Protocol m_protocol = Protocol::Fix;
    // What the read run did with the blocks it read: the reader's answers are
    // the run's output, and the run says how many blocks it rewrote, how many
    // it confirmed and how many it could not read (ADR 261).
    int m_readRewritten = 0;
    int m_readConfirmed = 0;
    int m_readUnreadable = 0;
};

}  // namespace llocr
