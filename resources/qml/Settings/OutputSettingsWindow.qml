pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Output settings")
    width: 500
    height: 540
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
                // Guarded setters skip the NOTIFY when a value already equals
                // the default, so re-read the controls explicitly.
                onClicked: {
                    Settings.resetOutputDefaults()
                    outputTab.loadValues()
                }
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    outputTab.loadValues()
                    window.close()
                }
            }
            LLOButton {
                emphasis: true
                text: qsTr("Save")
                onClicked: {
                    outputTab.saveValues()
                    Settings.forceSave()
                    window.close()
                }
            }
        }
    }

    onVisibleChanged: {
        if (visible)
            outputTab.loadValues()
    }

    OutputTab {
        id: outputTab
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
    }
}