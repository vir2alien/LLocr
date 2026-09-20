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

            LLOLabel {
                text: qsTr("Model location")
                color: Theme.textPrimary
                font.bold: true
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
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

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.preferredHeight: 1
                color: Theme.divider
            }

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
                Layout.preferredHeight: Math.min(ModelInstaller.installedCount, 3) * 40
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

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            LLOLabel {
                text: qsTr("Search Hugging Face")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    placeholderText: qsTr("e.g. vision gguf")
                    text: ModelInstaller.searchQuery
                    onEditingFinished: ModelInstaller.searchQuery = text.trim()
                }
                LLOButton {
                    text: qsTr("Search")
                    onClicked: {
                        ModelInstaller.searchQuery = searchField.text.trim()
                        ModelInstaller.startSearch()
                    }
                }
            }

            HfSearchList {
                id: searchList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(searchList.count, 3) * 32
                onInstallClicked: (index) => {
                    ModelInstaller.installRemote(index, root.isVerifyModelRole)
                    preparedIndex = -1
                    pickDialog.open()
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                visible: !ModelInstaller.searchActive
                         && ModelInstaller.searchCount === 0
                text: qsTr("Results appear here. Models install into the managed "
                           + "models directory.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                LLOLabel {
                    text: qsTr("HF token (optional)")
                }
                TextField {
                    id: tokenField
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    echoMode: TextInput.Password
                    placeholderText: qsTr("read-only token for gated repos")
                    text: ModelInstaller.hfToken()
                    onEditingFinished: ModelInstaller.setHfToken(text.trim())
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                LLOButton {
                    text: qsTr("Import catalog…")
                    onClicked: importDialog.open()
                }
                LLOButton {
                    text: qsTr("Export catalog…")
                    onClicked: exportDialog.open()
                }
                LLOButton {
                    text: qsTr("Restore defaults")
                    onClicked: ModelInstaller.resetUserCatalog(root.isVerifyModelRole)
                }
                Item { Layout.fillWidth: true }
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

    FileDialog {
        id: importDialog
        title: qsTr("Import preset catalog")
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const err = ModelInstaller.importCatalog(Runtime.localPath(selectedFile),
                                                     root.isVerifyModelRole)
            if (err.length) statusMsg.text = err
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export preset catalog")
        nameFilters: [qsTr("JSON files (*.json)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const err = ModelInstaller.exportCatalog(Runtime.localPath(selectedFile),
                                                     root.isVerifyModelRole)
            if (err.length) statusMsg.text = err
        }
    }
}