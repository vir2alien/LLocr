#pragma once

#include <functional>
#include <memory>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/CheckRequest.h"
#include "core/CheckResult.h"
#include "core/StatusMessage.h"
#include "models/GeneralPurposeModel.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

// The manual page-markup pass ("layout" role in the model profile): a full
// page image per request, the reply parsed into boxes by the caller. Resolves
// its own ConnectionRole::Layout — the layout model is configured separately
// in Settings → Layout model, and for the models that ship a layout role
// (TeleOCR) it is the same weights as block recognition, only the prompt
// differs.
class LayoutController : public QObject
{
    Q_OBJECT

public:
    explicit LayoutController(RequestProfileStore &requestProfiles, RuntimeController &runtime, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    using ImageProvider = std::function<QImage(int pageIndex)>;

    void layoutPages(const QList<int> &pages, const ImageProvider &imageProvider, const QString &systemPrompt, const QString &prompt);
    void stop();

signals:
    void busyChanged();
    void layoutFinished(int pageIndex, const QString &rawText);
    void statusRequested(const StatusMessage &message);

private:
    void startNext();
    void setBusy(bool busy);

    RequestProfileStore &m_requestProfiles;
    RuntimeController &m_runtime;
    std::unique_ptr<GeneralPurposeModel> m_model;
    QFutureWatcher<CheckResult> m_watcher;
    QList<int> m_queue;
    ImageProvider m_imageProvider;
    QString m_systemPrompt;
    QString m_prompt;
    int m_currentPage = -1;
    bool m_busy = false;
    bool m_stopRequested = false;
};

}  // namespace llocr
