pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Runtime settings")
    width: 560
    height: 680
    modality: Qt.NonModal

    property var setupWizardRef: null
    property var logWindowRef: null

    property bool canManage: !Runtime.lockedOut
                             && Settings.serverPath.trim().length > 0

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

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
                    Settings.resetRuntimeDefaults()
                    rtTab.loadValues()
                }
            }
            LLOButton {
                text: qsTr("Save")
                onClicked: {
                    rtTab.saveValues()
                    Settings.forceSave()
                    window.close()
                }
            }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    rtTab.loadValues()
                    window.close()
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    onVisibleChanged: {
        if (visible) {
            Runtime.refreshSingleInstanceLock()
            rtTab.loadValues()
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: availableWidth
        contentHeight: rtTab.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        RuntimeTab {
            id: rtTab
            width: parent.width
            setupWizardRef: window.setupWizardRef
            logWindowRef: window.logWindowRef
        }
    }
}