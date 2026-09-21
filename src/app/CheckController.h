#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/ConnectionConfig.h"
#include "models/GeneralPurposeModel.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

class CheckController : public QObject
{
    Q_OBJECT

public:
    explicit CheckController(RequestProfileStore &requestProfiles,
                             RuntimeController &runtime,
                             QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void checkBlock(const QImage &image, const QString &recognizedText,
                    const QString &systemPrompt, const QString &typePrompt);
    void stop();

signals:
    void busyChanged();
    void checkFinished(const CheckResult &result);
    void statusRequested(const QString &message);

private:
    QList<RequestParameter> requestParameters() const;
    ConnectionConfig buildConfig(const ResolvedConnection &conn) const;
    void setBusy(bool busy);

    RequestProfileStore &m_requestProfiles;
    RuntimeController &m_runtime;
    std::unique_ptr<GeneralPurposeModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
    bool m_busy = false;
};

}  // namespace llocr
