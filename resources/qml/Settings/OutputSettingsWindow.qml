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
                text: qsTr("Save")
                onClicked: {
                    outputTab.saveValues()
                    Settings.forceSave()
                    window.close()
                }
            }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: {
                    outputTab.loadValues()
                    window.close()
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    onVisibleChanged: {
        if (visible)
            outputTab.loadValues()
    }

    OutputTab {
        id: outputTab
        anchors.fill: parent
        anchors.margins: 12
    }
}