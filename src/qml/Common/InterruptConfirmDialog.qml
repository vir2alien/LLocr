import QtQuick
import QtQuick.Controls

// The "this will interrupt recognition" confirmation — the footer asks it
// before restarting or stopping the server.
Dialog {
    id: root

    property string prompt: ""
    property color promptColor: Theme.textPrimary
    property string confirmText: qsTr("Continue")
    signal confirmed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 420
    standardButtons: Dialog.NoButton

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    contentItem: LLOLabel {
        width: root.availableWidth
        wrapMode: Text.WordWrap
        color: root.promptColor
        text: root.prompt
    }

    footer: DialogButtons {
        cancelText: qsTr("Cancel")
        acceptText: root.confirmText
        onAccepted: root.accept()
        onRejected: root.reject()
    }

    onAccepted: root.confirmed()
}