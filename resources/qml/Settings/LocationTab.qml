pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root
    property int preparedIndex: -1
    property bool isVerifyModelRole: false
    // Opened via the "Configure runtime" button in External mode.
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property string modelPathText: isVerifyModelRole
        ? Settings.checkLaunchModelPath : Settings.launchModelPath
    readonly property string mmprojPathText: isVerifyModelRole
        ? Settings.checkLaunchMmprojPath : Settings.launchMmprojPath
    readonly property string activeTitleText: isVerifyModelRole
        ? ModelInstaller.checkActiveTitle : ModelInstaller.activeTitle

    Connections {
        target: Settings
        function onLaunchModelPathChanged() {
            if (!root.isVerifyModelRole && !modelPathField.activeFocus)
                modelPathField.text = Settings.launchModelPath
        }
        function onCheckLaunchModelPathChanged() {
            if (root.isVerifyModelRole && !modelPathField.activeFocus)
                modelPathField.text = Settings.checkLaunchModelPath
        }
        function onLaunchMmprojPathChanged() {
            if (!root.isVerifyModelRole && !mmprojPathField.activeFocus)
                mmprojPathField.text = Settings.launchMmprojPath
        }
        function onCheckLaunchMmprojPathChanged() {
            if (root.isVerifyModelRole && !mmprojPathField.activeFocus)
                mmprojPathField.text = Settings.checkLaunchMmprojPath
        }
    }

    Connections {
        target: ModelInstaller
        function onStateChanged() {
            if (ModelInstaller.state === ModelInstaller.ReadyToDownload && pickDialog.visible) {
                const idx = preparedIndex
                const count = root.isVerifyModelRole ? ModelInstaller.checkPresetCount
                                                    : ModelInstaller.presetCount
                if (idx >= 0 && idx < count)
                    pickDialog.license = ModelInstaller.presetInfo(idx,
                                                root.isVerifyModelRole).license
                else
                    pickDialog.license = ""
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: modelsLayout.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: modelsLayout
            width: parent.width
            spacing: 6

            // --- External server: the model is not managed here ----------
            ColumnLayout {
                visible: root.externalMode
                Layout.fillWidth: true
                spacing: 6

                LLOLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.helpColor
                    text: qsTr("The model is managed by the external server. "
                               + "Location and download settings are not "
                               + "available in this mode.")
                }

                LLOButton {
                    text: qsTr("Configure runtime…")
                    onClicked: {
                        if (root.runtimeSettingsRef)
                            root.runtimeSettingsRef.show()
                    }
                }
            }

            // --- Managed runtime -----------------------------------------
            ColumnLayout {
                visible: !root.externalMode
                Layout.fillWidth: true
                spacing: 6

                ComboBox {
                    id: sourceBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    textRole: "text"
                    model: [
                        { text: qsTr("Specify model files") },
                        { text: qsTr("Download model") }
                    ]
                    currentIndex: 0
                }

                // -- Specify model files -------------------------------
                ColumnLayout {
                    visible: sourceBox.currentIndex === 0
                    Layout.fillWidth: true
                    spacing: 6

                    LLOLabel {
                        Layout.fillWidth: true
                        font.pointSize: Theme.captionSize
                        color: Theme.helpColor
                        text: root.isVerifyModelRole
                              ? qsTr("These paths select the model for text verification. "
                                     + "Activating a downloaded model fills them automatically.")
                              : qsTr("These paths are used when the managed llama-server "
                                     + "is launched. Activating a downloaded model fills "
                                     + "them automatically.")
                    }

                    LLOLabel {
                        text: qsTr("Path to the main model")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: modelPathField
                            Layout.fillWidth: true
                            implicitHeight: Theme.controlHeight
                            selectByMouse: true
                            placeholderText: qsTr("path to the .gguf model file")
                            text: root.modelPathText
                            onEditingFinished: {
                                if (root.isVerifyModelRole)
                                    Settings.checkLaunchModelPath = text.trim()
                                else
                                    Settings.launchModelPath = text.trim()
                            }
                        }
                        LLOButton {
                            text: qsTr("Browse…")
                            onClicked: modelPicker.open()
                        }
                    }

                    LLOLabel {
                        text: qsTr("Path to the multimodal module (mmproj)")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: mmprojPathField
                            Layout.fillWidth: true
                            implicitHeight: Theme.controlHeight
                            selectByMouse: true
                            placeholderText: qsTr("optional mmproj file for vision models")
                            text: root.mmprojPathText
                            onEditingFinished: {
                                if (root.isVerifyModelRole)
                                    Settings.checkLaunchMmprojPath = text.trim()
                                else
                                    Settings.launchMmprojPath = text.trim()
                            }
                        }
                        LLOButton {
                            text: qsTr("Browse…")
                            onClicked: mmprojPicker.open()
                        }
                    }

                    LLOLabel {
                        Layout.fillWidth: true
                        visible: root.activeTitleText.length > 0
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: Theme.textSecondary
                        text: qsTr("Activated: %1").arg(root.activeTitleText)
                    }
                    LLOLabel {
                        Layout.fillWidth: true
                        visible: root.modelPathText.length > 0
                                 && root.activeTitleText.length === 0
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: Theme.textMuted
                        text: root.isVerifyModelRole
                              ? qsTr("A model outside the app registry — used for "
                                     + "text verification.")
                              : qsTr("A model outside the app registry — used as-is for "
                                     + "the managed launch.")
                    }
                }

                // -- Download model ------------------------------------
                ColumnLayout {
                    visible: sourceBox.currentIndex === 1
                    Layout.fillWidth: true
                    spacing: 6

                    InstallerStatusLabel {
                        isError: ModelInstaller.state === ModelInstaller.Error
                        busy: ModelInstaller.busy
                        statusText: ModelInstaller.statusMessage.length
                              ? ModelInstaller.statusMessage
                              : qsTr("Models are stored locally and launched by the managed runtime.")
                    }

                    InstallerProgressRow {
                        Layout.fillWidth: true
                        busy: ModelInstaller.busy
                        progress: ModelInstaller.progress
                        cancelVisible: ModelInstaller.state === ModelInstaller.Downloading
                        onCancelClicked: ModelInstaller.cancelInstall()
                    }

                    LLOLabel {
                        text: qsTr("Installed models: ")
                    }

                    ModelInstalledList {
                        id: installedList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(installedList.count, 3) * 40
                        managementActions: true
                        isVerifyModelRole: root.isVerifyModelRole
                        onActionError: (msg) => statusMsg.text = msg
                    }//ListView

                    LLOLabel {
                        visible: installedList.count === 0
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: Theme.textPrimary
                        text: qsTr("No models installed")
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
                        isVerifyModelRole: root.isVerifyModelRole
                        onInstallClicked: (index) => {
                            ModelInstaller.preparePreset(index, root.isVerifyModelRole)
                            preparedIndex = index
                            pickDialog.open()
                        }
                    }//ListView

                    LLOLabel {
                        visible: presetList.count === 0
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: Theme.textPrimary
                        text: qsTr("No presets available")
                    }

                    LLOLabel {
                        id: statusMsg
                        Layout.fillWidth: true
                        visible: text.length > 0
                        color: Theme.textSecondary
                        font.pointSize: Theme.captionSize
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                }
            }
        }//ColumnLayout
    }//ScrollView

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
                textFormat: Text.RichText
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
        id: modelPicker
        title: qsTr("Select a model file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GGUF models (*.gguf)"), qsTr("All files (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            if (root.isVerifyModelRole)
                Settings.checkLaunchModelPath = path
            else
                Settings.launchModelPath = path
        }
    }

    FileDialog {
        id: mmprojPicker
        title: qsTr("Select an mmproj file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GGUF models (*.gguf)"), qsTr("All files (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            if (root.isVerifyModelRole)
                Settings.checkLaunchMmprojPath = path
            else
                Settings.launchMmprojPath = path
        }
    }

}
