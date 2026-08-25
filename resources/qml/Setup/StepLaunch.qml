import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// SetupWizard → Step 4 "Launch": tune the managed-server launch parameters and
// verify the whole chain with the "Check" button (Runtime.runSelfTestQml).
// The step is complete once the self-test succeeded.
Item {
    id: root

    property bool complete: Settings.serverPath.trim().length > 0
                            && Settings.launchModelPath.trim().length > 0
                            && Runtime.selftestOk
                            && !Runtime.selftestRunning

    function fmtCommand() {
        var parts = []
        parts.push(Settings.serverPath.trim())
        if (Settings.launchModelPath.trim().length)
            parts.push("--model " + Settings.launchModelPath.trim())
        parts.push("--host " + Settings.launchHost)
        if (Settings.launchPort > 0)
            parts.push("--port " + Settings.launchPort)
        if (Settings.launchCtxSize > 0)
            parts.push("--ctx-size " + Settings.launchCtxSize)
        if (Settings.launchGpuLayers >= 0)
            parts.push("--n-gpu-layers " + Settings.launchGpuLayers)
        parts.push("--alias " + Settings.launchModelAlias)
        return parts.join("  ")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Launch")
            font.pixelSize: Theme.fontTitle
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textSecondary
            text: qsTr("Tune how the local server starts, then run a quick end-to-end "
                       + "check. It starts the server, loads your model and performs "
                       + "one OCR request.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 10

            Label { text: qsTr("Port"); font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
            TextField {
                id: portField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                validator: IntValidator { bottom: 0; top: 65535 }
                text: Settings.launchPort
                onEditingFinished: Settings.launchPort = parseInt(text, 10) || 0
            }

            Label { text: qsTr("Context size"); font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
            TextField {
                id: ctxField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                validator: IntValidator { bottom: 256; top: 131072 }
                text: Settings.launchCtxSize
                onEditingFinished: Settings.launchCtxSize = parseInt(text, 10) || 0
            }

            Label { text: qsTr("GPU layers"); font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
            TextField {
                id: gpuField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                validator: IntValidator { bottom: -1; top: 200 }
                text: Settings.launchGpuLayers
                onEditingFinished: Settings.launchGpuLayers = parseInt(text, 10) || 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            CheckBox {
                id: autoStartBox
                text: qsTr("Start the server when the app launches")
                font.pixelSize: Theme.fontCaption
                checked: Settings.autoStart
                onToggled: Settings.autoStart = checked
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
                // §4.3: autoStart is OFF by default; the wizard is the only place
                // that proposes it, with the model-size warning below.
                text: qsTr("Loading the model at startup uses several GB of RAM/VRAM "
                           + "even when idle — off by default.")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Label {
            text: qsTr("Command preview")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.surfaceSunken
            radius: Theme.radius
            border.color: Theme.border

            TextArea {
                anchors.fill: parent
                anchors.margins: 6
                readOnly: true
                text: root.fmtCommand()
                font.family: "monospace"
                font.pixelSize: Theme.fontSmall
                color: Theme.textPrimary
                wrapMode: TextEdit.WrapAnywhere
                background: null
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Check")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: !Runtime.selftestRunning
                onClicked: Runtime.runSelfTestQml()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Runtime.selftestOk ? Theme.success : Theme.textMuted
                text: Runtime.selftestRunning
                      ? qsTr("Running self-test…")
                      : (Runtime.selftestMessage.length
                         ? (Runtime.selftestOk
                            ? qsTr("Self-test passed: “%1”").arg(Runtime.selftestMessage)
                            : Runtime.selftestMessage)
                         : qsTr("Press Check to verify the full chain."))
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}