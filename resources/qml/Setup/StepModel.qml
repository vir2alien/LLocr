pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Runtime.modelPathValid

    onVisibleChanged: {
        if (!visible)
            return
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
    }

    implicitHeight: contentColumn.implicitHeight + 40

    ColumnLayout {
        id: contentColumn
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

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            contentHeight: paneLayout.implicitHeight
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true

            ColumnLayout {
                id: paneLayout
                width: root.width
                spacing: 6

                InstallerStatusLabel {
                    Layout.fillWidth: true
                    isError: ModelInstaller.state === ModelInstaller.Error
                    busy: ModelInstaller.busy
                    statusText: ModelInstaller.statusMessage.length
                          ? ModelInstaller.statusMessage
                          : qsTr("Pick a preset to download; it becomes the "
                                 + "recognition model when it lands.")
                }

                InstallerProgressRow {
                    visible: ModelInstaller.busy
                    Layout.fillWidth: true
                    busy: ModelInstaller.busy
                    progress: ModelInstaller.progress
                    cancelVisible: ModelInstaller.state === ModelInstaller.Downloading
                    onCancelClicked: ModelInstaller.cancelInstall()
                }

                LLOLabel {
                    visible: downloadList.count === 0
                    text: qsTr("No models available")
                    color: Theme.textPrimary
                }

                ModelDownloadList {
                    id: downloadList
                    Layout.fillWidth: true
                    Layout.preferredHeight: downloadList.implicitHeight
                    maxVisibleRows: -1
                    task: "pageOcr"
                    downloadTask: "pageOcr"
                    onActionError: (msg) => statusLabel.text = msg
                    onDownloadRequested: (title, quantId, license, runtimeNote) =>
                            pickDialog.showFor(title, quantId, license, runtimeNote)
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
        }//ScrollView

        Item { Layout.fillHeight: true }
    }//ColumnLayout

    InstallModelDialog {
        id: pickDialog
    }

}
