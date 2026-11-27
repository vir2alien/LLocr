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
// The header names the block in the interface language and carries its state
// — the word and the dot — next to the action that takes the user's text back
// (ADR 338, ADR 339); the switch reads OCR | VLM | Разница and stands only
// where the block carries both layers (ADR 339). Every verdict has one
// place: the header of the text panel (ADR 239).
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
    // The block in view carries the user's approval (ADR 187).
    readonly property bool approved: effectiveStatus === 5
    // A block the user drew and nothing has been recognized in yet: it is
    // markup, not a recognized text (ADR 189). The page says the same about
    // itself — «Только разметка».
    readonly property bool markupOnly: effectiveStatus === 0
                                       && Controller.selectedBlockText.length === 0
                                       && Controller.selectedBlockCorrected.length === 0

    // The reader's outcomes (ADR 260): the block was written from the image,
    // so the panel says what the reader read — not what the fixer corrected.
    readonly property bool readConfirmed: Controller.selectedBlockFixStatus === 4 && !correctionStored
    readonly property bool readApplied: Controller.selectedBlockFixStatus === 5
    readonly property bool readDamaged: Controller.selectedBlockFixStatus === 6

    // The user wrote the block's text: in the WYSIWYG editor (the document
    // layer, ADR 296) or into the layer of the block itself. The block is the
    // user's now, and the model's verdict about the previous text does not
    // speak for it (ADR 338).
    readonly property bool userEdited: Controller.selectedBlockDocumentDiverged
                                      || effectiveStatus === 6
    readonly property bool showsStatus: !root.markupOnly || root.userEdited
    // The state of the block in the header: the word and the color of the
    // dot (ADR 338).
    readonly property string statusWord: root.showsStatus
                                        ? root.blockStatusWord(root.effectiveStatus) : ""
    readonly property color stateColor: root.blockStatusColor(root.effectiveStatus)

    function blockStatusWord(status) {
        if (root.userEdited)
            return qsTr("Edited")
        if (root.readDamaged)
            return qsTr("Damaged")
        if (root.readConfirmed)
            return qsTr("No errors")
        if (status === 7)
            return qsTr("Invalid syntax")
        if (status === 3)
            return qsTr("Damaged")
        if (status === 4)
            return qsTr("Errors")
        if (status === 1 || status === 5)
            return qsTr("No errors")
        return qsTr("Recognized")
    }
    function blockStatusColor(status) {
        if (root.userEdited)
            return Theme.warning
        if (root.readDamaged || status === 3 || status === 4 || status === 7)
            return Theme.error
        if (root.readConfirmed || status === 1 || status === 5)
            return Theme.success
        return Theme.nothing
    }

    // The document keeps the user's own wording for this block (ADR 296):
    // the panel says so in the header and offers to restore the block's text.
    readonly property bool documentDiverged: root.isTextBlock
                                             && Controller.selectedBlockDocumentDiverged

    // A formula, a table or a snippet is read in a monospace face: the
    // brackets, the subscripts and the spaces must not drift apart.
    readonly property bool monoBlock: {
        const label = Controller.selectedBlockLabel.toLowerCase()
        return label.indexOf("equation") >= 0 || label.indexOf("formula") >= 0
                || label.indexOf("code") >= 0 || label.indexOf("latex") >= 0
                || label.indexOf("math") >= 0 || label.indexOf("table") >= 0
    }

    // What the panel shows: 0 — the recognized text, 1 — the correction,
    // 2 — both, one above the other.
    property int viewIndex: 0
    readonly property bool compareMode: viewIndex === 2
    // The layer in view: the recognized text or the VLM's word. A block with
    // one layer only shows that layer, whatever the switch said before.
    readonly property bool showOriginal: root.isTextBlock
                                         && Controller.selectedBlockText.length > 0
                                         && (root.compareMode || root.viewIndex === 0 || !root.hasLayers)
    readonly property bool showCorrection: root.isTextBlock
                                           && root.correctionStored
                                           && (root.compareMode || root.viewIndex === 1 || !root.hasLayers)
    // A block with nothing to switch between: the markup alone, a picture, a
    // block with one layer only. The switch of the layers has nothing to say
    // and is gone.
    readonly property bool hasLayers: Controller.selectedBlockText.length > 0
                                     && root.correctionStored
    // A correction that is gone takes its segment with it.
    onCorrectionStoredChanged: {
        if (!root.correctionStored && root.viewIndex !== 0)
            root.viewIndex = 0
    }

    // The segment of the layer the document carries: the correction when the
    // block points at one, else the recognized text.
    readonly property int documentViewIndex: Controller.selectedBlockUsesCorrected
                                             && Controller.selectedBlockCorrected.length > 0 ? 1 : 0
    // The panel opens on the layer the document carries and follows it: the
    // fix and the read move the block in view to a fresh correction (ADR 185).
    readonly property int currentBox: Controller.selectedBoxIndex
    onCurrentBoxChanged: root.viewIndex = root.documentViewIndex
    onDocumentViewIndexChanged: root.viewIndex = root.documentViewIndex

    // The switch moves the document: the layer the operator reads is the layer
    // the document carries (ADR 164).
    function selectView(index) {
        if (index === 2) {
            root.viewIndex = 2
            return
        }
        root.viewIndex = index
        Controller.selectedBlockUsesCorrected = (index === 1)
    }

    // The header row's width budget: the actions on the right take their
    // share, the block label gives way to them.
    readonly property real rowWidth: root.width - 2 * Theme.spacingSmall
    readonly property real actionReserve:
          (root.switchInHeader && switchControl.visible ? switchControl.implicitWidth : 0)
        + (restoreButton.visible ? restoreButton.implicitWidth : 0)
        + closeButton.implicitWidth
        + Theme.spacingSmall * 6

    // The segments of the switch: the same switch in both places it may
    // stand.
    readonly property var switchOptions: [
        { label: qsTr("OCR") },
        { label: qsTr("VLM") },
        { label: qsTr("Difference") }
    ]

    // What the header row holds, if every word stands at its natural width:
    // the dot, the name of the block, the state, the switch and the actions.
    readonly property real headerNeed:
          Theme.spacing
        + 10
        + blockNameLabel.implicitWidth
        + root.stateRoom
        + (root.hasLayers ? switchControl.implicitWidth : 0)
        + (restoreButton.visible ? restoreButton.implicitWidth : 0)
        + closeButton.implicitWidth
        + Theme.spacingSmall * 5

    // The switch stays in the header row while the row holds it; when the row
    // cannot hold it, the switch goes to the line below and the row keeps the
    // name and the actions (ADR 323).
    readonly property bool switchInHeader: !root.hasLayers
                                          || root.headerNeed <= root.rowWidth

    // The room the word of the state takes: the name gives way to it.
    readonly property real stateRoom:
          (statusLabel.visible ? statusLabel.implicitWidth : 0)
        + Theme.spacingSmall * 3

    // The word on the card of a layer: what the pipeline state of that layer
    // is. A layer nobody judged yet carries no word — its title says what it
    // is.
    function layerStatusWord(status) {
        if (status === 1)
            return qsTr("Verified")
        if (status === 5)
            return qsTr("Approved")
        if (status === 6)
            return qsTr("Manually modified")
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
            return qsTr("Unbalanced") + " \\left / \\right"
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

    // The outcome of the last fix run (ADR 185). A fix that confirmed the
    // text stores nothing: there is no correction to show, and the header
    // says so.
    readonly property bool fixNotNeeded: Controller.selectedBlockFixStatus === 1 && !correctionStored

    // The text of a layer as HTML: the fragments the correction changed are
    // tinted, the rest is the plain text (ADR 186). The tinting belongs to
    // the comparison mode: a single layer is read clean.
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
                                                    root.compareMode ? Controller.selectedBlockRemovedRanges : [],
                                                    Theme.diffRemovedBg,
                                                    Theme.diffRemovedText)
    readonly property string correctedHtml: layerHtml(Controller.selectedBlockCorrected,
                                                     root.compareMode ? Controller.selectedBlockAddedRanges : [],
                                                     Theme.diffAddedBg,
                                                     Theme.diffAddedText)
    implicitHeight: checkColumn.implicitHeight + 2 * Theme.spacingSmall
    visible: Controller.selectedBoxIndex >= 0 && Controller.hasResult

    // The panel takes the clicks over its own background: a click that missed
    // a button stays inside the panel and does not reach the preview under it
    // (ADR 340). The area stands under the content, so the buttons and the
    // text fields keep their clicks.
    MouseArea {
        anchors.fill: parent
    }

    // Approve and move on: the operator's rhythm. The page text editor keeps
    // the Return — a shortcut that fires while a text is being typed in would
    // approve a block by accident.
    Shortcut {
        sequences: ["Ctrl+Return", "Meta+Return"]
        enabled: Controller.selectedBoxIndex >= 0
                 && !Controller.pageTextEditorFocused
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
            id: headerRow
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(closeButton.implicitHeight, switchControl.implicitHeight)
            spacing: Theme.spacingSmall

            Rectangle {
                id: statusDot
                objectName: "statusDot"
                // A block that carries no text has no status: neither the dot
                // nor the word (ADR 338).
                visible: root.showsStatus
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                // The dot stands as far from the left edge as it stands from
                // the top one — the header's insents are equal.
                Layout.leftMargin: Theme.spacing
                radius: 5
                color: root.stateColor
                border.color: root.stateColor
                border.width: 1
            }

            LLOLabel {
                id: blockNameLabel
                text: BlockNames.labelName(Controller.selectedBlockLabel)
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                // The row keeps room for the state, the switch and the
                // actions; the label gives way.
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(0, root.rowWidth - root.actionReserve - root.stateRoom)
            }

            LLOLabel {
                id: statusLabel
                visible: root.showsStatus
                text: root.statusWord
                color: Theme.textSecondary
            }

            LLOButton {
                // The action that takes the user's text back (ADR 296): it
                // stands with the yellow status, in the header.
                id: restoreButton
                visible: root.documentDiverged
                text: qsTr("Restore the block text")
                implicitHeight: switchControl.implicitHeight
                topPadding: 0
                bottomPadding: 0
                onClicked: Controller.releaseSelectedBlockUserText()
            }

            Item {
                Layout.fillWidth: true
            }

            SegmentedControl {
                id: switchControl
                // The switch is gone where there is nothing to switch between:
                // a block that carries the markup alone.
                visible: root.hasLayers && root.switchInHeader
                options: root.switchOptions
                currentIndex: root.viewIndex
                onActivated: (index) => root.selectView(index)
            }

            LLOButton {
                id: closeButton
                text: "✕"
                implicitWidth: 24
                implicitHeight: switchControl.implicitHeight
                topPadding: 0
                bottomPadding: 0
                onClicked: Controller.selectedBoxIndex = -1
            }
        }

        // The switch that did not fit the header row: it stands on the line
        // below and the header keeps the name and the actions.
        RowLayout {
            id: switchRow
            Layout.fillWidth: true
            visible: root.hasLayers && !root.switchInHeader
            spacing: Theme.spacingSmall

            SegmentedControl {
                id: switchBelow
                Layout.leftMargin: Theme.spacing
                options: root.switchOptions
                currentIndex: root.viewIndex
                onActivated: (index) => root.selectView(index)
            }
            Item {
                Layout.fillWidth: true
            }
        }

        // The recognized text: the OCR result and the verdict it earned.
        BlockTextPanel {
            id: originalPanel
            Layout.fillWidth: true
            visible: root.showOriginal
            title: qsTr("Recognized text (OCR)")
            noteText: root.fixNotNeeded ? qsTr("No correction required") : ""
            verdictText: root.layerStatusWord(Controller.selectedBlockCheckStatus)
            confidenceText: root.layerScore(Controller.selectedBlockConfidence, Controller.selectedBlockCheckStatus)
            verdictColor: root.dotColor(Controller.selectedBlockCheckStatus)
            // The document carries the correction only when there is one and
            // the block points at it; otherwise it carries the recognized
            // text.
            inDocument: !(Controller.selectedBlockUsesCorrected && root.correctionStored)
            html: root.originalHtml
            mono: root.monoBlock
        }

        // The correction layer: the universal model's fix, judged separately
        // from the text. A block the fixer confirmed carries no correction to
        // show.
        BlockTextPanel {
            id: correctionPanel
            Layout.fillWidth: true
            visible: root.showCorrection
            title: root.readApplied ? qsTr("Read text (AI)") : qsTr("Correction (AI)")
            verdictText: root.layerStatusWord(Controller.selectedBlockCorrectedCheckStatus)
            confidenceText: root.layerScore(Controller.selectedBlockCorrectedConfidence, Controller.selectedBlockCorrectedCheckStatus)
            verdictColor: root.dotColor(Controller.selectedBlockCorrectedCheckStatus)
            inDocument: Controller.selectedBlockUsesCorrected && root.correctionStored
            html: root.correctedHtml
            mono: root.monoBlock
            empty: !root.correctionStored
            emptyText: qsTr("No correction yet")
        }
    }
}
