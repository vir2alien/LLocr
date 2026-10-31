pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Rectangle {
    id: root
    color: Theme.surfaceAlt

    property string previewMarkdown: ""
    // The segmented switch's state: text editor or the Markdown preview.
    property bool previewActive: false

    readonly property var selectedRange: Controller.selectedBlockTextRange

    Timer {
        id: previewDebounce
        interval: 250
        onTriggered: {
            if (root.previewActive)
                Controller.requestPreviewMarkdown(textArea.text)
        }
    }

    Connections {
        target: Controller
        function onPreviewMarkdownReady(markdown) {
            root.previewMarkdown = markdown
        }
    }
    LLOLabel {
        anchors.centerIn: parent
        verticalAlignment: Text.AlignVCenter
        visible: !Controller.hasResult && !root.previewActive
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
                    size: 15
                    color: copyButton.enabled ? Theme.textPrimary : Theme.textMuted
                }
                onClicked: Controller.copyCurrentPageText()
            }

            SegmentedControl {
                id: viewSwitch
                labels: [qsTr("Text (MD)"), qsTr("Preview")]
                currentIndex: root.previewActive ? 1 : 0
                onActivated: (index) => root.previewActive = index === 1
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
            currentIndex: root.previewActive ? 1 : 0

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
                        previewDebounce.restart()
                        if (syncing || !activeFocus || text === modelText)
                            return
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
                            // Defer only while uncommitted typing is on the
                            // field; an idle field must show the stored text
                            // right away, or a revert looks like a no-op.
                            if (textArea.activeFocus && textArea.text !== textArea.modelText)
                                textArea.pendingReload = true
                            else
                                textArea.reload()
                        }
                        function onPageChanged() {
                            textArea.pendingReload = false
                            textArea.reload()
                        }
                    }
                }
            }

            Loader {
                active: root.previewActive
                onActiveChanged: if (active) previewDebounce.restart()
                sourceComponent: previewComponent
            }
        }//StackLayout
    }//ColumnLayout

    Connections {
        target: Controller
        function onSelectedBoxChanged() {
            const range = root.selectedRange
            if (range.length !== 2 || root.previewActive)
                return
            if (textArea.activeFocus)
                return
            textFlickable.ensureVisible(textArea.positionToRectangle(range[0]))
        }
    }

    Component {
        id: previewComponent
        MarkdownPreview {
            markdown: root.previewMarkdown
            dark: Theme.dark
            bgColor: Theme.surfaceAlt
            fgColor: Theme.textPrimary
        }
    }
}