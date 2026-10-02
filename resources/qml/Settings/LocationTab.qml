pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root
    property bool isVerifyModelRole: false
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property string activeTitleText: isVerifyModelRole
        ? ModelInstaller.checkActiveTitle : ModelInstaller.activeTitle

    readonly property var profileList: isVerifyModelRole
        ? RequestProfilesValidate.profileModel : RequestProfilesOcr.profileModel
    readonly property string activeProfileId: isVerifyModelRole
        ? Settings.checkRequestProfileId
        : (Settings.requestProfileId.length
           ? Settings.requestProfileId : Settings.modelRecipeId)

    function syncProfileBox() {
        const row = profileList.rowOfId(root.activeProfileId)
        profileBox.currentIndex = row >= 0 ? row : 0
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

            ColumnLayout {
                visible: root.externalMode
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
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

                LLOLabel {
                    text: qsTr("Model profile")
                }
                ComboBox {
                    id: profileBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: root.profileList
                    textRole: "displayName"
                    valueRole: "modelId"
                    Component.onCompleted: root.syncProfileBox()
                    Connections {
                        target: Settings
                        function onModelRecipeIdChanged() { root.syncProfileBox() }
                        function onRequestProfileIdChanged() { root.syncProfileBox() }
                        function onCheckRequestProfileIdChanged() { root.syncProfileBox() }
                    }
                    onActivated: {
                        if (root.isVerifyModelRole) {
                            Settings.checkRequestProfileId = profileBox.currentValue
                        } else {
                            Settings.modelRecipeId = profileBox.currentValue
                            Settings.requestProfileId = profileBox.currentValue
                        }
                    }
                }

                LLOButton {
                    text: qsTr("Configure runtime…")
                    onClicked: {
                        if (root.runtimeSettingsRef)
                            root.runtimeSettingsRef.show()
                    }
                }
            }

            ColumnLayout {
                visible: !root.externalMode
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 6

                LLOLabel {
                    Layout.fillWidth: true
                    visible: root.activeTitleText.length > 0
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.textSecondary
                    text: qsTr("Activated: %1").arg(root.activeTitleText)
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
                    text: qsTr("Models")
                }

                ModelDownloadList {
                    id: downloadList
                    Layout.fillWidth: true
                    Layout.preferredHeight: downloadList.implicitHeight
                    maxVisibleRows: -1
                    isVerifyModelRole: root.isVerifyModelRole
                    onActionError: (msg) => statusMsg.text = msg
                    onDownloadRequested: (title, quantId, license, runtimeNote) => {
                        pickDialog.modelTitle = title
                        pickDialog.quantId = quantId
                        pickDialog.license = license
                        pickDialog.runtimeNote = runtimeNote
                        pickDialog.open()
                    }
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
        }//ColumnLayout
    }//ScrollView

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
                wrapMode: Text.WordWrap
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
