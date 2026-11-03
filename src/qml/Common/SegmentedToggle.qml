pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import LLocr

// A row of exclusive choices inside one block: the block carries the
// background, the chosen segment wears a card of its own, and every segment
// of the block is the same size — the widest label sets the width of them
// all. The control is driven from the outside — it reports the choice and
// never sets currentIndex itself.
Rectangle {
    id: root

    property var options: []
    property int currentIndex: 0

    signal activated(int index)

    readonly property int optionIconSize: 13
    readonly property int optionPadding: Theme.spacingLarge
    readonly property int blockPadding: 3
    readonly property int blockRadius: 8

    implicitHeight: Theme.controlHeight + root.blockPadding * 2
    implicitWidth: root.segmentWidth * Math.max(1, root.options.length) + root.blockPadding * 2
    radius: root.blockRadius
    color: Theme.surfaceSunken
    border.color: Theme.border
    border.width: 1

    function move(delta) {
        const count = root.options.length
        if (count === 0)
            return
        root.activated((root.currentIndex + delta + count) % count)
    }

    // The icons belong to the block: either every option carries one or
    // none of them does, so the width of the icon goes into every segment.
    readonly property bool blockHasIcons: {
        for (let i = 0; i < root.options.length; ++i) {
            const option = root.options[i]
            if (option && option.icon !== undefined)
                return true
        }
        return false
    }

    // The widest label of the block sets the width of every segment: the
    // segments are equal, whatever their own text is. The measuring texts
    // are invisible — the block itself is the only thing to look at.
    Repeater {
        id: measurer

        model: root.options

        delegate: Text {
            required property var modelData

            visible: false
            font: Theme.bodySmall
            text: modelData.label
        }
    }

    readonly property real segmentWidth: {
        let widest = 0
        for (let i = 0; i < measurer.count; ++i) {
            const label = measurer.itemAt(i)
            if (label)
                widest = Math.max(widest, label.implicitWidth)
        }
        const extra = root.blockHasIcons ? root.optionIconSize + Theme.spacingSmall : 0
        return widest + extra + root.optionPadding * 2
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.blockPadding
        spacing: 0

        Repeater {
            model: root.options

            delegate: Rectangle {
                id: option

                required property var modelData
                required property int index

                readonly property bool selected: option.index === root.currentIndex

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: root.segmentWidth
                Layout.minimumWidth: 0
                radius: root.blockRadius - 1
                color: option.selected ? Theme.surfaceAlt
                                      : (optionHover.hovered ? Theme.selected : "transparent")
                border.color: option.selected ? Theme.border : "transparent"
                border.width: 1
                activeFocusOnTab: true

                Accessible.role: Accessible.RadioButton
                Accessible.name: option.modelData.label
                Accessible.checked: option.selected

                RowLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - Theme.spacing)
                    spacing: Theme.spacingSmall

                    Icon {
                        visible: option.modelData.icon !== undefined
                        name: option.modelData.icon !== undefined ? option.modelData.icon : ""
                        size: root.optionIconSize
                        Layout.preferredWidth: root.optionIconSize
                        Layout.preferredHeight: root.optionIconSize
                        color: option.selected ? Theme.textPrimary : Theme.textSecondary
                        Accessible.ignored: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: option.modelData.label
                        font: Theme.bodySmall
                        color: option.selected ? Theme.textPrimary : Theme.textSecondary
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                HoverHandler { id: optionHover }

                TapHandler {
                    onTapped: root.activated(option.index)
                }

                Keys.onLeftPressed: root.move(-1)
                Keys.onRightPressed: root.move(1)
                Keys.onSpacePressed: root.activated(option.index)
                Keys.onReturnPressed: root.activated(option.index)
            }
        }
    }
}
