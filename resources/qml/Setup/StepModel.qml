import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Settings.launchModelPath.trim().length > 0

    // §H.2 estimate for the selected model (see label below the status).
    property var estTotal: 0
    property var estRam: 0
    function gib(bytes) { return bytes / (1024 * 1024 * 1024) }
    function refreshEstimate() {
        if (!Settings.launchModelPath.trim().length) { estTotal = 0; estRam = 0; return }
        var m = Runtime.estimateModelMemory(Settings.launchModelPath, Settings.launchCtxSize)
        root.estTotal = m.totalBytes
        root.estRam = m.systemRamBytes
    }
    Connections {
        target: Settings
        function onLaunchModelPathChanged() { refreshEstimate() }
        function onLaunchCtxSizeChanged() { refreshEstimate() }
    }
    Component.onCompleted: {
        refreshEstimate()
        ModelInstaller.reloadPresets()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Model")
            font.pixelSize: Theme.fontTitle
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textSecondary
            text: qsTr("Vision-capable GGUF models work with the managed server. Pick a "
                       + "preset, find one on Hugging Face, or point at a local file.")
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: ModelInstaller.state === 4 ? Theme.error
                 : (ModelInstaller.busy ? Theme.textSecondary : Theme.textMuted)
            text: ModelInstaller.statusMessage.length
                  ? ModelInstaller.statusMessage
                  : (root.complete
                     ? qsTr("Model selected: %1").arg(Settings.launchModelPath)
                     : qsTr("No model selected yet."))
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            // §H.2 estimate shown next to the selected model.
            visible: root.complete && root.estTotal > 0
            text: qsTr("Estimated footprint: ~%1 GiB (model + context) on %2 GiB RAM")
                .arg(root.gib(root.estTotal).toFixed(1))
                .arg(root.gib(root.estRam).toFixed(1))
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
                    TabButton { text: qsTr("Hugging Face") }
                    TabButton { text: qsTr("Local file") }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: modelTabBar.currentIndex

                    // ----- Presets -------------------------------------------
                    ColumnLayout {
                        spacing: 6
                        Label {
                            text: qsTr("Start from a preset")
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                        }
                        ListView {
                            id: presetList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
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
                                height: 34
                                color: "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 4
                                    spacing: 6
                                    Label {
                                        Layout.preferredWidth: 150
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
                                              ? qsTr("~%1 GiB VRAM").arg(pInfo.approxVramGb) : ""
                                    }
                                    LLOButton {
                                        text: qsTr("Install")
                                        enabled: !ModelInstaller.busy && !pInfo.installed
                                        onClicked: {
                                            prepareDialog.pendingIndex = index
                                            ModelInstaller.preparePreset(index)
                                            prepareDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ----- Hugging Face search --------------------------------
                    ColumnLayout {
                        spacing: 6
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
                            Layout.fillHeight: true
                            clip: true
                            visible: ModelInstaller.searchCount > 0
                            model: ModelInstaller.searchCount
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property int index
                                property var sInfo: ModelInstaller.searchResult(index)
                                width: searchList.width
                                height: 30
                                color: "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 4
                                    spacing: 6
                                    Label {
                                        Layout.preferredWidth: 150
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
                                        enabled: !ModelInstaller.busy
                                        onClicked: {
                                            prepareDialog.pendingIndex = -1
                                            ModelInstaller.installRemote(index)
                                            prepareDialog.open()
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
                    }

                    // ----- Local file -------------------------------------------
                    ColumnLayout {
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
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                            text: qsTr("The local model is not managed: its license is your "
                                       + "responsibility, and it is not verified by the catalog.")
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    // Confirm dialog for preset / remote installs (shows the license).
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
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                text: qsTr("Review the license before installing. Downloading starts "
                           + "after confirmation.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.accent
                visible: prepareDialog.license.length > 0
                // The license field may be a URL or a short name; render a real
                // link only when it is one.
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
    }

    FileDialog {
        id: modelPicker
        title: qsTr("Select a GGUF model")
        nameFilters: [qsTr("GGUF models (*.gguf)"), qsTr("All files (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            localPathField.text = path
            Settings.launchModelPath = path
        }
    }
}