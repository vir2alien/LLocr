pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // false = download models via the preset catalog, true = pick local files.
    property bool downloadMode: true
    property int preparedIndex: -1
    property bool preparedForCheck: false

    readonly property bool downloadComplete: Settings.launchModelPath.trim().length > 0
    readonly property bool pathComplete: Settings.launchModelPath.trim().length > 0
    property bool complete: downloadMode ? downloadComplete : pathComplete

    onVisibleChanged: {
        if (!visible)
            return
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
    }

    Component.onCompleted: {
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
    }

    Connections {
        target: ModelInstaller
        function onStateChanged() {
            if (ModelInstaller.state !== ModelInstaller.ReadyToDownload
                    || !pickDialog.visible)
                return
            const count = root.preparedForCheck ? ModelInstaller.checkPresetCount
                                                : ModelInstaller.presetCount
            if (root.preparedIndex >= 0 && root.preparedIndex < count)
                pickDialog.license = ModelInstaller.presetInfo(
                            root.preparedIndex, root.preparedForCheck).license
            else
                pickDialog.license = ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Models")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            RadioButton {
                id: downloadRadio
                text: qsTr("Download models")
                checked: true
                onToggled: root.downloadMode = true
            }
            RadioButton {
                id: pathRadio
                text: qsTr("Specify model files")
                onToggled: root.downloadMode = false
            }
            Item { Layout.fillWidth: true }
        }

        // --- Download models: OCR / check tabs ----------------------------
        ColumnLayout {
            visible: root.downloadMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            TabBar {
                id: modelTabBar
                Layout.fillWidth: true
                TabButton { text: qsTr("OCR model") }
                TabButton { text: qsTr("Check model") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: modelTabBar.currentIndex

                Repeater {
                    model: 2

                    delegate: ScrollView {
                        id: rolePane
                        required property int index
                        readonly property bool forCheck: index === 1

                        contentWidth: availableWidth
                        contentHeight: paneLayout.implicitHeight
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        clip: true

                        ColumnLayout {
                            id: paneLayout
                            width: rolePane.width
                            spacing: 6

                            InstallerStatusLabel {
                                isError: ModelInstaller.state === ModelInstaller.Error
                                busy: ModelInstaller.busy
                                statusText: ModelInstaller.statusMessage.length
                                      ? ModelInstaller.statusMessage
                                      : qsTr("Pick a preset to download, or activate "
                                             + "an installed model.")
                            }

                            InstallerProgressRow {
                                Layout.fillWidth: true
                                busy: ModelInstaller.busy
                                progress: ModelInstaller.progress
                                cancelVisible: ModelInstaller.state === ModelInstaller.Downloading
                                onCancelClicked: ModelInstaller.cancelInstall()
                            }

                            LLOLabel {
                                visible: installedList.count === 0
                                text: qsTr("No models installed")
                                color: Theme.textPrimary
                            }

                            ModelInstalledList {
                                id: installedList
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(installedList.count, 3) * 34
                                rowHeight: 34
                                managementActions: true
                                isVerifyModelRole: rolePane.forCheck
                                onActionError: (msg) => statusLabel.text = msg
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                Layout.preferredHeight: 1
                                color: Theme.divider
                            }

                            LLOLabel {
                                text: qsTr("Preset catalog")
                            }

                            ModelPresetList {
                                id: presetList
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(presetList.count, 3) * 36
                                rowHeight: 36
                                isVerifyModelRole: rolePane.forCheck
                                onInstallClicked: (index) => {
                                    ModelInstaller.preparePreset(index, rolePane.forCheck)
                                    root.preparedIndex = index
                                    root.preparedForCheck = rolePane.forCheck
                                    pickDialog.open()
                                }
                            }

                            LLOLabel {
                                id: statusLabel
                                Layout.fillWidth: true
                                visible: text.length > 0
                                color: Theme.textSecondary
                                font.pointSize: Theme.captionSize
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                            }
                        }
                    }
                }
            }//StackLayout
        }//ColumnLayout

        // --- Specify model files: all paths in one view -------------------
        ColumnLayout {
            visible: !root.downloadMode
            Layout.fillWidth: true
            spacing: 6

            LLOLabel {
                text: qsTr("Path to the OCR model")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    placeholderText: qsTr("path to the .gguf model file")
                    text: Settings.launchModelPath
                    onEditingFinished: Settings.launchModelPath = text.trim()
                }
                LLOButton {
                    text: qsTr("Browse…")
                    onClicked: { picker.target = 0; picker.open() }
                }
            }

            LLOLabel {
                text: qsTr("OCR multimodal module (mmproj)")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    placeholderText: qsTr("optional mmproj file for vision models")
                    text: Settings.launchMmprojPath
                    onEditingFinished: Settings.launchMmprojPath = text.trim()
                }
                LLOButton {
                    text: qsTr("Browse…")
                    onClicked: { picker.target = 1; picker.open() }
                }
            }

            LLOLabel {
                text: qsTr("Path to the check model")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    placeholderText: qsTr("optional small general-purpose model")
                    text: Settings.checkLaunchModelPath
                    onEditingFinished: Settings.checkLaunchModelPath = text.trim()
                }
                LLOButton {
                    text: qsTr("Browse…")
                    onClicked: { picker.target = 2; picker.open() }
                }
            }

            LLOLabel {
                text: qsTr("Check multimodal module (mmproj)")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    placeholderText: qsTr("optional mmproj file for vision models")
                    text: Settings.checkLaunchMmprojPath
                    onEditingFinished: Settings.checkLaunchMmprojPath = text.trim()
                }
                LLOButton {
                    text: qsTr("Browse…")
                    onClicked: { picker.target = 3; picker.open() }
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.helpColor
                text: qsTr("The check model is optional — text verification can be "
                           + "configured later in Settings.")
            }
        }

        Item { Layout.fillHeight: true }
    }//ColumnLayout

    Dialog {
        id: pickDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Install model")
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string license: ""

        ColumnLayout {
            width: parent.width
            spacing: 6
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textSecondary
                text: qsTr("Downloading starts after confirmation. The model license "
                           + "applies — review it before installing.")
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.accent
                visible: pickDialog.license.length > 0
                text: {
                    var lic = pickDialog.license
                    if (/^https?:\/\//.test(lic))
                        return qsTr("License: %1")
                            .arg("<a href=\"" + lic + "\">License</a>")
                    return qsTr("License: %1").arg(lic)
                }
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        onAccepted: {
            ModelInstaller.installPrepared()
        }
    }

    FileDialog {
        id: picker
        property int target: 0
        title: qsTr("Select a model file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GGUF models (*.gguf)"), qsTr("All files (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            if (picker.target === 0)
                Settings.launchModelPath = path
            else if (picker.target === 1)
                Settings.launchMmprojPath = path
            else if (picker.target === 2)
                Settings.checkLaunchModelPath = path
            else
                Settings.checkLaunchMmprojPath = path
        }
    }
}
