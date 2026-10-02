import QtQuick
import QtQuick.Controls

TabBar {
    id: root

    required property var titles

    implicitHeight: 32

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

    component TabItem: TabButton {
        id: tabItem
        implicitHeight: 32
        padding: 12

        background: Rectangle { color: "transparent" }

        contentItem: Text {
            text: tabItem.text
            font.pointSize: Theme.bodySmallSize
            font.bold: tabItem.checked
            color: tabItem.checked ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideNone
        }

        indicator: Rectangle {
            visible: tabItem.checked
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            radius: 1
            color: Theme.accent
        }
    }

    Repeater {
        model: root.titles
        TabItem { text: modelData }
    }
}