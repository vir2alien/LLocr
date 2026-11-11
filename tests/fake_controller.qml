pragma Singleton

import QtQuick

// The controller double of the panel tests: the three panels read the
// document's state from the controller singleton, and the test needs a
// document with pages, a recognized page and an idle app. The double answers
// what the panels ask and nothing else — the real controller belongs to the
// app, and a test must not drive it.
QtObject {
    property bool hasImage: true
    property bool hasResult: true
    property bool busy: false
    property bool checkBusy: false
    property bool actionsIdle: true
    property int pageCount: 3
    property int currentPage: 0
    property int selectedPageCount: 0
    property int selectedBoxIndex: -1
    property bool currentPageEditable: true
    property bool currentPageEdited: false
    property int currentPageRecognitionMode: 3
    property int currentPageVerification: 0
    property int currentPageOrientationProblem: 0
    property int currentPageSuggestedTurn: 0
    property string resultText: ""
    property var selectedBlockTextRange: []
    property bool pageTextEditorFocused: false
    property var pageModel: pageModelStub

    readonly property QtObject pageModelStub: QtObject {
        function selectAll() {}
        function clearSelection() {}
        function isSelected(index) { return false }
        function applyPagesRange(text) {}
        function selectedPagesRange() { return "" }
    }

    function attachBlockTextHighlighter(document, color) {}
    function requestEditorMarkdown(text) {}
    function boxIndexForTextPosition(position) { return -1 }
    function setUserBlockText(ordinal, markdown) {}
    function moveBlock(from, to) {}
    function removeBlock(index) {}
    function copyCurrentPageText() {}
    function revertCurrentPageEdits() {}
    function rotatePage(degrees) {}
}
