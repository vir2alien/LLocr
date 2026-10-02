import QtQuick
import QtQuick.Controls

// The "this will interrupt recognition" confirmation — the footer asks it
// before restarting or stopping the server.
Dialog {
    id: root

    property string prompt: ""
    signal confirmed()

    parent: Overlay.overlay
    modal: true
    width: 420
    standardButtons: Dialog.Cancel | Dialog.Ok

    LLOLabel {
        width: parent.width
        color: Theme.textPrimary
        text: root.prompt
    }

    onAccepted: root.confirmed()
}