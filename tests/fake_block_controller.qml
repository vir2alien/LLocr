pragma Singleton

import QtQuick

// The controller double of the block panel test: the panel reads the selected
// block's state from the controller singleton, and the test needs a recognized
// block, an idle app and a document that diverges from the block (ADR 307).
// The double answers what the panel asks and nothing else — the real
// controller belongs to the app, and a test must not drive it.
QtObject {
    property bool hasResult: true
    property bool busy: false
    property bool checkBusy: false
    property bool pageTextEditorFocused: false
    property int selectedBoxIndex: 0
    property string selectedBlockLabel: "equation"
    property string selectedBlockText: "E = mc^2"
    property string selectedBlockCorrected: ""
    property bool selectedBlockUsesCorrected: false
    property int selectedBlockCheckStatus: 1
    property int selectedBlockCorrectedCheckStatus: 0
    property int selectedBlockFixStatus: 0
    property int selectedBlockSyntaxProblem: 0
    property real selectedBlockConfidence: 0.9
    property real selectedBlockCorrectedConfidence: 0
    property var selectedBlockRemovedRanges: []
    property var selectedBlockAddedRanges: []
    property bool highlightDifferences: false
    property bool currentPageEdited: true
    property bool selectedBlockDocumentDiverged: true

    // How often the panel asked to restore the block's text.
    property int restoreCalls: 0

    readonly property QtObject boxModel: QtObject {
        function isImageBox(index) { return false }
    }

    function releaseSelectedBlockUserText() { restoreCalls++ }
    function revertBlockCorrection() {}
    function approveSelectedBlock() {}
    function revertBlockApproval() {}
    function approveSelectedBlockAndSelectNext() {}
}
