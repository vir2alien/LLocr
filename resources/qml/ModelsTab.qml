import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr

// Settings → Models (Stage E): local registry, preset catalog and Hugging Face
// search/download. Backed by the `ModelInstaller` singleton.
Item {
    id: root

    function fmtBytes(bytes) {
        if (bytes <= 0) return ""
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ----- Status line ------------------------------------------------
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

        ProgressBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            visible: ModelInstaller.busy
            from: 0
            to: 1
            value: ModelInstaller.progress
        }

        // ----- Installed models -------------------------------------------
        Label {
            text: qsTr("Installed models")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        ListView {
            id: installedList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.height * 0.32, 220)
            clip: true
            model: ModelInstaller.installedCount
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                property var info: ModelInstaller.installedInfo(index)
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
                        Layout.preferredWidth: 150
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textPrimary
                        text: info.title
                    }
                    Label {
                        Layout.preferredWidth: 70
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textSecondary
                        text: info.quantization
                    }
                    Label {
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        text: fmtBytes(info.size)
                    }
                    Label {
                        Layout.preferredWidth: 60
                        font.pixelSize: Theme.fontSmall
                        color: info.origin === "managed" ? Theme.textSecondary : Theme.textMuted
                        text: info.origin === "managed" ? qsTr("managed") : qsTr("external")
                    }
                    Button {
                        text: info.active ? qsTr("Active") : qsTr("Activate")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontSmall
                        enabled: !info.active
                        onClicked: ModelInstaller.setActiveModel(index)
                    }
                    Button {
                        text: qsTr("Remove")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontSmall
                        enabled: info.origin === "managed"
                        onClicked: {
                            const err = ModelInstaller.removeModel(index)
                            if (err.length)
                                statusMsg.text = err
                        }
                    }
                }
            }
        }

        // ----- Preset catalog ---------------------------------------------
        Label {
            text: qsTr("Preset catalog")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        ListView {
            id: presetList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.height * 0.28, 160)
            clip: true
            model: ModelInstaller.presetCount
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                property var pInfo: ModelInstaller.presetInfo(index)
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
                    Button {
                        text: qsTr("Install")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontSmall
                        enabled: !ModelInstaller.busy
                        onClicked: {
                            ModelInstaller.preparePreset(index)
                            preparedIndex = index
                            pickDialog.open()
                        }
                    }
                }
            }
        }

        // ----- HF search ---------------------------------------------------
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
            Button {
                text: qsTr("Search")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: {
                    ModelInstaller.searchQuery = searchField.text.trim()
                    ModelInstaller.startSearch()
                }
            }
        }

        ListView {
            id: searchList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(root.height * 0.24, 140)
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
                    Button {
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

        // ----- HF token ----------------------------------------------------
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

        // ----- Catalog import/export --------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Import catalog…")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: importDialog.open()
            }
            Button {
                text: qsTr("Export catalog…")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: exportDialog.open()
            }
            Button {
                text: qsTr("Restore defaults")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: ModelInstaller.resetUserCatalog()
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }

    // Confirmation for a prepared/remote install with the license link.
    Dialog {
        id: pickDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Install model")
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string license: ""

        ColumnLayout {
            spacing: 6
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                text: qsTr("Downloading starts after confirmation. The model license "
                           + "applies — review it before installing.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.accent
                visible: pickDialog.license.length > 0
                text: qsTr("License: %1").arg(pickDialog.license)
                linkColor: Theme.accent
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }

        onAccepted: {
            ModelInstaller.installPrepared()
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import preset catalog")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        onAccepted: {
            const err = ModelInstaller.importCatalog(selectedFile)
            if (err.length) statusMsg.text = err
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export preset catalog")
        nameFilters: ["JSON files (*.json)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const err = ModelInstaller.exportCatalog(selectedFile)
            if (err.length) statusMsg.text = err
        }
    }

    // Shared inline status label (errors from row actions land here).
    Label {
        id: statusMsg
        visible: false
    }

    // License of the prepared preset is surfaced in the confirm dialog.
    property int preparedIndex: -1
    Connections {
        target: ModelInstaller
        function onStateChanged() {
            if (ModelInstaller.state === 2 && pickDialog.visible) {
                // ReadyToDownload: show the license for the prepared preset.
                const idx = preparedIndex
                if (idx >= 0 && idx < ModelInstaller.presetCount)
                    pickDialog.license = ModelInstaller.presetInfo(idx).license
                else
                    pickDialog.license = ""
            }
        }
    }
}