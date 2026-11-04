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
class SettingsStore;

// Drives one fix request (ADR 163): resolves the fix-model connection and
// asks it to confirm, correct or reject the block's current text. The queue
// that feeds it is RemedyQueue.
//
// The read protocol (ADR 259) is the same request with another contract: the
// model writes the block from the image and the recognized text travels only
// as a sample of the document's notation.
class FixController : public ModelRequestController
{
    Q_OBJECT

public:
    explicit FixController(RequestProfileStore &requestProfiles, SettingsStore &settings, RuntimeController &runtime, QObject *parent = nullptr);

    void fixBlock(const QImage &image,
                  const QString &recognizedText,
                  const QString &systemPrompt,
                  const QString &typePrompt,
                  const QString &blockType,
                  int maxOutput,
                  CheckRequest::FixProtocol protocol = CheckRequest::FixProtocol::FullBlock);

signals:
    void fixFinished(const CheckResult &result);

private:
    void abortRequest() override;
    QList<RequestParameter> requestParameters() const;

    RequestProfileStore &m_requestProfiles;
    SettingsStore &m_settings;
    std::unique_ptr<FixModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
    StatusMessage m_budgetNote;  // the thinking was cut to fit the context
};

// The role is looked up by the request profile's id: on Managed the request
// carries the server alias, which all fix models share (ADR 268).
void applyReasoningBudget(QList<RequestParameter> &parameters, CheckRequest::FixProtocol protocol, const RequestProfileStore &requestProfiles, const SettingsStore &settings);

}  // namespace llocr
