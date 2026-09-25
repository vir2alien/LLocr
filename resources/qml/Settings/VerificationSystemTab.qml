pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    signal edited()

    readonly property bool userModified:
        systemArea.text !== Verification.originalSystemPrompt()

    function loadValues() {
        systemArea.text = Verification.systemPrompt
    }

    function saveValues() {
        Verification.systemPrompt = systemArea.text
    }

    function revertOriginal() {
        systemArea.text = Verification.originalSystemPrompt()
        root.edited()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: qsTr("The system prompt sets the verification protocol. The verifier "
                       + "model answers with one of: OK — the block is correct; "
                       + "FIX followed by a newline and the complete corrected block; "
                       + "or REVIEW — the block is unreadable.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Item { Layout.fillWidth: true }
            LLOButton {
                subtle: true
                text: qsTr("Restore original prompt")
                visible: root.userModified
                onClicked: revertConfirmDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.dialogRadius
            color: Theme.surfaceAlt
            border.color: systemArea.activeFocus ? Theme.accent : Theme.divider
            border.width: systemArea.activeFocus ? 2 : 1

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth

                TextArea {
                    id: systemArea
                    width: parent.width
                    padding: 16
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    placeholderText: qsTr("System prompt for the verification model")
                    font.pointSize: Theme.bodySmallSize
                    onEditingFinished: {
                        if (systemArea.text !== Verification.systemPrompt)
                            root.edited()
                    }
                }
            }
        }
    }

    Dialog {
        id: revertConfirmDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 400
        title: qsTr("Restore original system prompt?")

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: qsTr("The customized system prompt will be replaced by the "
                       + "built-in one. The change is applied to the window "
                       + "draft and is stored only after Save.")
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.revertOriginal()
    }
}
