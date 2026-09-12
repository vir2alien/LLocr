import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Settings.serverPath.trim().length > 0
                            && Settings.launchModelPath.trim().length > 0
                            && SelfTest.selftestOk
                            && !SelfTest.selftestRunning

    property real modelBytes: 0
    property real kvBytes: 0
    property real totalBytes: 0
    property real systemRamBytes: 0
    property bool hasEstimate: false
    property bool hasMemoryWarning: false

    function gib(bytes) { return bytes / (1024 * 1024 * 1024) }
    function giText(bytes) { return (bytes / (1024 * 1024 * 1024)).toFixed(1) }
    property string commandPreview: ""

    function refreshEstimate() {
        if (!Settings.launchModelPath.trim().length) {
            hasEstimate = false
            hasMemoryWarning = false
            return
        }
        // The context size comes from the active launch profile.
        var m = Runtime.estimateModelMemory(Settings.launchModelPath)
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
        function onLaunchPortChanged() { root.commandPreview = Runtime.launchCommandPreview() }
        function onLaunchHostChanged() { root.commandPreview = Runtime.launchCommandPreview() }
        function onLaunchModelAliasChanged() { root.commandPreview = Runtime.launchCommandPreview() }
    }
    Connections {
        target: LaunchProfiles
        function onProfileChanged() { refreshAll() }
        function onActiveProfileChanged() { syncPresetModel() }
    }
    Component.onCompleted: {
        refreshAll()
        syncPresetModel()
    }

    // currentIndex is assigned imperatively: a declarative binding would be
    // broken by the user's own combobox interaction.
    function syncPresetModel() {
        presetListModel.clear()
        for (let i = 0; i < LaunchProfiles.presetIds.length; ++i)
            presetListModel.append({ name: LaunchProfiles.presetNames[i] })
        const idx = LaunchProfiles.presetIds.indexOf(LaunchProfiles.activeProfileId)
        profileBox.currentIndex = idx >= 0 ? idx : 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Launch")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Tune how the local server starts, then run a quick end-to-end "
                       + "check. It starts the server, loads your model and performs "
                       + "one OCR request.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 10

            LLOLabel { text: qsTr("Port") }
            TextField {
                id: portField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                validator: IntValidator { bottom: 0; top: 65535 }
                text: Settings.launchPort
                onEditingFinished: Settings.launchPort = parseInt(text, 10) || 0
            }

            LLOLabel {
                text: qsTr("Launch profile")
            }
            ComboBox {
                id: profileBox
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                textRole: "name"
                model: ListModel { id: presetListModel }
                onActivated: LaunchProfiles.selectDraftProfile(
                                 LaunchProfiles.presetIds[currentIndex])
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            CheckBox {
                id: autoStartBox
                text: qsTr("Start the server when the app launches")
                font.pointSize: Theme.captionSize
                checked: Settings.autoStart
                onToggled: Settings.autoStart = checked
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: qsTr("Full parameter table: Settings → Launch. "
                           + "Loading the model at startup uses several GB "
                           + "of RAM/VRAM even when idle — off by default.")
            }
        }

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
                LLOLabel {
                    Layout.fillWidth: true
                    font.pointSize: Theme.captionSize
                    color: Theme.textPrimary
                    text: qsTr("Estimated memory needs ~%1 GiB (model + context) — "
                               + "this looks high for %2 GiB of RAM.")
                        .arg(root.gib(root.totalBytes).toFixed(1))
                        .arg(root.gib(root.systemRamBytes).toFixed(1))
                }
                LLOLabel {
                    Layout.fillWidth: true
                    text: qsTr("Reduce --ctx-size or --n-gpu-layers, or use a smaller model.")
                }
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
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

        LLOLabel {
            text: qsTr("Command preview")
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
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                wrapMode: TextEdit.WrapAnywhere
                background: null
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            LLOButton {
                text: qsTr("Check")
                enabled: !SelfTest.selftestRunning
                onClicked: SelfTest.runSelfTestQml()
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: SelfTest.selftestOk ? Theme.success : Theme.textMuted
                text: SelfTest.selftestRunning
                      ? qsTr("Running self-test…")
                      : (SelfTest.selftestMessage.length
                         ? (SelfTest.selftestOk
                            ? qsTr("Self-test passed: “%1”").arg(SelfTest.selftestMessage)
                            : SelfTest.selftestMessage)
                         : qsTr("Press Check to verify the full chain."))
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}