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