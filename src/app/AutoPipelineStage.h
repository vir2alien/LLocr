#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <utility>

#include "core/StatusMessage.h"

namespace llocr {

// One step of the auto pipeline (ADR 257). The pipeline runs the stages in
// order and moves on when a stage reports that it is done; a stage that
// cannot run says why, and the pipeline skips it — a missing model or a
// failed stage does not stop the run.
class AutoPipelineStage : public QObject
{
    Q_OBJECT

public:
    // The pages the run works on. The provider expands an empty selection
    // into every page, so an empty list here means there is nothing to
    // work on.
    using Scope = QList<int>;

    AutoPipelineStage(QString title, QObject *parent = nullptr) : QObject(parent), m_title(std::move(title)) {}

    // The title is a translation key: the status line must follow the app's
    // language, and a language switch arrives without a restart.
    QString title() const { return StatusMessage::translate("AutoPipelineStage", m_title).text(); }

    // Empty when the stage can run; otherwise why it is skipped — no model
    // for the task, nothing to recognize. The reason reaches the status line.
    virtual QString skipReason(const Scope &pages) const = 0;

    // Starts the work over the pages. The stage reports the end of the work
    // through finished() and an early failure through failed(); after failed()
    // the stage must not emit finished().
    virtual void start(const Scope &pages) = 0;
    virtual void stop() = 0;

    virtual int progressDone() const { return 0; }
    virtual int progressTotal() const { return 0; }

signals:
    void finished();
    void failed(const StatusMessage &message);

private:
    QString m_title;
};

}  // namespace llocr
