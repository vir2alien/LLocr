pragma ComponentBehavior: Bound

import QtQuick

import LLocr

// One switch for the whole app: a compact row of exclusive segments where
// the active one is the filled one, so "what am I looking at" reads at a
// glance. A segment may carry an icon next to its label. The segments are
// drawn inset from the container, so the selection never pokes out of its
// corners. The control is driven from the outside — it reports the choice
// (activated(index)) and never writes currentIndex itself, so a binding to a
// stored setting survives the click.
Rectangle {
    id: root

    property var options: []
    property int currentIndex: 0

    signal activated(int index)

    readonly property int optionIconSize: 13
    readonly property int optionPadding: 10

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 24
    radius: Theme.controlRadius
    color: Theme.surfaceAlt
    border.color: Theme.divider
    border.width: 1

    function move(delta) {
        const count = root.options.length
        if (count === 0)
            return
        root.activated((root.currentIndex + delta + count) % count)
    }

    Row {
        id: row
        x: 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTiny

        Repeater {
            model: root.options

            delegate: Item {
                id: segment

                required property var modelData
                required property int index

                readonly property bool selected: segment.index === root.currentIndex
                readonly property string label: segment.modelData.label
                readonly property string icon: segment.modelData.icon !== undefined
                                              ? segment.modelData.icon : ""

                width: content.width + root.optionPadding * 2
                height: root.height - 4
                activeFocusOnTab: true

                Accessible.role: Accessible.RadioButton
                Accessible.name: segment.label
                Accessible.checked: segment.selected

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.controlRadius - 1
                    color: segment.selected ? Theme.selected
                                           : (segmentHover.hovered ? Theme.selected : "transparent")
                    border.color: segment.selected ? Theme.border : "transparent"
                    border.width: 1
                }

                Item {
                    id: content
                    anchors.centerIn: parent
                    width: (segment.icon !== "" ? root.optionIconSize + Theme.spacingSmall : 0)
                           + segmentLabel.implicitWidth
                    height: Math.max(root.optionIconSize, segmentLabel.implicitHeight)

                    Icon {
                        id: segmentIcon
                        visible: segment.icon !== ""
                        name: segment.icon
                        size: root.optionIconSize
                        color: Theme.textPrimary
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        Accessible.ignored: true
                    }

                    Text {
                        id: segmentLabel
                        text: segment.label
                        font: Theme.body
                        color: Theme.textPrimary
                        anchors.left: parent.left
                        anchors.leftMargin: segment.icon !== ""
                                            ? root.optionIconSize + Theme.spacingSmall : 0
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                HoverHandler {
                    id: segmentHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: root.activated(segment.index)
                }

                Keys.onLeftPressed: root.move(-1)
                Keys.onRightPressed: root.move(1)
                Keys.onSpacePressed: root.activated(segment.index)
                Keys.onReturnPressed: root.activated(segment.index)
            }
        }
    }
}
