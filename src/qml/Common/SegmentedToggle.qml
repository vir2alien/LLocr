pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import LLocr

// A row of exclusive choices, each one a button of its own: the chosen one
// wears the accent, so the current choice stands on the surface without
// opening anything. The control is driven from the outside — it reports the
// choice and never sets currentIndex itself.
RowLayout {
    id: root

    property var options: []
    property int currentIndex: 0

    signal activated(int index)

    readonly property int optionIconSize: 13
    readonly property int optionPadding: Theme.spacingLarge

    spacing: Theme.spacing

    function move(delta) {
        const count = root.options.length
        if (count === 0)
            return
        root.activated((root.currentIndex + delta + count) % count)
    }

    Repeater {
        model: root.options

        delegate: Rectangle {
            id: option

            required property var modelData
            required property int index

            readonly property bool selected: option.index === root.currentIndex

            implicitWidth: optionRow.implicitWidth + root.optionPadding
            Layout.preferredWidth: option.implicitWidth
            Layout.preferredHeight: Theme.controlHeight
            radius: Theme.controlRadius
            color: option.selected ? Theme.cardAccentBg
                                    : (optionHover.hovered ? Theme.surfaceSunken : Theme.surfaceAlt)
            border.color: option.selected ? Theme.cardAccent : Theme.border
            border.width: 1
            activeFocusOnTab: true

            Accessible.role: Accessible.RadioButton
            Accessible.name: option.modelData.label
            Accessible.checked: option.selected

            RowLayout {
                id: optionRow
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Icon {
                    visible: option.modelData.icon !== undefined
                    name: option.modelData.icon !== undefined ? option.modelData.icon : ""
                    size: root.optionIconSize
                    Layout.preferredWidth: root.optionIconSize
                    Layout.preferredHeight: root.optionIconSize
                    color: option.selected ? Theme.cardAccent : Theme.textSecondary
                    Accessible.ignored: true
                }

                Text {
                    text: option.modelData.label
                    font: Theme.bodySmall
                    color: option.selected ? Theme.cardAccent : Theme.textSecondary
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
