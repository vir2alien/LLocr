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
    property string blockText: ""
    // The block is open for moving and resizing: the button offers the way
    // out (ADR 189).
    property bool editing: false
    signal editToggled()

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
            iconName: miniRoot.editing ? "check" : "move"
            tip: miniRoot.editing ? qsTr("Finish editing") : qsTr("Move or resize the block")
            onClicked: miniRoot.editToggled()
        }
        MiniButton {
            visible: miniRoot.isTextBlock
            iconName: "scan-text"
            tip: qsTr("Recognize block")
            enabled: !ModelInstaller.taskQueueActive && Controller.actionsIdle && Controller.blockRecognitionModelAvailable
            onClicked: Controller.recognizeSelectedBlock()
        }
        MiniButton {
            visible: miniRoot.isTextBlock
            iconName: "circle-check"
            tip: qsTr("Verify block")
            enabled: !ModelInstaller.taskQueueActive && Controller.actionsIdle && Controller.decisionModelAvailable
                     && miniRoot.blockText.length > 0
            onClicked: Controller.checkSelectedBlock()
        }
        MiniButton {
            visible: miniRoot.isTextBlock
            iconName: "wand-sparkles"
            tip: qsTr("Fix block")
            enabled: !ModelInstaller.taskQueueActive && Controller.actionsIdle && Controller.fixModelAvailable
                     && miniRoot.blockText.length > 0
            onClicked: Controller.fixSelectedBlock()
        }
        MiniButton {
            iconName: "trash-2"
            tip: qsTr("Delete block")
            enabled: !ModelInstaller.taskQueueActive && Controller.actionsIdle
                     && Controller.selectedBoxIndex >= 0
            onClicked: Controller.removeBlock(Controller.selectedBoxIndex)
        }
    }
}
