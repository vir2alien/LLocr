#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>

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

    void judgeBlock(const QImage &image, const QString &recognizedText, const QString &question, const QString &criteriaTrue = QString(), const QString &criteriaFalse = QString());

signals:
    void judgeFinished(const llocr::DecisionResult &result);

private:
    void abortRequest() override;

    std::unique_ptr<DecisionModel> m_model;
    QFutureWatcher<DecisionResult> m_watcher;
};

}  // namespace llocr
