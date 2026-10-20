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

    readonly property bool isTextBlock: Controller.selectedBoxIndex < 0
                                        || !Controller.boxModel.isImageBox(Controller.selectedBoxIndex)

    // The dot shows the effective layer (ADR 164): the correction's status
    // when the block carries one (or its damaged verdict), else the text's.
    readonly property bool correctionActive: Controller.selectedBlockCorrected.length > 0
                                             || Controller.selectedBlockCorrectedCheckStatus !== 0
    readonly property int effectiveStatus: correctionActive
                                           ? Controller.selectedBlockCorrectedCheckStatus
                                           : Controller.selectedBlockCheckStatus

    // The word next to each dot: what the layer's pipeline state is.
    function statusWord(status, emptyWord) {
        if (status === 1)
            return qsTr("Verified")
        if (status >= 3)
            return status === 3 ? qsTr("Damaged") : qsTr("Mismatch")
        return emptyWord
    }
    readonly property string effectiveCaption: effectiveStatus !== 0
        ? statusWord(effectiveStatus, "")
        : correctionActive ? qsTr("Fixed")
        : (Controller.selectedBlockText.length > 0 ? qsTr("Recognized") : "")
    readonly property string correctionCaption: statusWord(Controller.selectedBlockCorrectedCheckStatus, qsTr("Fixed"))

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
                visible: root.effectiveStatus !== 0
                         || Controller.selectedBlockText.length > 0
                         || Controller.selectedBlockCorrected.length > 0
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: root.effectiveStatus === 1 ? Theme.success
                     : root.effectiveStatus >= 3 ? Theme.error
                     : Theme.nothing
            }
            LLOLabel {
                visible: root.effectiveCaption.length > 0
                text: root.effectiveCaption
                color: Theme.textMuted
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

        // The correction layer: the universal model's fix, judged separately
        // from the text («Отменить исправление» rolls the block back to it).
        RowLayout {
            visible: root.isTextBlock
                     && Controller.selectedBlockCorrected.length > 0
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            LLOLabel {
                text: qsTr("Corrected text:")
                color: Theme.textMuted
            }
            Rectangle {
                visible: Controller.selectedBlockCorrectedCheckStatus !== 0
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: Controller.selectedBlockCorrectedCheckStatus === 1 ? Theme.success
                     : Controller.selectedBlockCorrectedCheckStatus >= 3 ? Theme.error
                     : Theme.nothing
            }
            LLOLabel {
                text: root.correctionCaption
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Revert correction")
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.revertBlockCorrection()
            }
        }
        ScrollView {
            visible: root.isTextBlock
                     && Controller.selectedBlockCorrected.length > 0
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
    }
}
