pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

RowLayout {
    id: root

    property bool busy: false
    property real progress: 0
    property bool cancelVisible: false

    signal cancelClicked()

    spacing: 6

    ProgressBar {
        Layout.fillWidth: true
        Layout.preferredHeight: 12
        visible: root.busy
        from: 0
        to: 1
        value: root.progress
    }

    LLOButton {
        text: qsTr("Cancel")
        visible: root.cancelVisible
        onClicked: root.cancelClicked()
    }
}
