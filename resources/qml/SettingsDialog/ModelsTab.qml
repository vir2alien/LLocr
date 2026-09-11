import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root
    property int preparedIndex: -1

    function fmtBytes(bytes) {
        if (bytes <= 0) return ""
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    Connections {
        target: ModelInstaller
        function onStateChanged() {
            if (ModelInstaller.state === 2 && pickDialog.visible) {
                const idx = preparedIndex
                if (idx >= 0 && idx < ModelInstaller.presetCount)
                    pickDialog.license = ModelInstaller.presetInfo(idx).license
                else
                    pickDialog.license = ""
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: ModelInstaller.state === 4 ? Theme.error
                 : (ModelInstaller.busy ? Theme.textSecondary : Theme.textMuted)
            text: ModelInstaller.statusMessage.length
                  ? ModelInstaller.statusMessage
                  : qsTr("Models are stored locally and launched by the managed runtime.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: 6

            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
                visible: ModelInstaller.busy
                from: 0
                to: 1
                value: ModelInstaller.progress
            }

            LLOButton {
                text: qsTr("Cancel")
                visible: ModelInstaller.state === 3
                onClicked: ModelInstaller.cancelInstall()
            }
        }

        Label {
            text: qsTr("Installed models: ")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        ListView {
            id: installedList
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(installedList.count, 3) * 40
            clip: true
            model: ModelInstaller.installedCount
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                property var info: ModelInstaller.installedInfo(index)
                Connections {
                    target: ModelInstaller
                    function onInstalledChanged() {
                        delegateRoot.info = Qt.binding(function () {
                            return ModelInstaller.installedInfo(delegateRoot.index)
                        })
                    }
                }
                id: delegateRoot
                width: installedList.width
                height: 40
                color: info.active ? Theme.surfaceSunken : "transparent"
                border.color: info.active ? Theme.accent : "transparent"
                border.width: info.active ? 1 : 0
                radius: Theme.radius

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textPrimary
                        text: info.title
                    }
                    Label {
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        text: fmtBytes(info.size)
                    }
                    Label {
                        Layout.preferredWidth: 80
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: info.origin === "managed" ? Theme.textSecondary : Theme.textMuted
                        text: info.origin === "managed" ? qsTr("managed") : qsTr("external")
                    }
                    LLOButton {
                        text: info.active ? qsTr("Active") : qsTr("Activate")
                        enabled: !info.active
                        onClicked: ModelInstaller.setActiveModel(index)
                    }
                    LLOButton {
                        text: qsTr("Remove")
                        enabled: info.origin === "managed"
                        onClicked: {
                            const err = ModelInstaller.removeModel(index)
                            if (err.length)
                                statusMsg.text = err
                        }
                    }
                    LLOButton {
                        text: qsTr("Open folder")
                        onClicked: {
                            const err = ModelInstaller.openModelFolder(index)
                            if (err.length)
                                statusMsg.text = err
                        }
                    }
                }
            }
        }//ListView

        Label {
            visible: installedList.count === 0
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            font.pixelSize: Theme.fontSmall
            color: Theme.textPrimary
            text: qsTr("No models installed")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Label {
            text: qsTr("Preset catalog")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        ListView {
            id: presetList
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(presetList.count, 3) * 36
            clip: true
            model: ModelInstaller.presetCount
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                property var pInfo: ModelInstaller.presetInfo(index)
                Connections {
                    target: ModelInstaller
                    function onInstalledChanged() {
                        presetRoot.pInfo = Qt.binding(function () {
                            return ModelInstaller.presetInfo(presetRoot.index)
                        })
                    }
                }
                id: presetRoot
                width: presetList.width
                height: 36
                color: "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6
                    Label {
                        Layout.preferredWidth: 160
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textPrimary
                        text: pInfo.title
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        text: pInfo.repo
                    }
                    Label {
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        text: pInfo.approxVramGb > 0
                              ? qsTr("~%1 GiB VRAM").arg(pInfo.approxVramGb)
                              : ""
                    }
                    LLOButton {
                        text: qsTr("Install")
                        enabled: !ModelInstaller.busy && !pInfo.installed
                        onClicked: {
                            ModelInstaller.preparePreset(index)
                            preparedIndex = index
                            pickDialog.open()
                        }
                    }
                }
            }
        }//ListView

        Label {
            visible: presetList.count === 0
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            font.pixelSize: Theme.fontSmall
            color: Theme.textPrimary
            text: qsTr("No presets available")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Label {
            text: qsTr("Search Hugging Face")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
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

        ListView {
            id: searchList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(searchList.count, 3) * 32
            clip: true
            visible: ModelInstaller.searchCount > 0
            model: ModelInstaller.searchCount
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property int index
                property var sInfo: ModelInstaller.searchResult(index)
                width: searchList.width
                height: 32
                color: "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6
                    Label {
                        Layout.preferredWidth: 180
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textPrimary
                        text: sInfo.title
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        text: sInfo.id
                    }
                    LLOButton {
                        text: qsTr("Install")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontSmall
                        enabled: !ModelInstaller.busy
                        onClicked: {
                            ModelInstaller.installRemote(index)
                            preparedIndex = -1
                            pickDialog.open()
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            visible: !ModelInstaller.searchActive
                     && ModelInstaller.searchCount === 0
            text: qsTr("Results appear here. Models install into the managed "
                       + "models directory.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("HF token (optional)")
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
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
                onClicked: ModelInstaller.resetUserCatalog()
            }
            Item { Layout.fillWidth: true }
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
            Label {
                Layout.fillWidth: true
                wrapMode: Label.WordWrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                text: qsTr("Downloading starts after confirmation. The model license "
                           + "applies — review it before installing.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Label.WordWrap
                font.pixelSize: Theme.fontSmall
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
        id: importDialog
        title: qsTr("Import preset catalog")
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const err = ModelInstaller.importCatalog(Runtime.localPath(selectedFile))
            if (err.length) statusMsg.text = err
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export preset catalog")
        nameFilters: [qsTr("JSON files (*.json)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const err = ModelInstaller.exportCatalog(Runtime.localPath(selectedFile))
            if (err.length) statusMsg.text = err
        }
    }

    Label {
        id: statusMsg
        visible: text.length > 0
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
