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
                    Settings.resetRuntimeDefaults()
                    rtTab.loadValues()
                }
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    rtTab.loadValues()
                    window.close()
                }
            }
            LLOButton {
                emphasis: true
                text: qsTr("Save")
                onClicked: {
                    rtTab.saveValues()
                    Settings.forceSave()
                    window.close()
                }
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            Runtime.refreshSingleInstanceLock()
            rtTab.loadValues()
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        contentWidth: availableWidth
        contentHeight: rtTab.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        RuntimeTab {
            id: rtTab
            width: parent.width
        }
    }
}