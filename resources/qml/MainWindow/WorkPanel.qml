import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Rectangle {
    color: Theme.surfaceAlt
    Label {
        anchors.centerIn: parent
        verticalAlignment: Text.AlignVCenter
        visible: !controller.hasResult && !previewSwitch.checked
        text: qsTr("Recognized text will appear here")
        color: Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        width: Math.min(implicitWidth, parent.width - 2 * Theme.spacing)
    }

    ColumnLayout {
        anchors.fill: parent
        visible: controller.hasResult
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: previewSwitch.implicitHeight + 2*Theme.spacingSmall
            spacing: Theme.spacing
            Layout.margins: Theme.spacingSmall

            Label {
                visible: controller.currentPageEditable
                text: controller.currentPageEdited ? qsTr("Edited")
                                                   : qsTr("Recognized")
                color: controller.currentPageEdited ? Theme.textPrimary
                                                   : Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.bold: controller.currentPageEdited
            }

            Button {
                flat: true
                text: qsTr("Revert")
                visible: controller.currentPageEditable && controller.currentPageEdited
                onClicked: controller.revertCurrentPageEdits()
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
                    readOnly: !controller.currentPageEditable
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    background: null

                    property bool syncing: false

                    function reload() {
                        var t = controller.resultText
                        if (text === t)
                            return
                        syncing = true
                        text = t
                        syncing = false
                    }

                    onTextChanged: {
                        if (!syncing)
                            controller.setCurrentPageText(text)
                    }

                    Component.onCompleted: reload()

                    Connections {
                        target: controller
                        function onResultChanged() { textArea.reload() }
                    }
                }
            }

            // --- 1: Preview ---
            Loader {
                active: previewSwitch.checked
                sourceComponent: previewComponent
            }
        }
    }

    Component {
        id: previewComponent
        MarkdownPreview {
            markdown: controller.resolveImagesForPreview(textArea.text)
            dark: Theme.dark
            bgColor: Theme.surfaceAlt
            fgColor: Theme.textPrimary
        }
    }
}