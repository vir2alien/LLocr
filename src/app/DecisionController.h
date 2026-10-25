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

    void judgeBlock(const QImage &image,
                    const QString &blockType,
                    const QString &recognizedText,
                    const QString &notation,
                    const QString &question,
                    const QString &criteriaTrue = QString(),
                    const QString &criteriaFalse = QString());

    // A choice question: the model answers with the probability of every
    // class instead of a yes/no verdict (the orientation check).
    void askChoice(const QImage &image, const QString &blockType, const QString &notation, const QJsonObject &questions);

signals:
    void judgeFinished(const llocr::DecisionResult &result);

private:
    void abortRequest() override;
    void ask(const DecisionRequest &request);

    std::unique_ptr<DecisionModel> m_model;
    QFutureWatcher<DecisionResult> m_watcher;
};

}  // namespace llocr
