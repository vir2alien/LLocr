pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr

import "../Common"

// Floating action bar for the selected block; the delegate positions it at
// the block frame (above, or below when there is no room).
Rectangle {
    id: miniRoot

    property bool isTextBlock: true

    implicitWidth: miniRow.implicitWidth + 2 * Theme.spacingSmall
    implicitHeight: miniRow.implicitHeight + 2 * Theme.spacingSmall
    width: implicitWidth
    height: implicitHeight

    radius: Theme.controlRadius
    color: Theme.surface
    border.color: Theme.border

    // Swallow the clicks so the bar padding does not deselect the block
    // (or select whatever lies beneath).
    MouseArea {
        anchors.fill: parent
    }

    Row {
        id: miniRow
        anchors.centerIn: parent
        spacing: 0

        component MiniButton: ToolButton {
            id: miniButton

            property string iconName: ""
            property string tip: ""

            implicitWidth: 32
            implicitHeight: 26
            padding: 0

            Icon {
                anchors.centerIn: parent
                name: miniButton.iconName
                size: 16
                color: miniButton.enabled ? Theme.textPrimary : Theme.textMuted
            }

            ToolTip.visible: miniButton.hovered
            ToolTip.text: miniButton.tip
        }

        MiniButton {
            visible: miniRoot.isTextBlock
            iconName: "scan-text"
            tip: qsTr("Recognize block")
            enabled: !Controller.checkBusy && !Controller.busy
            onClicked: Controller.recognizeSelectedBlock()
        }
        MiniButton {
            visible: miniRoot.isTextBlock
            iconName: "circle-check"
            tip: qsTr("Verify block")
            enabled: !Controller.checkBusy && !Controller.busy
            onClicked: Controller.checkSelectedBlock()
        }
        MiniButton {
            iconName: "trash-2"
            tip: qsTr("Delete block")
            enabled: !Controller.checkBusy && !Controller.busy
                     && Controller.selectedBoxIndex >= 0
            onClicked: Controller.removeBlock(Controller.selectedBoxIndex)
        }
    }
}
