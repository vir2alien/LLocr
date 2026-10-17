#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>

#include "app/ModelRequestController.h"
#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "models/BlockRecognitionModel.h"

namespace llocr {

class RequestProfileStore;

class CheckController : public ModelRequestController
{
    Q_OBJECT

public:
    explicit CheckController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent = nullptr);

    void checkBlock(const QImage &image, const QString &systemPrompt, const QString &typePrompt);

signals:
    void checkFinished(const CheckResult &result);

private:
    void abortRequest() override;
    QList<RequestParameter> requestParameters() const;

    RequestProfileStore &m_requestProfiles;
    std::unique_ptr<BlockRecognitionModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
};

}  // namespace llocr
