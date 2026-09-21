pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Rectangle {
    id: root

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    readonly property bool isTextBlock: Controller.selectedBlockLabel !== "image"
                                        && Controller.selectedBlockLabel !== "chart"

    implicitHeight: checkColumn.implicitHeight + 2 * Theme.spacingSmall
    visible: Controller.selectedBoxIndex >= 0 && Controller.hasResult

    ColumnLayout {
        id: checkColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: closeButton.implicitHeight
            spacing: Theme.spacingSmall
            LLOLabel {
                text: qsTr("Block %1").arg(Controller.selectedBlockLabel)
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Layout.maximumWidth: root.width - 90
            }
            Rectangle {
                visible: Controller.selectedBlockCheckStatus !== 0
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: Controller.selectedBlockCheckStatus === 1 ? Theme.success
                     : Controller.selectedBlockCheckStatus === 2 ? Theme.warning
                     : Theme.error
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                id: closeButton
                text: "\u2715"
                implicitWidth: 24
                implicitHeight: 22
                onClicked: Controller.selectedBoxIndex = -1
            }
        }

        ScrollView {
            visible: root.isTextBlock
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(recognizedText.contentHeight, 96)
            contentWidth: availableWidth
            TextArea {
                id: recognizedText
                width: parent.width
                readOnly: true
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textSecondary
                background: null
                topPadding: 0
                bottomPadding: 0
                text: Controller.selectedBlockText
            }
        }

        // Corrected (FIX) result, kept separate from the recognized text.
        LLOLabel {
            visible: root.isTextBlock
                     && Controller.selectedBlockCheckStatus === 2
            text: qsTr("Corrected by the verifier:")
            color: Theme.textMuted
        }
        ScrollView {
            visible: root.isTextBlock
                     && Controller.selectedBlockCheckStatus === 2
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(correctedText.contentHeight, 96)
            contentWidth: availableWidth
            TextArea {
                id: correctedText
                width: parent.width
                readOnly: true
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textPrimary
                background: null
                topPadding: 0
                bottomPadding: 0
                text: Controller.selectedBlockCorrected
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            LLOButton {
                text: qsTr("Verify")
                visible: root.isTextBlock
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.checkSelectedBlock()
            }
            LLOButton {
                text: qsTr("Revert correction")
                visible: Controller.selectedBlockCheckStatus !== 0
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.revertBlockCorrection()
            }
            LLOButton {
                text: qsTr("Delete block")
                enabled: !Controller.checkBusy && !Controller.busy
                         && Controller.selectedBoxIndex >= 0
                onClicked: Controller.boxModel.removeBox(Controller.selectedBoxIndex)
            }
            Item { Layout.fillWidth: true }
        }
    }
}
