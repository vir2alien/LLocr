pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool preparedForCheck: false

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
                                visible: downloadList.count === 0
                                text: qsTr("No models available")
                                color: Theme.textPrimary
                            }

                            ModelDownloadList {
                                id: downloadList
                                Layout.fillWidth: true
                                Layout.preferredHeight: downloadList.implicitHeight
                                maxVisibleRows: -1
                                isVerifyModelRole: rolePane.forCheck
                                onActionError: (msg) => statusLabel.text = msg
                                onDownloadRequested: (title, quantId, license, runtimeNote) => {
                                    root.preparedForCheck = rolePane.forCheck
                                    pickDialog.modelTitle = title
                                    pickDialog.quantId = quantId
                                    pickDialog.license = license
                                    pickDialog.runtimeNote = runtimeNote
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

        Item { Layout.fillHeight: true }
    }//ColumnLayout

    Dialog {
        id: pickDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Install model")
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string modelTitle: ""
        property string quantId: ""
        property string license: ""
        property string runtimeNote: ""

        ColumnLayout {
            width: parent.width
            spacing: 6
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: [pickDialog.modelTitle, pickDialog.quantId].join(" ").trim()
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textSecondary
                text: qsTr("Downloading starts after confirmation. The model license "
                           + "applies — review it before installing.")
            }
            LLOLabel {
                Layout.fillWidth: true
                visible: pickDialog.runtimeNote.length > 0
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: Theme.warning
                text: qsTr("The managed runtime cannot run this model. %1")
                        .arg(pickDialog.runtimeNote)
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

}
