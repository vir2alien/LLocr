#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>

#include "app/ModelRequestController.h"
#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "models/FixModel.h"

namespace llocr {

class RequestProfileStore;

// Drives one fix request (ADR 163): resolves the fix-model connection and
// asks it to confirm, correct or reject the block's current text. The queue
// that feeds it lives in VerificationQueueController.
class FixController : public ModelRequestController
{
    Q_OBJECT

public:
    explicit FixController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent = nullptr);

    void fixBlock(const QImage &image, const QString &recognizedText, const QString &systemPrompt, const QString &typePrompt, const QString &blockType, int maxOutput);

signals:
    void fixFinished(const CheckResult &result);

private:
    void abortRequest() override;
    QList<RequestParameter> requestParameters() const;

    RequestProfileStore &m_requestProfiles;
    std::unique_ptr<FixModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
};

}  // namespace llocr
