pragma Singleton

import QtQuick

import LLocr

QtObject {
    // One line, one task: the busiest active stage wins, the last status
    // message only shows when nothing is running. Shared by the footer and
    // the toolbar's running indicator.
    function text() {
        if (Controller.autoPipelineRunning) {
            const stage = Controller.autoPipelineStageTitle
            const total = Controller.autoPipelineProgressTotal
            const done = Controller.autoPipelineProgressDone
            // The run starts before the first stage is assigned: the number
            // must not fall to zero in that window.
            const step = Math.max(1, Controller.autoPipelineStageIndex + 1)
            const count = Controller.autoPipelineStageCount
            // Between the stages the orchestrator has no current stage: the
            // line shows the step alone until the title arrives.
            if (stage.length === 0)
                return qsTr("Auto %1/%2").arg(step).arg(count)
            const head = qsTr("Auto %1/%2: %3").arg(step).arg(count).arg(stage)
            return total > 0 ? qsTr("%1 %2 / %3").arg(head).arg(done).arg(total) : head
        }
        if (ModelInstaller.taskQueueActive)
            return ModelInstaller.statusMessage
                   + (ModelInstaller.taskQueueRemaining > 0
                      ? " · " + qsTr("%n more in queue", "", ModelInstaller.taskQueueRemaining)
                      : "")
        if (Controller.importing)
            return qsTr("Opening %1 / %2").arg(Controller.importProgressDone)
                                        .arg(Controller.importProgressTotal)
        if (Controller.checkRunning)
            return Controller.checkDecisionPhase ? qsTr("Checking %1 / %2").arg(Controller.checkProgressDone)
                                                                          .arg(Controller.checkProgressTotal)
                                                 : Controller.checkFixPhase ? qsTr("Fixing %1 / %2").arg(Controller.checkProgressDone)
                                                                              .arg(Controller.checkProgressTotal)
                                                 : qsTr("Re-recognizing %1 / %2").arg(Controller.checkProgressDone)
                                                                                  .arg(Controller.checkProgressTotal)
        if (Controller.layoutBusy)
            return qsTr("Markup %1 / %2").arg(Controller.layoutProgressDone)
                                          .arg(Controller.layoutProgressTotal)
        // The run's line: the outcome of the last run and the caveats it
        // carried. It outranks the "check finished" line of the manual flow —
        // that line belongs to a check the user started.
        if (Controller.autoPipelineMessage.length > 0)
            return Controller.autoPipelineMessage
        if (Controller.checkFinished)
            return qsTr("Check finished")
        return Controller.statusMessage
    }

    // The footer's spinner follows the same tasks the status line names.
    function running() {
        return Controller.busy || Controller.importing || Controller.checkBusy
               || Controller.layoutBusy || ModelInstaller.taskQueueActive
               || Controller.autoPipelineRunning
    }
}
