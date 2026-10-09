#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/ConnectionConfig.h"
#include "core/DecisionRequest.h"
#include "core/DecisionResult.h"
#include "core/StatusMessage.h"
#include "models/DecisionModel.h"
#include "runtime/RuntimeController.h"

namespace llocr {

// Drives one decision request: resolves the decision-model connection, judges
// the block, and can stop an in-flight request. The queue that feeds it lives
// in VerificationQueueController.
class DecisionController : public QObject
{
    Q_OBJECT

public:
    explicit DecisionController(RuntimeController &runtime, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void judgeBlock(const QImage &image, const QString &recognizedText, const QString &question);
    void stop();

signals:
    void busyChanged();
    void judgeFinished(const llocr::DecisionResult &result);
    void statusRequested(const StatusMessage &message);

private:
    void setBusy(bool busy);

    RuntimeController &m_runtime;
    std::unique_ptr<DecisionModel> m_model;
    QFutureWatcher<DecisionResult> m_watcher;
    bool m_busy = false;
    bool m_stopRequested = false;
};

}  // namespace llocr
