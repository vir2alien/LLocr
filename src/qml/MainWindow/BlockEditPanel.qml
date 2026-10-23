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

    // The two layers of a block (ADR 164): the OCR text and the correction.
    // The panel's toggle picks the one the user reads, and the verdict follows
    // that layer (ADR 185).
    readonly property bool correctionStored: Controller.selectedBlockCorrected.length > 0
                                             && Controller.selectedBlockCorrected !== Controller.selectedBlockText
    readonly property bool correctionActive: Controller.selectedBlockCorrected.length > 0
                                             || Controller.selectedBlockCorrectedCheckStatus !== 0
    readonly property bool correctionInView: Controller.selectedBlockUsesCorrected && correctionStored
    readonly property int effectiveStatus: Controller.selectedBlockUsesCorrected && correctionActive
                                           ? Controller.selectedBlockCorrectedCheckStatus
                                           : Controller.selectedBlockCheckStatus
    readonly property double effectiveConfidence: Controller.selectedBlockUsesCorrected && correctionActive
                                                  ? Controller.selectedBlockCorrectedConfidence
                                                  : Controller.selectedBlockConfidence
    // The score belongs to the model's verdict: an approved block carries no
    // verdict of its own.
    readonly property bool scoreVisible: effectiveConfidence >= 0 && effectiveStatus !== 5

    // The word next to each dot: what the layer's pipeline state is. A layer
    // nobody judged yet carries no word — its title says what it is.
    function statusWord(status) {
        if (status === 1)
            return qsTr("Verified")
        if (status === 5)
            return qsTr("Approved")
        if (status === 3)
            return qsTr("Damaged")
        if (status === 4)
            return qsTr("Mismatch")
        return ""
    }
    function dotColor(status) {
        if (status === 1 || status === 5)
            return Theme.success
        if (status >= 3)
            return Theme.error
        return Theme.nothing
    }
    function scoreText(confidence) {
        return Math.round(confidence * 100) + "%"
    }

    // The overall state of the block: recognized (nothing checked it yet),
    // verified (the layer in view carries a verdict), corrected (a correction
    // the decision has not judged yet) or approved by hand.
    readonly property string overallStatus: effectiveStatus === 5 ? qsTr("Approved")
                                          : effectiveStatus !== 0 ? qsTr("Verified")
                                          : correctionInView ? qsTr("Corrected")
                                          : qsTr("Recognized")

    // The outcome of the last fix run — the one thing the correction layer
    // alone cannot tell (ADR 185).
    readonly property string fixResult: {
        if (Controller.selectedBlockFixStatus === 1)
            return qsTr("No correction required")
        if (Controller.selectedBlockFixStatus === 2)
            return qsTr("Corrected")
        if (Controller.selectedBlockFixStatus === 3)
            return qsTr("Damaged")
        return ""
    }

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
                text: Controller.selectedBlockLabel
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                // The row keeps room for the dot, the status, the score, the
                // approve button and the close button; the label gives way.
                Layout.maximumWidth: Math.max(0, root.width - 300)
            }
            Rectangle {
                visible: root.effectiveStatus !== 0
                         || Controller.selectedBlockText.length > 0
                         || Controller.selectedBlockCorrected.length > 0
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: root.dotColor(root.effectiveStatus)
            }
            LLOLabel {
                visible: root.overallStatus.length > 0
                text: root.overallStatus
                color: Theme.textMuted
            }
            LLOLabel {
                visible: root.scoreVisible
                text: root.scoreText(root.effectiveConfidence)
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                visible: root.isTextBlock && Controller.selectedBlockText.length > 0
                text: root.correctionInView ? qsTr("Approve correction") : qsTr("Approve block")
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.approveSelectedBlock()
            }
            LLOButton {
                id: closeButton
                text: "\u2715"
                implicitWidth: 24
                implicitHeight: 22
                onClicked: Controller.selectedBoxIndex = -1
            }
        }

        // What the last fix run answered (ADR 185): a confirmed text, a stored
        // correction or a block the fixer could not read.
        RowLayout {
            visible: root.fixResult.length > 0
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            LLOLabel {
                text: qsTr("Correction:")
                color: Theme.textMuted
            }
            LLOLabel {
                text: root.fixResult
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }
        }

        // The OCR text and its own verdict.
        RowLayout {
            visible: root.isTextBlock && Controller.selectedBlockText.length > 0
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: root.dotColor(Controller.selectedBlockCheckStatus)
            }
            LLOLabel {
                text: qsTr("Recognized text")
                color: Theme.textMuted
            }
            LLOLabel {
                text: root.statusWord(Controller.selectedBlockCheckStatus)
                visible: text.length > 0
                Layout.leftMargin: Theme.spacing
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
        }

        ScrollView {
            visible: root.isTextBlock && Controller.selectedBlockText.length > 0
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
        // A block the fixer confirmed carries no correction to show.
        RowLayout {
            visible: root.isTextBlock && root.correctionStored
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: root.dotColor(Controller.selectedBlockCorrectedCheckStatus)
            }
            LLOLabel {
                text: qsTr("Corrected text")
                color: Theme.textMuted
            }
            LLOLabel {
                text: root.statusWord(Controller.selectedBlockCorrectedCheckStatus)
                visible: text.length > 0
                Layout.leftMargin: Theme.spacing
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
            LLOButton {
                text: root.correctionInView ? qsTr("Show original") : qsTr("Show correction")
                onClicked: Controller.selectedBlockUsesCorrected = !root.correctionInView
            }
            LLOButton {
                text: qsTr("Revert correction")
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.revertBlockCorrection()
            }
        }
        ScrollView {
            visible: root.isTextBlock && root.correctionStored
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
