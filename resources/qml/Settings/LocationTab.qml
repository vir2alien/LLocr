pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root
    // Model-profile role id: "ocr", "blockRecognition", "decision" or "layout".
    property string role: "ocr"
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property string activeTitleText: role === "blockRecognition" ? ModelInstaller.checkActiveTitle
                                            : role === "decision" ? ModelInstaller.decisionActiveTitle
                                            : role === "layout" ? ModelInstaller.layoutActiveTitle
                                            : ModelInstaller.activeTitle

    readonly property var profileList: role === "blockRecognition" ? RequestProfilesValidate.profileModel
                                     : role === "decision" ? RequestProfilesDecision.profileModel
                                     : role === "layout" ? RequestProfilesLayout.profileModel
                                     : RequestProfilesOcr.profileModel
    readonly property string activeProfileId: role === "blockRecognition" ? Settings.checkRequestProfileId
                                            : role === "decision" ? Settings.decisionRequestProfileId
                                            : role === "layout" ? Settings.layoutRequestProfileId
                                            : Settings.modelRecipeId

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
                    text: qsTr("The model is managed by the external server. Location and download settings are not available in this mode.")
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
                        function onCheckRequestProfileIdChanged() { root.syncProfileBox() }
                        function onDecisionRequestProfileIdChanged() { root.syncProfileBox() }
                        function onLayoutRequestProfileIdChanged() { root.syncProfileBox() }
                    }
                    onActivated: {
                        if (root.role === "blockRecognition")
                            Settings.checkRequestProfileId = profileBox.currentValue
                        else if (root.role === "decision")
                            Settings.decisionRequestProfileId = profileBox.currentValue
                        else if (root.role === "layout")
                            Settings.layoutRequestProfileId = profileBox.currentValue
                        else
                            Settings.modelRecipeId = profileBox.currentValue
                    }
                }

                LLOButton {
                    text: qsTr("Configure runtime…")
                    onClicked: {
                        if (root.runtimeSettingsRef)
                            root.runtimeSettingsRef.bringToFront()
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
                    role: root.role
                    onActionError: (msg) => statusMsg.text = msg
                    onDownloadRequested: (title, quantId, license, runtimeNote) =>
                        pickDialog.showFor(title, quantId, license, runtimeNote)
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

    InstallModelDialog {
        id: pickDialog
    }
}
