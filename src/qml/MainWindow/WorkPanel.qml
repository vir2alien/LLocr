pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Rectangle {
    id: root
    color: Theme.surfaceAlt

    property string editorMarkdown: ""
    property var editorBlockMap: []
    property var editorBlockMd: []
    // The segmented switch's state: raw markdown editor or the WYSIWYG one.
    property bool wysiwygActive: false

    readonly property var selectedRange: Controller.selectedBlockTextRange

    Timer {
        id: editorDebounce
        interval: 250
        onTriggered: {
            if (root.wysiwygActive)
                Controller.requestEditorMarkdown(textArea.text)
        }
    }

    Connections {
        target: Controller
        function onEditorMarkdownReady(markdown, blockMap, blockMd) {
            // The markdown goes last: its change triggers the push, and the
            // push must carry the fragments of that same answer.
            root.editorBlockMap = blockMap
            root.editorBlockMd = blockMd
            root.editorMarkdown = markdown
        }
    }
    LLOLabel {
        anchors.centerIn: parent
        verticalAlignment: Text.AlignVCenter
        visible: !Controller.hasResult && !root.wysiwygActive
        text: qsTr("Recognized text will appear here")
        color: Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        width: Math.min(implicitWidth, parent.width - 2 * Theme.spacing)
    }

    ColumnLayout {
        anchors.fill: parent
        visible: Controller.hasResult
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            // The panel menus share one height (ADR 301) — this one is the
            // preview's, and its hairline below ends where the other two end.
            Layout.preferredHeight: Theme.panelMenuHeight
            spacing: Theme.spacing
            Layout.leftMargin: Theme.spacingSmall
            Layout.rightMargin: Theme.spacingSmall

            LLOLabel {
                visible: Controller.currentPageEditable
                text: Controller.currentPageEdited ? qsTr("Edited")
                                                   : PageStatus.statusText(Controller.currentPageRecognitionMode,
                                                                           Controller.currentPageVerification,
                                                                           Controller.currentPageOrientationProblem)
                color: Controller.currentPageEdited ? Theme.textPrimary
                           : Controller.currentPageOrientationProblem !== 0 ? Theme.error
                           : Theme.textMuted
                font.bold: Controller.currentPageEdited
            }

            // The turn the layout tokens advise for a sideways page (ADR 245):
            // one click applies it — the user turns, the app advises.
            LLOButton {
                readonly property int turn: Controller.currentPageSuggestedTurn
                visible: Controller.currentPageOrientationProblem === 1 && turn !== 0
                text: turn === 180 ? qsTr("Turn upside up")
                                   : turn === -90 ? qsTr("Turn left")
                                                  : qsTr("Turn right")
                onClicked: Controller.rotatePage(turn)
            }

            LLOButton {
                text: qsTr("Revert")
                visible: Controller.currentPageEditable && Controller.currentPageEdited
                onClicked: {
                    Controller.revertCurrentPageEdits()
                    editorDebounce.restart()
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                id: copyButton
                implicitWidth: 28
                implicitHeight: 26
                padding: 0
                enabled: Controller.hasResult
                Accessible.name: qsTr("Copy the page text")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy the page text")
                Icon {
                    anchors.centerIn: parent
                    name: "clipboard-copy"
                    size: Theme.iconSizeMedium
                    color: copyButton.enabled ? Theme.textPrimary : Theme.textMuted
                }
                onClicked: Controller.copyCurrentPageText()
            }

            SegmentedControl {
                id: viewSwitch
                options: [
                    { label: qsTr("Text (MD)") },
                    { label: qsTr("WYSIWYG") }
                ]
                currentIndex: root.wysiwygActive ? 1 : 0
                onActivated: (index) => root.wysiwygActive = index === 1
            }
        }// RowLayout text menu
        Rectangle {
            Layout.fillWidth: true;
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.wysiwygActive ? 1 : 0

            Flickable {
                id: textFlickable
                contentWidth: textArea.contentWidth
                contentHeight: textArea.contentHeight
                clip: true

                function ensureVisible(rect) {
                    if (rect.y < contentY)
                        contentY = rect.y
                    else if (rect.y + rect.height > contentY + height)
                        contentY = rect.y + rect.height - height
                }

                TextArea {
                    id: textArea
                    width: textFlickable.width
                    // The document is the join of the blocks (ADR 296): the
                    // raw pane is its read-only view — editing happens in the
                    // WYSIWYG tab, per block.
                    readOnly: true
                    textFormat: TextArea.PlainText
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    background: null

                    function reload() {
                        text = Controller.resultText
                    }

                    onActiveFocusChanged: {
                        // The panel's approve hotkey must not take the Return
                        // away from a text the user is typing in.
                        Controller.pageTextEditorFocused = activeFocus
                    }

                    Component.onCompleted: {
                        reload()
                        Controller.attachBlockTextHighlighter(textArea.textDocument, Theme.selected)
                    }

                    // The attach is idempotent (setColor on the existing
                    // highlighter): re-push on theme switches, whose color the
                    // one-shot attach would otherwise freeze.
                    Connections {
                        target: Theme
                        function onDarkChanged() {
                            Controller.attachBlockTextHighlighter(textArea.textDocument, Theme.selected)
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (eventPoint) => {
                            const position = textArea.positionAt(eventPoint.position.x, eventPoint.position.y)
                            const boxIndex = Controller.boxIndexForTextPosition(position)
                            if (boxIndex >= 0)
                                Controller.selectedBoxIndex = boxIndex
                        }
                    }

                    Connections {
                        target: Controller
                        function onResultChanged() {
                            // The push defers itself inside the page while
                            // the user is typing (editor.js dirty/stale).
                            if (root.wysiwygActive)
                                editorDebounce.restart()
                            textArea.reload()
                        }
                        function onPageChanged() {
                            textArea.reload()
                            if (root.wysiwygActive)
                                editorDebounce.restart()
                        }
                    }
                }
            }

            Loader {
                id: editorLoader
                active: root.wysiwygActive
                onActiveChanged: if (active) editorDebounce.restart()
                sourceComponent: editorComponent
            }
        }//StackLayout
    }//ColumnLayout

    Connections {
        target: Controller
        function onSelectedBoxChanged() {
            if (root.wysiwygActive) {
                const ed = editorLoader.item as MarkdownEditor
                if (ed)
                    ed.highlightBox(Controller.selectedBoxIndex)
                return
            }
            const range = root.selectedRange
            if (range.length !== 2)
                return
            if (textArea.activeFocus)
                return
            textFlickable.ensureVisible(textArea.positionToRectangle(range[0]))
        }
    }

    Component {
        id: editorComponent
        MarkdownEditor {
            id: editor

            markdown: root.editorMarkdown
            blockMap: root.editorBlockMap
            blockMd: root.editorBlockMd
            dark: Theme.dark
            bgColor: Theme.surfaceAlt
            fgColor: Theme.textPrimary
            hl: {
                const c = Theme.selected
                return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
                       + Math.round(c.b * 255) + "," + (Math.round(c.a * 255) / 255).toFixed(3) + ")"
            }
            onEditorBlockChanged: (blockOrdinal, markdown) => {
                if (blockOrdinal >= 0 && blockOrdinal < root.editorBlockMap.length
                    && root.editorBlockMap[blockOrdinal] >= 0)
                    Controller.setUserBlockText(blockOrdinal, markdown)
            }
            onBlockStructureRequested: (blockOrdinal, action) => {
                if (blockOrdinal < 0 || blockOrdinal >= root.editorBlockMap.length
                    || root.editorBlockMap[blockOrdinal] < 0)
                    return
                const boxIndex = root.editorBlockMap[blockOrdinal]
                if (action === "up")
                    Controller.moveBlock(boxIndex, boxIndex - 1)
                else if (action === "down")
                    Controller.moveBlock(boxIndex, boxIndex + 1)
                else if (action === "remove")
                    Controller.removeBlock(boxIndex)
            }
            onBlockClicked: (blockIndex) => {
                if (blockIndex >= 0 && blockIndex < root.editorBlockMap.length && root.editorBlockMap[blockIndex] >= 0)
                    Controller.selectedBoxIndex = root.editorBlockMap[blockIndex]
            }
        }
    }
}