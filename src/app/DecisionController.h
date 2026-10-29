#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QString>

#include "app/ModelRequestController.h"
#include "core/DecisionRequest.h"
#include "core/DecisionResult.h"
#include "models/DecisionModel.h"

namespace llocr {

// Drives one decision request: resolves the decision-model connection, judges
// the block, and can stop an in-flight request. The queue that feeds it lives
// in VerificationQueueController.
class DecisionController : public ModelRequestController
{
    Q_OBJECT

public:
    explicit DecisionController(RuntimeController &runtime, QObject *parent = nullptr);

    // The visual-diff decision (docs/31): the crop and the candidate rendered
    // back into an image travel together, the text rides as the bare state.
    void judgeVisual(const QImage &crop, const QImage &renderImage, const QString &recognizedText);

signals:
    void judgeFinished(const llocr::DecisionResult &result);

private:
    void abortRequest() override;
    void ask(const DecisionRequest &request);

    std::unique_ptr<DecisionModel> m_model;
    QFutureWatcher<DecisionResult> m_watcher;
};

}  // namespace llocr
