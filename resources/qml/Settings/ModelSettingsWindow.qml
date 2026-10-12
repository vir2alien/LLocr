pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    // Model-profile role id: "ocr", "blockRecognition", "decision" or "layout".
    property string role: "ocr"

    title: RoleProfiles.title(role)
    width: 560
    height: 680
    modality: Qt.NonModal

    property var runtimeSettingsRef: null

    // The profile combo in LocationTab applies immediately; the snapshot
    // makes Cancel able to undo it, like the text fields of the other tabs.
    property string profileSnapshot: ""

    function activeProfileId() {
        return RoleProfiles.requestProfileId(role)
    }

    function restoreProfileSnapshot() {
        RoleProfiles.setRequestProfileId(role, profileSnapshot)
    }

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
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
                window.restoreProfileSnapshot()
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
                Settings.forceSave()
                window.close()
            }
        }
    }//footer

    function loadValues() {
        launchTab.loadValues()
        requestTab.loadValues()
    }

    onVisibleChanged: {
        if (visible) {
            window.profileSnapshot = window.activeProfileId()
            window.loadValues()
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: 8

        SettingsTabBar {
            id: tabBar
            Layout.fillWidth: true
            titles: [qsTr("Model"), qsTr("Launch"), qsTr("Request")]
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            LocationTab {
                id: locationTab
                role: window.role
                runtimeSettingsRef: window.runtimeSettingsRef
            }

            LaunchTab {
                id: launchTab
                role: window.role
                runtimeSettingsRef: window.runtimeSettingsRef
            }

            RequestTab {
                id: requestTab
                role: window.role
            }
        }
    }//ColumnLayout
}
