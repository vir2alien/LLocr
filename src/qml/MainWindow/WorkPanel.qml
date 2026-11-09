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
    property bool pendingEditorReload: false
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
        function onEditorMarkdownReady(markdown, blockMap) {
            root.editorMarkdown = markdown
            root.editorBlockMap = blockMap
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
            Layout.preferredHeight: viewSwitch.implicitHeight + 2*Theme.spacingSmall
            spacing: Theme.spacing
            Layout.margins: Theme.spacingSmall

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
                    // The panel defers reloads while it holds focus; a revert
                    // must show the stored text immediately, not on blur.
                    textArea.pendingReload = false
                    textArea.reload()
                    root.pendingEditorReload = false
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
                    readOnly: !Controller.currentPageEditable
                    textFormat: TextArea.PlainText
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    background: null

                    property bool syncing: false
                    // The text the model last served: a panel change that
                    // still matches it is an echo, not a user edit.
                    property string modelText: ""
                    // A rebuild that lands mid-edit (background re-check)
                    // must not clobber the panel: the reload waits until
                    // the field loses focus.
                    property bool pendingReload: false

                    function reload() {
                        var t = Controller.resultText
                        modelText = t
                        syncing = true
                        text = t
                        syncing = false
                    }

                    onTextChanged: {
                        if (syncing || !activeFocus || text === modelText)
                            return
                        // Real typing only: a programmatic reload (syncing)
                        // must not schedule a WYSIWYG push — the echo would
                        // setValue over in-flight keystrokes and clear the
                        // editor's undo stack.
                        editorDebounce.restart()
                        Controller.setCurrentPageText(text)
                        // A document-backed field re-emits textChanged for an
                        // already-handled edit when the next event is
                        // processed — a keystroke's late duplicate lands
                        // mid-click. The committed text is the model now:
                        // without this the replay re-commits over a revert,
                        // and «Отменить изменения» needs two clicks.
                        modelText = text
                    }

                    onActiveFocusChanged: {
                        // The panel's approve hotkey must not take the Return
                        // away from a text the user is typing in.
                        Controller.pageTextEditorFocused = activeFocus
                        if (!activeFocus && pendingReload) {
                            pendingReload = false
                            reload()
                        }
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
                            // The WYSIWYG editor defers its reload while the
                            // user is typing; the raw field follows the model
                            // right away, hidden or not — a hidden-but-stale
                            // field would encode the wrong page text.
                            const ed = editorLoader.item as MarkdownEditor
                            if (root.wysiwygActive) {
                                if (ed && ed.editorFocused)
                                    root.pendingEditorReload = true
                                else
                                    editorDebounce.restart()
                            }
                            if (textArea.activeFocus && textArea.text !== textArea.modelText)
                                textArea.pendingReload = true
                            else
                                textArea.reload()
                        }
                        function onPageChanged() {
                            textArea.pendingReload = false
                            textArea.reload()
                            const ed = editorLoader.item as MarkdownEditor
                            if (root.wysiwygActive) {
                                if (ed && ed.editorFocused)
                                    root.pendingEditorReload = true
                                else
                                    editorDebounce.restart()
                            }
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
            dark: Theme.dark
            bgColor: Theme.surfaceAlt
            fgColor: Theme.textPrimary
            hl: {
                const c = Theme.selected
                return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
                       + Math.round(c.b * 255) + "," + (Math.round(c.a * 255) / 255).toFixed(3) + ")"
            }
            onEditorTextChanged: (markdown) => Controller.setCurrentPageTextFromEditor(markdown)
            onBlockClicked: (blockIndex) => {
                if (blockIndex >= 0 && blockIndex < root.editorBlockMap.length && root.editorBlockMap[blockIndex] >= 0)
                    Controller.selectedBoxIndex = root.editorBlockMap[blockIndex]
            }
            // A rebuild that landed while the user typed in the WYSIWYG
            // editor reaches it when the editor loses focus — the same
            // deferral the raw field's pendingReload implements. (The
            // property's change signal carries no value; read it.)
            onEditorFocusedChanged: {
                if (!editor.editorFocused && root.pendingEditorReload) {
                    root.pendingEditorReload = false
                    editorDebounce.restart()
                }
            }
        }
    }
}