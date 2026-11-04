#pragma once

#include <functional>
#include <QImage>
#include <QString>

#include "app/BlockQueue.h"
#include "app/DecisionController.h"
#include "core/ModelProfiles.h"

namespace llocr {

class RuntimeController;
class SettingsStore;

// The decision role's queue (docs/09): every task goes to the visual-diff
// decision (docs/17) — the crop and the candidate rendered back into an image
// travel together, and the verdict is the threshold on P(match) — Ok above
// it, Mismatch below (ADR 235/254; the thresholds are per block type and
// measured, ADR 184/258). The fix is a separate action (ADR 178), so one
// decision run costs one server role switch.
class DecisionQueue : public BlockQueue
{
    Q_OBJECT

public:
    // The visual-diff decision's render leg: the candidate text painted back
    // into an image before the request goes out. The app supplies the
    // CandidateRenderer; a headless test can install a stand-in painter.
    using RenderCandidateFn = std::function<void(const QString &candidateText, const std::function<void(bool ok, const QImage &image, const QString &error)> &)>;

    struct Deps {
        DocumentModel &document;
        BlockRunGate &gate;
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        RuntimeController &runtime;
        SettingsStore &settings;
        // The text of a block as the page shows it: a formula as display math,
        // a table as a grid. The visual-diff decision paints its candidate
        // from it, so the picture the model reads is the one the document
        // carries — the raw block text would reach the renderer as literal
        // LaTeX and the table markup as a run-on line (ADR 237).
        std::function<QString(int pageIndex, int boxIndex)> displayTextProvider;
        // The visual-diff decision needs the candidate rendered back into an
        // image before the request goes out. Empty when no renderer is wired:
        // a visual-diff run then lands its blocks on Review instead of
        // judging them.
        RenderCandidateFn renderCandidate;
    };

    DecisionQueue(Deps deps, QObject *parent = nullptr);

    bool requestBusy() const override;

signals:
    // The old verdict of the layer being judged is erased while the request
    // is in flight: a failed run leaves the block gray instead of showing a
    // stale verdict (ADR 164).
    void verificationStarted(int pageIndex, int boxIndex);
    void blockVerified(int pageIndex, int boxIndex, const llocr::CheckResult &result);

protected:
    void beginRun() override;
    void startTask(const Task &task, const BoundingBox &box, const QImage &crop, const QString &text) override;
    void stopRequest() override;

private:
    void deliverVerdict(const CheckResult &verdict);
    double matchThresholdFor(const QString &blockType) const;

    Deps m_deps;
    DecisionController m_decision;
    double m_matchThreshold = 0.5;
    const ModelProfiles::Role *m_decisionRole = nullptr;  // resolved per run: the per-block thresholds live on the role
    QString m_judgedType;                                 // the block type of the verdict in flight
};

}  // namespace llocr
