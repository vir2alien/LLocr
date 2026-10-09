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

    Component.onCompleted: {
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
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

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            TabBar {
                id: modelTabBar
                Layout.fillWidth: true
                TabButton { text: qsTr("OCR model") }
                TabButton { text: qsTr("Block OCR model") }
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
                        readonly property string role: forCheck ? "blockRecognition" : "ocr"

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
                                visible: downloadList.count === 0
                                text: qsTr("No models available")
                                color: Theme.textPrimary
                            }

                            ModelDownloadList {
                                id: downloadList
                                Layout.fillWidth: true
                                Layout.preferredHeight: downloadList.implicitHeight
                                maxVisibleRows: -1
                                role: rolePane.role
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
                    }
                }
            }//StackLayout
        }//ColumnLayout

        Item { Layout.fillHeight: true }
    }//ColumnLayout

    InstallModelDialog {
        id: pickDialog
    }

}
