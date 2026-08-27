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

    // §H.2 memory estimate (recomputed when the model or context size changes).
    property var modelBytes: 0
    property var kvBytes: 0
    property var totalBytes: 0
    property var systemRamBytes: 0
    property bool hasEstimate: false
    property bool hasMemoryWarning: false

    function gib(bytes) { return bytes / (1024 * 1024 * 1024) }
    function giText(bytes) { return (bytes / (1024 * 1024 * 1024)).toFixed(1) }
    // Command preview is built in C++ (ServerLaunchConfig::toDisplayCommand —
    // shell-escaped, capability-aware, single source of truth); we only refresh
    // it when a launch setting it depends on changes.
    property string commandPreview: ""

    function refreshEstimate() {
        if (!Settings.launchModelPath.trim().length) {
            hasEstimate = false
            hasMemoryWarning = false
            return
        }
        var m = Runtime.estimateModelMemory(Settings.launchModelPath,
                                            Settings.launchCtxSize)
        root.modelBytes = m.modelBytes
        root.kvBytes = m.kvCacheBytes
        root.totalBytes = m.totalBytes
        root.systemRamBytes = m.systemRamBytes
        root.hasEstimate = true
        root.hasMemoryWarning = m.totalBytes > m.systemRamBytes * 0.9
    }

    function refreshAll() {
        refreshEstimate()
        commandPreview = Runtime.launchCommandPreview()
    }

    Connections {
        target: Settings
        function onLaunchModelPathChanged() { refreshAll() }
        function onLaunchCtxSizeChanged() { refreshAll() }
        function onLaunchCacheTypeKChanged() { refreshAll() }
        function onLaunchCacheTypeVChanged() { refreshAll() }
        function onLaunchPortChanged() { root.commandPreview = Runtime.launchCommandPreview() }
        function onLaunchHostChanged() { root.commandPreview = Runtime.launchCommandPreview() }
        function onLaunchGpuLayersChanged() { root.commandPreview = Runtime.launchCommandPreview() }
        function onLaunchModelAliasChanged() { root.commandPreview = Runtime.launchCommandPreview() }
    }
    Component.onCompleted: refreshAll()

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

        // ----- §H.2 memory estimate + warning -----------------------------
        Rectangle {
            Layout.fillWidth: true
            visible: root.hasMemoryWarning
            implicitHeight: memoryCol.implicitHeight + Theme.spacing
            color: Theme.warningBg
            border.color: Theme.warning
            border.width: 1
            radius: Theme.controlRadius

            ColumnLayout {
                id: memoryCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textPrimary
                    text: qsTr("Estimated memory needs ~%1 GiB (model + context) — "
                               + "this looks high for %2 GiB of RAM.")
                        .arg(root.gib(root.totalBytes).toFixed(1))
                        .arg(root.gib(root.systemRamBytes).toFixed(1))
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                    text: qsTr("Reduce --ctx-size or --n-gpu-layers, or use a smaller model.")
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: qsTr("Memory estimate: ~%1 GiB total (%2 GiB model + %3 GiB KV cache) on %4 GiB RAM")
                .arg(root.giText(root.totalBytes))
                .arg(root.giText(root.modelBytes))
                .arg(root.giText(root.kvBytes))
                .arg(root.giText(root.systemRamBytes))
            visible: root.hasEstimate
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
                text: root.commandPreview
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