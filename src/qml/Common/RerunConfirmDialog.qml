import QtQuick
import QtQuick.Controls

// The "results are already in scope" confirmation for the rerunnable runs —
// the footer asks whether to redo everything or to fill in only what is
// missing.
Dialog {
    id: root

    property string prompt: ""
    signal redoRequested()
    signal continueRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 420
    standardButtons: Dialog.NoButton

    LLOLabel {
        width: parent.width
        text: root.prompt
    }

    footer: DialogButtonBox {
        LLOButton {
            text: qsTr("Only what's not done")
            onClicked: {
                root.close()
                root.continueRequested()
            }
        }
        LLOButton {
            text: qsTr("Redo all")
            onClicked: {
                root.close()
                root.redoRequested()
            }
        }
        LLOButton {
            text: qsTr("Cancel")
            flat: true
            onClicked: root.reject()
        }
    }
}
