pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    // "ocr" fills the recognition-model settings, "check" the verification
    // model's; the tab contents (Location/Launch/Request) follow the role.
    property string role: "ocr"

    title: role === "check" ? qsTr("Check model settings")
                            : qsTr("OCR model settings")
    width: 560
    height: 680
    modality: Qt.NonModal

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    readonly property bool checkRole: role === "check"

    footer: ToolBar {
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: Theme.spacingSmall

            LLOButton {
                text: qsTr("Restore defaults")
                onClicked: {
                    launchTab.resetValues()
                    requestTab.resetValues()
                }
            }
            Item { Layout.fillWidth: true }

            LLOButton {
                text: qsTr("Save")
                onClicked: {
                    launchTab.saveValues()
                    requestTab.saveValues()
                    // RequestTab already resolves the role's profile store;
                    // commit the draft id as the active one for the role.
                    const draftId = requestTab.profiles.draftProfileId
                    if (draftId.length > 0) {
                        if (window.checkRole)
                            Settings.checkRequestProfileId = draftId
                        else
                            Settings.modelRecipeId = draftId
                    }
                    Settings.forceSave()
                    window.close()
                }
            }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    launchTab.loadValues()
                    requestTab.loadValues()
                    window.close()
                }
            }
        }
    }

    function loadValues() {
        launchTab.loadValues()
        requestTab.loadValues()
    }

    onVisibleChanged: {
        if (visible) {
            window.loadValues()
            // The old tab bar refreshed the Location lists on every entry;
            // keep that behavior for window (re)opens — index changes do not
            // fire when the tab is already active.
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            implicitHeight: 28

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
                implicitHeight: 28
                padding: 4
                contentItem: Text {
                    text: tabBtn.text
                    font.pointSize: Theme.captionSize
                    color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }
                background: Rectangle {
                    color: tabBtn.checked ? Theme.surface : Theme.surfaceSunken
                    border.color: Theme.divider
                    border.width: 1
                    Rectangle {
                        visible: tabBtn.checked
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: Theme.surface
                    }
                }
            }

            CustomTabButton { text: qsTr("Location") }
            CustomTabButton { text: qsTr("Launch") }
            CustomTabButton { text: qsTr("Request") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            LocationTab {
                id: locationTab
                checkRole: window.checkRole
            }

            LaunchTab {
                id: launchTab
                checkRole: window.checkRole
            }

            // Both roles use the parameter-profile table; the tab binds to the
            // role's request profile store.
            RequestTab {
                id: requestTab
                checkRole: window.checkRole
            }
        }
    }

    // Refresh the model lists when the Location tab (with the installed-model
    // / preset / search lists) becomes active.
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
}
