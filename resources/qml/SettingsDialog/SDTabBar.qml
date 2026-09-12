import QtQuick
import QtQuick.Controls

import LLocr


TabBar {
    implicitHeight: 28

    background: Rectangle {
        color: "transparent"
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }
    }

    component CustomTabButton: TabButton {
        id: tabBtn
        implicitHeight: 28
        padding: 4
        contentItem: Text {
            text: tabBtn.text
            font.pointSize: Theme.captionSize
            color: tabBtn.checked ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideNone
        }
        background: Rectangle {
            color: tabBtn.checked ? Theme.surface : Theme.surfaceSunken
            border.color: Theme.divider
            border.width: 1
            Rectangle {
                visible: tabBtn.checked
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.surface
            }
        }
    }

    CustomTabButton { text: qsTr("UI") }
    CustomTabButton { text: qsTr("Request") }
    CustomTabButton { text: qsTr("Output") }
    CustomTabButton { text: qsTr("Runtime") }

    CustomTabButton { text: qsTr("Launch") }

    CustomTabButton {
        text: qsTr("Models")
        onToggled: {
            if (checked) {
                ModelInstaller.reloadPresets()
                ModelInstaller.rescanRegistry()
            }
        }
    }
}
