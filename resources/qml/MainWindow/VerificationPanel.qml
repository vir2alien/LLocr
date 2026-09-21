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

    implicitHeight: checkColumn.implicitHeight + 2 * Theme.spacingSmall
    visible: (Controller.selectedBoxIndex >= 0 || Controller.checkRunning)
             && Controller.hasResult

    ColumnLayout {
        id: checkColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            LLOLabel {
                text: qsTr("Check block")
                font.bold: true
                color: Theme.textPrimary
            }
            LLOLabel {
                text: Controller.selectedBlockLabel
                color: Theme.textMuted
                elide: Text.ElideRight
            }
            // Status marker: green OK, yellow fixed, red review.
            LLOLabel {
                visible: Controller.selectedBlockCheckStatus !== 0
                text: Controller.selectedBlockCheckStatus === 1 ? qsTr("OK")
                    : Controller.selectedBlockCheckStatus === 2 ? qsTr("Fixed")
                    : qsTr("Review")
                color: Controller.selectedBlockCheckStatus === 1 ? Theme.success
                    : Controller.selectedBlockCheckStatus === 2 ? Theme.warning
                    : Theme.error
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: "\u2715"
                implicitWidth: 24
                implicitHeight: 22
                onClicked: Controller.selectedBoxIndex = -1
            }
        }

        LLOLabel {
            text: qsTr("Recognized text:")
            color: Theme.textMuted
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            contentWidth: availableWidth
            TextArea {
                width: parent.width
                readOnly: true
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textSecondary
                background: null
                text: Controller.selectedBlockText
            }
        }

        // Corrected (FIX) result, kept separate from the recognized text.
        LLOLabel {
            visible: Controller.selectedBlockCheckStatus === 2
            text: qsTr("Corrected by the verifier:")
            color: Theme.textMuted
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            visible: Controller.selectedBlockCheckStatus === 2
            contentWidth: availableWidth
            TextArea {
                width: parent.width
                readOnly: true
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textPrimary
                background: null
                text: Controller.selectedBlockCorrected
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            LLOButton {
                text: qsTr("Verify block")
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.checkSelectedBlock()
            }
            BusyIndicator {
                visible: Controller.checkBusy
                implicitWidth: 20
                implicitHeight: 20
                running: Controller.checkBusy
            }
            LLOLabel {
                visible: Controller.checkRunning
                text: qsTr("%1 / %2").arg(Controller.checkProgressDone)
                                          .arg(Controller.checkProgressTotal)
                color: Theme.textMuted
                font.pointSize: Theme.captionSize
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Stop")
                visible: Controller.checkBusy
                enabled: Controller.checkBusy
                onClicked: Controller.stopCheck()
            }
        }

        LLOLabel {
            visible: Controller.checkErrorMessage.length > 0
            text: Controller.checkErrorMessage
            color: Theme.error
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredHeight: visible
                                  ? Math.max(0, Math.min(Math.ceil(contentHeight), 96))
                                  : 0
        }
    }
}
