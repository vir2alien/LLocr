pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

// The inspector of the selected block: the two versions of its text (the OCR
// result and the correction), the verdict each of them earned and the actions
// the operator has (ADR 185, ADR 186).
//
// The card in view is the version the document shows; the header carries the
// state of the block and the actions. Every status has one place: the pipeline
// state in the header, the per-layer verdicts in the card headers.
Rectangle {
    id: root

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    readonly property bool isTextBlock: Controller.selectedBoxIndex < 0
                                        || !Controller.boxModel.isImageBox(Controller.selectedBoxIndex)

    // The two layers of a block (ADR 164): the OCR text and the correction.
    // The card in view is the one the user reads, and the verdict follows
    // that layer (ADR 185).
    readonly property bool correctionStored: Controller.selectedBlockCorrected.length > 0
                                             && Controller.selectedBlockCorrected !== Controller.selectedBlockText
    readonly property bool correctionActive: Controller.selectedBlockCorrected.length > 0
                                             || Controller.selectedBlockCorrectedCheckStatus !== 0
    readonly property bool correctionInView: Controller.selectedBlockUsesCorrected && correctionStored
    readonly property int effectiveStatus: Controller.selectedBlockUsesCorrected && correctionActive
                                           ? Controller.selectedBlockCorrectedCheckStatus
                                           : Controller.selectedBlockCheckStatus
    // The layer the operator acts on: the one the panel shows (ADR 187).
    readonly property bool correctionLayerInView: Controller.selectedBlockUsesCorrected && correctionActive
    // The block in view carries the user's approval: the button offers the undo.
    readonly property bool approved: effectiveStatus === 5
    // A block the user drew and nothing has been recognized in yet: it is
    // markup, not a recognized text (ADR 189). The page says the same about
    // itself — «Только разметка».
    readonly property bool markupOnly: effectiveStatus === 0
                                       && Controller.selectedBlockText.length === 0
                                       && Controller.selectedBlockCorrected.length === 0

    // A formula, a table or a snippet is read in a monospace face: the
    // brackets, the subscripts and the spaces must not drift apart.
    readonly property bool monoBlock: {
        const label = Controller.selectedBlockLabel.toLowerCase()
        return label.indexOf("equation") >= 0 || label.indexOf("formula") >= 0
                || label.indexOf("code") >= 0 || label.indexOf("latex") >= 0
                || label.indexOf("math") >= 0 || label.indexOf("table") >= 0
    }

    // The word next to each dot: what the layer's pipeline state is. A layer
    // nobody judged yet carries no word — its title says what it is.
    function statusWord(status) {
        if (status === 1)
            return qsTr("Verified")
        if (status === 5)
            return qsTr("Approved")
        if (status === 6)
            return qsTr("Edited by hand")
        if (status === 3)
            return qsTr("Damaged")
        if (status === 4)
            return qsTr("Mismatch")
        if (status === 7)
            return qsTr("Invalid syntax")
        return ""
    }
    function dotColor(status) {
        if (status === 1 || status === 5 || status === 6)
            return Theme.success
        if (status === 3 || status === 4 || status === 7)
            return Theme.error
        return Theme.nothing
    }
    // What is wrong with the formula's LaTeX (ADR 194): the kind the app
    // found, named in the language of the interface.
    function syntaxProblemWord(problem) {
        if (problem === 1)
            return qsTr("Unclosed brace")
        if (problem === 2)
            return qsTr("Stray closing brace")
        if (problem === 3)
            return qsTr("Unclosed environment")
        if (problem === 4)
            return qsTr("Mismatched environment")
        if (problem === 5)
            return qsTr("Unbalanced \\left and \\right")
        if (problem === 6)
            return qsTr("Unclosed math delimiter")
        if (problem === 7)
            return qsTr("Truncated command")
        if (problem === 8)
            return qsTr("Missing argument")
        if (problem === 9)
            return qsTr("Code block inside the formula")
        return ""
    }
    readonly property string syntaxProblem: syntaxProblemWord(Controller.selectedBlockSyntaxProblem)
    function scoreText(confidence) {
        return confidence >= 0 ? Math.round(confidence * 100) + "%" : ""
    }
    // The score belongs to the model's verdict: a human state (approved,
    // written by hand) has no score of its own (ADR 187).
    function layerScore(confidence, status) {
        if (status === 5 || status === 6)
            return ""
        return scoreText(confidence)
    }

    // The state of the block in the header: what the pipeline did with it.
    readonly property string overallStatus: effectiveStatus === 5 ? qsTr("Approved")
                                          : effectiveStatus === 6 ? qsTr("Edited by hand")
                                          : effectiveStatus === 1 ? qsTr("Verified")
                                          : effectiveStatus === 3 ? qsTr("Damaged")
                                          : effectiveStatus === 4 ? qsTr("Mismatch")
                                          : effectiveStatus === 7 ? qsTr("Invalid syntax")
                                          : correctionInView ? qsTr("Corrected")
                                          : root.markupOnly ? qsTr("Markup")
                                          : qsTr("Recognized")

    // The outcome of the last fix run (ADR 185). A fix that confirmed the
    // text stores nothing: there is no correction to show, and the header
    // says so.
    readonly property bool fixNotNeeded: Controller.selectedBlockFixStatus === 1 && !correctionStored
    readonly property bool fixDamaged: Controller.selectedBlockFixStatus === 3

    // The text of a layer as HTML: the fragments the correction changed are
    // tinted, the rest is the plain text (ADR 186).
    function escapeHtml(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function layerHtml(text, ranges, background, foreground) {
        if (!text)
            return ""
        var out = "<span style=\"white-space: pre-wrap\">"
        var pos = 0
        for (var i = 0; i < ranges.length; ++i) {
            const start = ranges[i].start
            const length = ranges[i].length
            if (start > pos)
                out += escapeHtml(text.substring(pos, start))
            out += "<span style=\"background-color:" + background + ";color:" + foreground + "\">"
            out += escapeHtml(text.substring(start, start + length))
            out += "</span>"
            pos = start + length
        }
        if (pos < text.length)
            out += escapeHtml(text.substring(pos))
        return out + "</span>"
    }
    readonly property string originalHtml: layerHtml(Controller.selectedBlockText,
                                                    Controller.highlightDifferences ? Controller.selectedBlockRemovedRanges : [],
                                                    Theme.diffRemovedBg,
                                                    Theme.diffRemovedText)
    readonly property string correctedHtml: layerHtml(Controller.selectedBlockCorrected,
                                                     Controller.highlightDifferences ? Controller.selectedBlockAddedRanges : [],
                                                     Theme.diffAddedBg,
                                                     Theme.diffAddedText)
    implicitHeight: checkColumn.implicitHeight + 2 * Theme.spacingSmall
    visible: Controller.selectedBoxIndex >= 0 && Controller.hasResult

    // Approve and move on: the operator's rhythm. The page text editor keeps
    // the Return — a shortcut that fires while a text is being typed in would
    // approve a block by accident.
    Shortcut {
        sequences: ["Ctrl+Return", "Meta+Return"]
        enabled: Controller.selectedBoxIndex >= 0 && !Controller.pageTextEditorFocused
        onActivated: Controller.approveSelectedBlockAndSelectNext()
    }

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
                // The row keeps room for the status, the buttons and the close
                // button; the label gives way.
                Layout.maximumWidth: Math.max(0, root.width - 380)
            }
            Rectangle {
                // A block without content carries no dot — the overlay shows
                // none either (ADR 189).
                visible: !root.markupOnly
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: root.dotColor(root.effectiveStatus)
            }
            LLOLabel {
                text: root.overallStatus
                color: Theme.textSecondary
            }
            LLOLabel {
                // The reason the formula was rejected (ADR 194): the panel
                // names what is broken, so the operator knows what to look at.
                visible: root.effectiveStatus === 7 && root.syntaxProblem.length > 0
                text: root.syntaxProblem
                color: Theme.textMuted
            }
            LLOLabel {
                visible: root.fixNotNeeded
                text: qsTr("No correction required")
                color: Theme.textMuted
            }
            Item {
                Layout.fillWidth: true
            }
            LLOButton {
                // The switch compares the two versions: without a correction
                // there is nothing to compare (ADR 188).
                visible: root.isTextBlock && root.correctionStored
                text: qsTr("Differences")
                checkable: true
                checked: Controller.highlightDifferences
                onToggled: Controller.highlightDifferences = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Highlight what the correction changed")
            }
            LLOButton {
                visible: root.correctionStored || root.fixDamaged
                text: qsTr("Reset correction")
                subtle: true
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: Controller.revertBlockCorrection()
            }
            LLOButton {
                visible: root.isTextBlock && Controller.selectedBlockText.length > 0
                // The one button of the block: it confirms it, and while the
                // confirmation stands it undoes it (ADR 187). No colour of its
                // own: the panel carries no green (ADR 188).
                text: root.approved ? qsTr("Revoke approval")
                     : (root.correctionLayerInView ? qsTr("Approve correction")
                                                   : qsTr("Approve block"))
                enabled: !Controller.checkBusy && !Controller.busy
                onClicked: root.approved ? Controller.revertBlockApproval()
                                         : Controller.approveSelectedBlock()
            }
            LLOButton {
                id: closeButton
                text: "\u2715"
                implicitWidth: 24
                implicitHeight: 22
                onClicked: Controller.selectedBoxIndex = -1
            }
        }

        // The recognized text: the OCR result and the verdict it earned.
        BlockVersionCard {
            Layout.fillWidth: true
            visible: root.isTextBlock && Controller.selectedBlockText.length > 0
            title: qsTr("Recognized text (OCR)")
            badgeText: root.statusWord(Controller.selectedBlockCheckStatus)
            badgeScore: root.layerScore(Controller.selectedBlockConfidence, Controller.selectedBlockCheckStatus)
            badgeColor: root.dotColor(Controller.selectedBlockCheckStatus)
            active: !root.correctionInView
            verified: Controller.selectedBlockCheckStatus === 1
                      || Controller.selectedBlockCheckStatus === 5
                      || Controller.selectedBlockCheckStatus === 6
            html: root.originalHtml
            plainText: Controller.selectedBlockText
            mono: root.monoBlock
            editable: !Controller.checkBusy && !Controller.busy
            onActivated: Controller.selectedBlockUsesCorrected = false
            onEdited: (text) => Controller.setSelectedBlockText(text, false)
        }

        // The correction layer: the universal model's fix, judged separately
        // from the text. A block the fixer confirmed carries no correction to
        // show.
        BlockVersionCard {
            Layout.fillWidth: true
            visible: root.isTextBlock && root.correctionStored
            title: qsTr("Corrected text (AI)")
            badgeText: root.statusWord(Controller.selectedBlockCorrectedCheckStatus)
            badgeScore: root.layerScore(Controller.selectedBlockCorrectedConfidence, Controller.selectedBlockCorrectedCheckStatus)
            badgeColor: root.dotColor(Controller.selectedBlockCorrectedCheckStatus)
            active: root.correctionInView
            verified: Controller.selectedBlockCorrectedCheckStatus === 1
                      || Controller.selectedBlockCorrectedCheckStatus === 5
                      || Controller.selectedBlockCorrectedCheckStatus === 6
            html: root.correctedHtml
            plainText: Controller.selectedBlockCorrected
            mono: root.monoBlock
            editable: !Controller.checkBusy && !Controller.busy
            onActivated: Controller.selectedBlockUsesCorrected = true
            onEdited: (text) => Controller.setSelectedBlockText(text, true)
        }
    }
}
