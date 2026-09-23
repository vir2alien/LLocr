pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Settings.launchModelPath.trim().length > 0

    property real estTotal: 0
    property real estRam: 0
    function gib(bytes) { return bytes / (1024 * 1024 * 1024) }
    function refreshEstimate() {
        if (!Settings.launchModelPath.trim().length) { estTotal = 0; estRam = 0; return }
        var m = Runtime.estimateModelMemory(Settings.launchModelPath)
        root.estTotal = m.totalBytes
        root.estRam = m.systemRamBytes
    }
    Connections {
        target: Settings
        function onLaunchModelPathChanged() { refreshEstimate() }
    }
    Connections {
        target: LaunchProfilesOcr
        function onProfileChanged() { refreshEstimate() }
    }
    onVisibleChanged: {
        if (!visible)
            return
        refreshEstimate()
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Model")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Vision-capable GGUF models work with the managed server. Pick a "
                       + "preset or point at a local file.")
        }

        InstallerStatusLabel {
            isError: ModelInstaller.state === ModelInstaller.Error
            busy: ModelInstaller.busy
            statusText: ModelInstaller.statusMessage.length
                  ? ModelInstaller.statusMessage
                  : (root.complete
                     ? qsTr("Model selected: %1").arg(Settings.launchModelPath)
                     : qsTr("No model selected yet."))
        }

        LLOLabel {
            visible: installedList.count > 0
            text: qsTr("Installed models")
            color: Theme.textPrimary
        }

        ModelInstalledList {
            id: installedList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(installedList.count, 3) * 34
            rowHeight: 34
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            visible: root.complete && root.estTotal > 0
            text: qsTr("Estimated footprint: ~%1 GiB (model + context) on %2 GiB RAM")
                .arg(root.gib(root.estTotal).toFixed(1))
                .arg(root.gib(root.estRam).toFixed(1))
        }

        InstallerProgressRow {
            Layout.fillWidth: true
            Layout.fillHeight: false
            busy: ModelInstaller.busy
            progress: ModelInstaller.progress
            cancelVisible: ModelInstaller.state === ModelInstaller.Downloading
            onCancelClicked: ModelInstaller.cancelInstall()
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 6
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                TabBar {
                    id: modelTabBar
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Presets") }
                    TabButton { text: qsTr("Local file") }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: modelTabBar.currentIndex

                    ColumnLayout {//Presets
                        spacing: 6
                        LLOLabel {
                            text: qsTr("Start from a preset")
                        }
                        ModelPresetList {
                            id: presetList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rowHeight: 34
                            onInstallClicked: (index) => {
                                prepareDialog.pendingIndex = index
                                ModelInstaller.preparePreset(index)
                                prepareDialog.open()
                            }
                        }
                    }//ColumnLayout

                    ColumnLayout {//Local file
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TextField {
                                id: localPathField
                                Layout.fillWidth: true
                                implicitHeight: Theme.controlHeight
                                placeholderText: qsTr("path to a .gguf model")
                                text: Settings.launchModelPath
                            }
                            LLOButton {
                                text: qsTr("Browse…")
                                onClicked: modelPicker.open()
                            }
                        }
                        LLOButton {
                            text: qsTr("Use this file")
                            enabled: localPathField.text.trim().length > 0
                            onClicked: Settings.launchModelPath = localPathField.text.trim()
                        }
                        LLOLabel {
                            Layout.fillWidth: true
                            font.pointSize: Theme.captionSize
                            color: Theme.helpColor
                            text: qsTr("The local model is not managed: its license is your "
                                       + "responsibility, and it is not verified by the catalog.")
                        }
                    }//ColumnLayout
                }
            }
        }//Frame

        Item { Layout.fillHeight: true }
    }//ColumnLayout

    Dialog {
        id: prepareDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Install model")
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string license: ""
        property int pendingIndex: -1

        ColumnLayout {
            spacing: 6
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                text: qsTr("Review the license before installing. Downloading starts "
                           + "after confirmation.")
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.accent
                visible: prepareDialog.license.length > 0
                text: {
                    var lic = prepareDialog.license
                    if (/^https?:\/\//.test(lic))
                        return qsTr("License: %1")
                            .arg("<a href=\"" + lic + "\">License</a>")
                    return qsTr("License: %1").arg(lic)
                }
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
        onOpened: {
            if (prepareDialog.pendingIndex >= 0
                && prepareDialog.pendingIndex < ModelInstaller.presetCount) {
                prepareDialog.license =
                    ModelInstaller.presetInfo(prepareDialog.pendingIndex).license || ""
            } else {
                prepareDialog.license = ""
            }
        }
        onAccepted: ModelInstaller.installPrepared()
    }//Dialog

    FileDialog {
        id: modelPicker
        title: qsTr("Select a GGUF model")
        nameFilters: [qsTr("GGUF models (*.gguf)"), qsTr("All files (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            Settings.launchModelPath = path
        }
    }
}