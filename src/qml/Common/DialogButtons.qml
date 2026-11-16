pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import LLocr

// The footer of a confirmation dialog: the cancel and the primary action on
// the right, the same order and the same emphasis as the window footers.
// The platform style supplies its own buttons, and they carry the look of
// the system instead of the look of the app — the dialogs build the pair
// themselves.
Item {
    id: root

    property string cancelText: ""
    property string acceptText: ""
    property bool acceptEnabled: true

    signal accepted()
    signal rejected()

    implicitHeight: row.implicitHeight + Theme.spacingLarge

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.bottomMargin: Theme.spacing
        spacing: Theme.spacing

        Item { Layout.fillWidth: true }

        LLOButton {
            text: root.cancelText
            onClicked: root.rejected()
        }
        LLOButton {
            text: root.acceptText
            emphasis: true
            enabled: root.acceptEnabled
            onClicked: root.accepted()
        }
    }
}
