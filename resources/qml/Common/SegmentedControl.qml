pragma ComponentBehavior: Bound

import QtQuick

// A compact exclusive switch between two views: the active segment is the
// filled one, so "what am I looking at" reads at a glance (a Switch leaves
// the inactive state unnamed). The segments are drawn inset from the
// container so the selection highlight never pokes out of its corners.
Rectangle {
    id: root

    property var labels: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 24
    radius: Theme.controlRadius
    color: Theme.surfaceAlt
    border.color: Theme.divider
    border.width: 1

    Row {
        id: row
        x: 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: root.labels

            delegate: Item {
                id: segment

                required property string modelData
                required property int index

                width: segmentLabel.implicitWidth + 20
                height: root.height - 4

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.controlRadius - 1
                    color: segment.index === root.currentIndex ? Theme.selected : "transparent"
                    border.color: segment.index === root.currentIndex ? Theme.border : "transparent"
                    border.width: 1
                }
                Text {
                    id: segmentLabel
                    anchors.centerIn: parent
                    text: segment.modelData
                    font: Theme.caption
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.currentIndex = segment.index
                        root.activated(segment.index)
                    }
                }
            }
        }
    }
}
