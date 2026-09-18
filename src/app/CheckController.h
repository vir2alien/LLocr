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

// Orchestrates one text-verification request: resolves the runtime connection
// (External or Managed), builds a CheckRequest from the block crop + recognized
// text + user prompt, sends it through the general-purpose model and emits the
// outcome. Request parameters are hardcoded for now (Settings UI comes later).
class CheckController : public QObject
{
    Q_OBJECT

public:
    explicit CheckController(RuntimeController &runtime,
                             QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void checkBlock(const QImage &image, const QString &recognizedText,
                    const QString &prompt);

signals:
    void busyChanged();
    void checkFinished(bool success, const QString &text, const QString &errorMessage);
    void statusRequested(const QString &message);

private:
    static QList<RequestParameter> defaultParameters();
    ConnectionConfig buildConfig(const ResolvedConnection &conn) const;
    void setBusy(bool busy);

    RuntimeController &m_runtime;
    std::unique_ptr<GeneralPurposeModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
    bool m_busy = false;
};

}  // namespace llocr