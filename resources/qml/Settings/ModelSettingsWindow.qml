pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    property string role: "ocr"

    readonly property bool isVerifyModelRole: role === "check"

    title: role === "check" ? qsTr("Check model settings")
                            : qsTr("OCR model settings")
    width: 560
    height: 680
    modality: Qt.NonModal

    // Runtime settings window reference — the Model tab's "Configure runtime"
    // button opens it when the connection mode is External.
    property var runtimeSettingsRef: null

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacing

            LLOButton {
                subtle: true
                text: qsTr("Restore defaults")
                onClicked: {
                    launchTab.resetValues()
                    requestTab.resetValues()
                }
            }
            Item { Layout.fillWidth: true }

            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    launchTab.loadValues()
                    requestTab.loadValues()
                    window.close()
                }
            }
            LLOButton {
                emphasis: true
                text: qsTr("Save")
                onClicked: {
                    launchTab.saveValues()
                    requestTab.saveValues()
                    // RequestTab already resolves the role's profile store;
                    // commit the draft id as the active one for the role.
                    const draftId = requestTab.profiles.draftProfileId
                    if (draftId.length > 0) {
                        if (window.isVerifyModelRole)
                            Settings.checkRequestProfileId = draftId
                        else
                            Settings.modelRecipeId = draftId
                    }
                    Settings.forceSave()
                    window.close()
                }
            }
        }
    }//footer

    function loadValues() {
        launchTab.loadValues()
        requestTab.loadValues()
    }

    onVisibleChanged: {
        if (visible) {
            window.loadValues()
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
        }
    }

    Connections {
        target: tabBar
        function onCurrentIndexChanged() {
            if (tabBar.currentIndex === 0) {
                ModelInstaller.refreshInstalled()
                ModelInstaller.reloadPresets()
                ModelInstaller.rescanRegistry()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            implicitHeight: 32

            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }

            component CustomTabButton: TabButton {
                id: tabBtn
                implicitHeight: 32
                padding: 12

                background: Rectangle { color: "transparent" }

                contentItem: Text {
                    text: tabBtn.text
                    font.pointSize: Theme.bodySmallSize
                    font.bold: tabBtn.checked
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }

                indicator: Rectangle {
                    visible: tabBtn.checked
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 2
                    radius: 1
                    color: Theme.accent
                }
            }

            CustomTabButton { text: qsTr("Model") }
            CustomTabButton { text: qsTr("Launch") }
            CustomTabButton { text: qsTr("Request") }
        }//TabBar

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            LocationTab {
                id: locationTab
                isVerifyModelRole: window.isVerifyModelRole
                runtimeSettingsRef: window.runtimeSettingsRef
            }

            LaunchTab {
                id: launchTab
                checkRole: window.isVerifyModelRole
                runtimeSettingsRef: window.runtimeSettingsRef
            }

            RequestTab {
                id: requestTab
                checkRole: window.isVerifyModelRole
            }
        }
    }//ColumnLayout
}
