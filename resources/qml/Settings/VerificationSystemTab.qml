pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    function loadValues() {
        systemArea.text = Verification.systemPrompt
    }

    function saveValues() {
        Verification.systemPrompt = systemArea.text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            text: qsTr("The system prompt sets the verification protocol. The verifier "
                       + "model answers with one of: OK — the block is correct; "
                       + "FIX followed by a newline and the complete corrected block; "
                       + "or REVIEW — the block is unreadable.")
        }

        TextArea {
            id: systemArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: TextArea.Wrap
            selectByMouse: true
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            placeholderText: qsTr("System prompt for the verification model")
        }
    }
}