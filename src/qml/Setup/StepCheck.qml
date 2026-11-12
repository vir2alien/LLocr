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

    // The Finish opens on the check that ran in this visit: the phase carries
    // it, so a self-test from an earlier visit cannot open the gate.
    property bool complete: root.phase === "done"
                            && Runtime.configValid
                            && SelfTest.selftestOk
                            && !SelfTest.selftestRunning

    // idle → runtime → models → check → done | failed
    property string phase: "idle"

    // Why the last attempt failed — the key of the failure kind. The screen
    // turns it into a plain-language answer; the raw message stays in the log.
    property string failureKind: ""

    readonly property bool needsRuntime: !Runtime.serverPathValid
    readonly property bool needsModels: missingChoices.length > 0
    readonly property bool chainBusy: RuntimeInstaller.busy || ModelInstaller.busy || SelfTest.selftestRunning

    readonly property real totalDownloadBytes: {
        let total = 0;
        for (const choice of root.missingChoices)
            total += choice.size || 0;
        return total;
    }

    // The verdict of the step, on a line of its own and larger than the rest
    // of the text: what the check is doing right now, or how it ended.
    readonly property string statusText: {
        switch (root.phase) {
        case "runtime":
            return qsTr("Downloading the runtime…");
        case "models":
            return qsTr("Downloading the models…");
        case "check":
            return qsTr("Checking…");
        case "done":
            return qsTr("Check passed");
        case "failed":
            return qsTr("Check failed");
        default:
            return qsTr("The check has not run yet");
        }
    }

    readonly property color statusColor: {
        if (root.phase === "done")
            return Theme.success;
        if (root.phase === "failed")
            return Theme.error;
        if (root.chainBusy)
            return Theme.textSecondary;
        return Theme.textMuted;
    }

    // What went wrong and what the user can do about it. The kinds come from
    // the runtime (the self-test) and from the step itself (the downloads).
    readonly property var failure: root.failureDescription(root.failureKind)

    function failureDescription(kind) {
        switch (kind) {
        case "runtimeMissing":
            return {
                reason: qsTr("The llama.cpp runtime is not installed — its file is missing."),
                action: qsTr("Install it: press the button above to download the runtime, "
                             + "or pick an installed build in Settings → Runtime.")
            };
        case "modelMissing":
            return {
                reason: qsTr("The model for the recognition task is not installed — its file is missing."),
                action: qsTr("Pick another model in Settings → Models, or press the button above "
                             + "to download the missing one.")
            };
        case "runtimeDownloadFailed":
            return {
                reason: qsTr("The runtime could not be downloaded."),
                action: qsTr("Check the connection to the internet and the free disk space, "
                             + "then press the button above again.")
            };
        case "modelDownloadFailed":
            return {
                reason: qsTr("The model could not be downloaded."),
                action: qsTr("Check the connection to the internet and the free disk space, "
                             + "then press the button above again.")
            };
        case "portBusy":
            return {
                reason: qsTr("The server could not start: the port it wants is taken by another program."),
                action: qsTr("Free the port or turn on the automatic port pick in "
                             + "Settings → Runtime, then press the button above again.")
            };
        case "notEnoughMemory":
            return {
                reason: qsTr("The model did not fit into the memory of this machine."),
                action: qsTr("Pick a lighter quantization in Settings → Models, or lower the "
                             + "context size in the launch parameters, then press the button above again.")
            };
        case "backendUnavailable":
            return {
                reason: qsTr("The selected backend cannot run on this computer — the graphics "
                             + "runtime it needs is missing."),
                action: qsTr("Pick another backend on the Hardware step or in "
                             + "Settings → Runtime, then press the button above again.")
            };
        case "modelRejected":
            return {
                reason: qsTr("The server could not read the model file."),
                action: qsTr("Download the model again or pick a supported one in "
                             + "Settings → Models, then press the button above again.")
            };
        case "instanceRunning":
            return {
                reason: qsTr("Another copy of the app is already running and holds the runtime."),
                action: qsTr("Close the other copy and press the button above again.")
            };
        case "autostartOff":
            return {
                reason: qsTr("The server is not set to start on its own."),
                action: qsTr("Turn on the automatic start in Settings → Runtime, then press "
                             + "the button above again.")
            };
        case "serverStartFailed":
            return {
                reason: qsTr("The server did not start."),
                action: qsTr("Check the free memory and the launch parameters in "
                             + "Settings → Runtime, then press the button above again.")
            };
        case "noAnswer":
            return {
                reason: qsTr("The server is running but did not answer the test request in time."),
                action: qsTr("The model may still be loading — press the button above again. "
                             + "If it keeps failing, look at the server log in the main window.")
            };
        case "requestRejected":
            return {
                reason: qsTr("The server answered the test request with an error."),
                action: qsTr("Check the model and its parameters in Settings → Models, then press "
                             + "the button above again.")
            };
        default:
            return {
                reason: qsTr("The check could not be completed."),
                action: qsTr("Press the button above again. If it keeps failing, look at the "
                             + "server log in the main window.")
            };
        }
    }

    implicitHeight: contentColumn.implicitHeight + 40

    onVisibleChanged: {
        // Entering the step again starts the check afresh — a verdict of the
        // previous visit is not this visit's verdict.
        if (visible && !chainBusy) {
            phase = "idle";
            failureKind = "";
        }
    }

    function fmtBytes(bytes) {
        if (bytes <= 0)
            return "";
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toFixed(1) + " GiB";
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MiB";
        return (bytes / 1024).toFixed(1) + " KiB";
    }

    function startChain() {
        if (chainBusy)
            return;
        failureKind = "";
        if (needsRuntime) {
            // An installed runtime is reused as is (ADR 170); only its
            // absence downloads the fresh release.
            phase = "runtime";
            RuntimeInstaller.startDownloadAndInstall();
        } else {
            startModelQueue();
        }
    }

    function startModelQueue() {
        if (needsModels) {
            phase = "models";
            ModelInstaller.queueTaskInstalls(missingChoices);
        } else {
            runCheck();
        }
    }

    function runCheck() {
        phase = "check";
        SelfTest.runSelfTestQml();
    }

    Connections {
        target: RuntimeInstaller
        function onStateChanged() {
            if (phase !== "runtime")
                return;
            if (RuntimeInstaller.state === RuntimeInstaller.Installed)
                startModelQueue();
            else if (RuntimeInstaller.state === RuntimeInstaller.Error) {
                failureKind = "runtimeDownloadFailed";
                phase = "failed";
            }
        }
    }

    Connections {
        target: ModelInstaller
        function onTaskQueueStateChanged() {
            if (phase !== "models" || ModelInstaller.taskQueueActive)
                return;
            if (ModelInstaller.state === ModelInstaller.Error) {
                failureKind = "modelDownloadFailed";
                phase = "failed";
            } else {
                runCheck();
            }
        }
    }

    Connections {
        target: SelfTest
        function onSelftestFinished() {
            // The signal is also the notification of "the self-test started":
            // while it runs, there is no verdict yet.
            if (phase !== "check" || SelfTest.selftestRunning)
                return;
            if (SelfTest.selftestOk) {
                failureKind = "";
                phase = "done";
            } else {
                failureKind = SelfTest.selftestFailure.length ? SelfTest.selftestFailure : "unknown";
                phase = "failed";
            }
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 20
        spacing: Theme.spacing

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Check and launch")
            font.pixelSize: Theme.bodySize
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
            spacing: Theme.spacingSmall

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
                    text: RoleProfiles.choiceTitles(missingRow.modelData) + " — "
                          + missingRow.modelData.model + " · " + missingRow.modelData.quantId
                          + (missingRow.modelData.size > 0
                             ? " (" + root.fmtBytes(missingRow.modelData.size) + ")" : "")
                }
            }

            LLOLabel {
                visible: root.totalDownloadBytes > 0
                font.pixelSize: Theme.captionSize
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

        LLOButton {
            emphasis: true
            text: root.needsRuntime || root.needsModels ? qsTr("Download and check") : qsTr("Check")
            enabled: !root.chainBusy
            onClicked: root.startChain()
        }

        LLOLabel {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.headingSize3
            font.bold: true
            color: root.statusColor
            text: root.statusText
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.phase === "failed"
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("What went wrong")
                font.bold: true
                color: Theme.textSecondary
            }

            LLOLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.failure.reason
            }

            LLOLabel {
                Layout.topMargin: Theme.spacingSmall
                text: qsTr("What to do")
                font.bold: true
                color: Theme.textSecondary
            }

            LLOLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.failure.action
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

        Item { Layout.fillHeight: true }
    }
}
