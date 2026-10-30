#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "app/AutoPipelineStage.h"
#include "core/StatusMessage.h"

namespace llocr {

// The auto pipeline (ADR 257): a list of stages, run in order over the pages
// the user selected. The controller owns the order and the reporting; every
// stage owns its own work. A stage that cannot run says why and the pipeline
// skips it, so the run ends with the stages that did work rather than with
// the first missing model.
class AutoPipelineController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        // The pages the run works on. The provider expands an empty selection
        // into every page, so an empty list here means there is nothing to
        // work on.
        std::function<QList<int>()> scopePages;
        // Whether the document can be worked on now (no other job runs).
        std::function<bool()> idle;
    };

    explicit AutoPipelineController(Deps deps, QObject *parent = nullptr);

    // The stages, in the order they run. The controller does not own them; a
    // stage destroyed before the run reaches it is passed over.
    void setStages(QList<AutoPipelineStage *> stages);

    bool isRunning() const { return m_running; }
    bool canStart() const;
    QString blockedReason() const;

    int stageIndex() const { return m_stageIndex; }
    int stageCount() const { return m_stages.size(); }
    QString stageTitle() const;
    int progressDone() const;
    int progressTotal() const;

    // How long a stopped stage may take to report before the run ends
    // anyway: the stage aborts the work in flight and the run waits for it.
    // A negative value is rejected — the grace never shrinks to a tick.
    void setStopGraceMs(int milliseconds);

public slots:
    // Starts the run; returns false when it could not start.
    bool start();
    // Stops the running stage; the stages after it do not start.
    void stop();

signals:
    void stateChanged();
    void statusRequested(const llocr::StatusMessage &message);
    void finished(bool ok);

private:
    void runStage(int index);
    void onStageFinished();
    void onStageFailed(const llocr::StatusMessage &message);
    void stopCurrentStage();
    void finish(bool ok);
    void reportSummary();

    static constexpr int kStopGraceMs = 2000;

    Deps m_deps;
    QList<QPointer<AutoPipelineStage>> m_stages;
    AutoPipelineStage::Scope m_scope;
    QPointer<AutoPipelineStage> m_current;
    QTimer m_stopTimer;
    int m_stopGraceMs = kStopGraceMs;
    int m_stageIndex = -1;
    bool m_running = false;
    bool m_stopping = false;
    bool m_startingStage = false;
    QStringList m_skips;
    QStringList m_failures;
};

}  // namespace llocr
