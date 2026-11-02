import QtQuick
import QtQuick.Controls

// The "this will interrupt recognition" confirmation — the footer asks it
// before restarting or stopping the server.
Dialog {
    id: root

    property string prompt: ""
    property color promptColor: Theme.textPrimary
    signal confirmed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 420
    standardButtons: Dialog.Cancel | Dialog.Ok

    LLOLabel {
        width: parent.width
        color: root.promptColor
        text: root.prompt
    }

    onAccepted: root.confirmed()
}