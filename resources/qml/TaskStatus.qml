pragma Singleton

import QtQuick

import LLocr

QtObject {
    // One line, one task: the busiest active stage wins, the last status
    // message only shows when nothing is running. Shared by the footer and
    // the toolbar's running indicator.
    function text() {
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
        if (Controller.checkFinished)
            return qsTr("Check finished")
        return Controller.statusMessage
    }

    // The toolbar's running indicator folds the recognition stage (which has
    // no done/total counters) into the same line.
    function running() {
        return Controller.busy || Controller.importing || Controller.checkBusy
               || Controller.layoutBusy || ModelInstaller.taskQueueActive
    }
}
