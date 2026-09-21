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

        // --- Bottom: block verification panel ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: checkColumn.implicitHeight
                + 2 * Theme.spacingSmall
            visible: (Controller.selectedBoxIndex >= 0 || Controller.checkRunning)
                     && Controller.hasResult
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
                    // Status marker: green OK, yellow fixed, red review.
                    LLOLabel {
                        visible: Controller.selectedBlockCheckStatus !== 0
                        text: Controller.selectedBlockCheckStatus === 1 ? qsTr("OK")
                            : Controller.selectedBlockCheckStatus === 2 ? qsTr("Fixed")
                            : qsTr("Review")
                        color: Controller.selectedBlockCheckStatus === 1 ? Theme.success
                            : Controller.selectedBlockCheckStatus === 2 ? Theme.warning
                            : Theme.error
                        font.bold: true
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

                // Corrected (FIX) result, kept separate from the recognized text.
                LLOLabel {
                    visible: Controller.selectedBlockCheckStatus === 2
                    text: qsTr("Corrected by the verifier:")
                    color: Theme.textMuted
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    visible: Controller.selectedBlockCheckStatus === 2
                    contentWidth: availableWidth
                    TextArea {
                        width: parent.width
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        color: Theme.textPrimary
                        background: null
                        text: Controller.selectedBlockCorrected
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    LLOButton {
                        text: qsTr("Verify block")
                        enabled: !Controller.checkBusy && !Controller.busy
                        onClicked: Controller.checkSelectedBlock()
                    }
                    LLOButton {
                        text: qsTr("Check page")
                        enabled: !Controller.checkBusy && !Controller.busy
                                 && Controller.pageVerificationSupported
                        onClicked: Controller.checkEnabledBlocksOnPage()
                    }
                    BusyIndicator {
                        visible: Controller.checkBusy
                        implicitWidth: 20
                        implicitHeight: 20
                        running: Controller.checkBusy
                    }
                    LLOLabel {
                        visible: Controller.checkRunning
                        text: qsTr("%1 / %2").arg(Controller.checkProgressDone)
                                                  .arg(Controller.checkProgressTotal)
                        color: Theme.textMuted
                        font.pointSize: Theme.captionSize
                    }
                    Item { Layout.fillWidth: true }
                    LLOButton {
                        text: qsTr("Stop")
                        visible: Controller.checkBusy
                        enabled: Controller.checkBusy
                        onClicked: Controller.stopCheck()
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
            }//ColumnLayout
        }//Rectangle
    }//ColumnLayout

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