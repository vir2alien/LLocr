pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The wizard's last managed step (ADR 170): one button downloads the missing
// runtime and model quantizations, then the self-test verifies the whole
// chain; its success is what unlocks the wizard's Finish.
Item {
    id: root

    // What the hardware step's applyChoices() could not activate: quantizations
    // still to download, {task, key, quantId, model, size} each.
    property var missingChoices: []

    property bool complete: Runtime.configValid
                            && SelfTest.selftestOk
                            && !SelfTest.selftestRunning

    // idle → runtime → models → check → done | failed
    property string phase: "idle"

    readonly property bool needsRuntime: !Runtime.serverPathValid
    readonly property bool needsModels: missingChoices.length > 0
    readonly property bool chainBusy: RuntimeInstaller.busy || ModelInstaller.busy || SelfTest.selftestRunning

    readonly property real totalDownloadBytes: {
        let total = 0
        for (const choice of root.missingChoices)
            total += choice.size || 0
        return total
    }

    implicitHeight: contentColumn.implicitHeight + 40

    function fmtBytes(bytes) {
        if (bytes <= 0)
            return ""
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    function startChain() {
        if (chainBusy)
            return
        if (needsRuntime) {
            // An installed runtime is reused as is (ADR 170); only its
            // absence downloads the fresh release.
            phase = "runtime"
            RuntimeInstaller.startDownloadAndInstall()
        } else {
            startModelQueue()
        }
    }

    function startModelQueue() {
        if (needsModels) {
            phase = "models"
            ModelInstaller.queueTaskInstalls(missingChoices)
        } else {
            runCheck()
        }
    }

    function runCheck() {
        phase = "check"
        SelfTest.runSelfTestQml()
    }

    Connections {
        target: RuntimeInstaller
        function onStateChanged() {
            if (phase !== "runtime")
                return
            if (RuntimeInstaller.state === RuntimeInstaller.Installed)
                startModelQueue()
            else if (RuntimeInstaller.state === RuntimeInstaller.Error)
                phase = "failed"
        }
    }

    Connections {
        target: ModelInstaller
        function onTaskQueueStateChanged() {
            if (phase !== "models" || ModelInstaller.taskQueueActive)
                return
            if (ModelInstaller.state === ModelInstaller.Error)
                phase = "failed"
            else
                runCheck()
        }
    }

    Connections {
        target: SelfTest
        function onSelftestFinished() {
            if (phase !== "check")
                return
            phase = SelfTest.selftestOk ? "done" : "failed"
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Check and launch")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("The button downloads everything that is still missing — "
                       + "the runtime and the models — then runs an end-to-end "
                       + "check: the server starts, the model loads, one OCR "
                       + "request is performed.")
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.needsRuntime || root.needsModels
            spacing: 4

            LLOLabel {
                text: qsTr("To download")
                font.bold: true
            }

            LLOLabel {
                visible: root.needsRuntime
                Layout.fillWidth: true
                text: qsTr("llama.cpp runtime — %1 backend")
                      .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.backend))
            }

            Repeater {
                model: root.missingChoices

                delegate: LLOLabel {
                    id: missingRow

                    required property var modelData

                    Layout.fillWidth: true
                    text: RoleProfiles.title(missingRow.modelData.task) + " — "
                          + missingRow.modelData.model + " · " + missingRow.modelData.quantId
                          + (missingRow.modelData.size > 0
                             ? " (" + root.fmtBytes(missingRow.modelData.size) + ")" : "")
                }
            }

            LLOLabel {
                visible: root.totalDownloadBytes > 0
                font.pointSize: Theme.captionSize
                color: Theme.textSecondary
                text: qsTr("Models total: %1").arg(root.fmtBytes(root.totalDownloadBytes))
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: !root.needsRuntime && !root.needsModels
            color: Theme.textSecondary
            text: qsTr("Everything is already installed — only the check remains.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOButton {
                emphasis: true
                text: root.needsRuntime || root.needsModels ? qsTr("Download and check") : qsTr("Check")
                enabled: !root.chainBusy
                onClicked: root.startChain()
            }

            LLOLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: SelfTest.selftestOk ? Theme.success
                     : SelfTest.selftestRunning ? Theme.textSecondary
                     : Theme.textMuted
                text: SelfTest.selftestRunning
                      ? qsTr("Running self-test…")
                      : (SelfTest.selftestMessage.length
                         ? (SelfTest.selftestOk
                            ? qsTr("Self-test passed: “%1”").arg(SelfTest.selftestMessage)
                            : SelfTest.selftestMessage)
                         : (root.phase === "done"
                            ? qsTr("All set.")
                            : qsTr("The check starts the server and performs one OCR request.")))
            }
        }

        InstallerStatusLabel {
            Layout.fillWidth: true
            visible: root.phase === "runtime" && RuntimeInstaller.busy
            isError: RuntimeInstaller.state === RuntimeInstaller.Error
            busy: RuntimeInstaller.busy
            statusText: RuntimeInstaller.statusMessage
        }
        InstallerProgressRow {
            Layout.fillWidth: true
            visible: root.phase === "runtime" && RuntimeInstaller.busy
            busy: RuntimeInstaller.busy
            progress: RuntimeInstaller.progress
            cancelVisible: true
            onCancelClicked: RuntimeInstaller.cancelInstall()
        }

        InstallerStatusLabel {
            Layout.fillWidth: true
            visible: root.phase === "models" && ModelInstaller.busy
            isError: ModelInstaller.state === ModelInstaller.Error
            busy: ModelInstaller.busy
            statusText: ModelInstaller.statusMessage
        }
        InstallerProgressRow {
            Layout.fillWidth: true
            visible: root.phase === "models" && ModelInstaller.busy
            busy: ModelInstaller.busy
            progress: ModelInstaller.progress
            cancelVisible: true
            onCancelClicked: ModelInstaller.cancelTaskInstalls()
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.phase === "failed"
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.error
            text: qsTr("Something failed — see the message above, fix the cause and press the button again.")
        }

        Item { Layout.fillHeight: true }
    }
}
