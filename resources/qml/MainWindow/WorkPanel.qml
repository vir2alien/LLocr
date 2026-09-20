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

    Timer {
        id: previewDebounce
        interval: 250
        onTriggered:
            root.previewMarkdown = Controller.resolveImagesForPreview(textArea.text)
    }
    LLOLabel {
        anchors.centerIn: parent
        verticalAlignment: Text.AlignVCenter
        visible: !Controller.hasResult && !previewSwitch.checked
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
            Layout.preferredHeight: previewSwitch.implicitHeight + 2*Theme.spacingSmall
            spacing: Theme.spacing
            Layout.margins: Theme.spacingSmall

            LLOLabel {
                visible: Controller.currentPageEditable
                text: Controller.currentPageEdited ? qsTr("Edited")
                                                   : qsTr("Recognized")
                color: Controller.currentPageEdited ? Theme.textPrimary
                                                   : Theme.textMuted
                font.bold: Controller.currentPageEdited
            }

            LLOButton {
                text: qsTr("Revert")
                visible: Controller.currentPageEditable && Controller.currentPageEdited
                onClicked: Controller.revertCurrentPageEdits()
            }

            Item { Layout.fillWidth: true }
            Switch {
                id: previewSwitch
                Layout.alignment: Qt.AlignVCenter
                topPadding: 0
                bottomPadding: 0
                text: qsTr("Preview")
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
            currentIndex: previewSwitch.checked ? 1 : 0

            // --- 0: Edit ---
            ScrollView {
                TextArea {
                    id: textArea
                    readOnly: !Controller.currentPageEditable
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    background: null

                    property bool syncing: false

                    function reload() {
                        var t = Controller.resultText
                        if (text === t)
                            return
                        syncing = true
                        text = t
                        syncing = false
                    }

                    onTextChanged: {
                        if (!syncing)
                            Controller.setCurrentPageText(text)
                        previewDebounce.restart()
                    }

                    Component.onCompleted: reload()

                    Connections {
                        target: Controller
                        function onResultChanged() { textArea.reload() }
                    }
                }
            }

            // --- 1: Preview ---
            Loader {
                active: previewSwitch.checked
                onActiveChanged: if (active) previewDebounce.restart()
                sourceComponent: previewComponent
            }
        }

        // --- Bottom: selected-block verification panel ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: checkColumn.implicitHeight
                + 2 * Theme.spacingSmall
            visible: Controller.selectedBoxIndex >= 0 && Controller.hasResult
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1

            ColumnLayout {
                id: checkColumn
                anchors.fill: parent
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

                LLOLabel {
                    text: qsTr("Check prompt:")
                    color: Theme.textMuted
                }
                TextArea {
                    id: checkPrompt
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    wrapMode: TextArea.Wrap
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    placeholderText: qsTr("e.g. Fix recognition errors in the text. Return only the corrected text.")
                    text: qsTr("Check the text against the image and fix any errors. Return only the corrected text.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    LLOButton {
                        text: qsTr("Check")
                        // Recognition owns the connection/server too; a click
                        // during a run is silently refused in C++.
                        enabled: !Controller.checkBusy && !Controller.busy
                                && checkPrompt.text.trim().length > 0
                        onClicked: Controller.checkSelectedBlock(checkPrompt.text)
                    }
                    BusyIndicator {
                        visible: Controller.checkBusy
                        implicitWidth: 20
                        implicitHeight: 20
                        running: Controller.checkBusy
                    }
                    Item { Layout.fillWidth: true }
                    LLOButton {
                        text: qsTr("Apply fix")
                        visible: Controller.checkSucceeded
                        enabled: Controller.checkSucceeded && !Controller.busy
                        onClicked: Controller.applyCheckedText()
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

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    visible: Controller.checkResultText.length > 0
                    contentWidth: availableWidth
                    TextArea {
                        width: parent.width
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        color: Theme.textPrimary
                        background: null
                        text: Controller.checkResultText
                    }
                }

                LLOLabel {
                    visible: Controller.checkApplied
                    text: qsTr("Fix applied to the page text.")
                    color: Theme.textMuted
                }
            }
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