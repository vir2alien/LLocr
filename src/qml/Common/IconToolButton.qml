pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// A button with a Lucide glyph before its caption; the highlight turns the
// caption into the warm accent (the «Исправить» badge). In stop mode the
// glyph gives way to the run's indicator — a spinning ring around a stop
// square — while the caption stays: the button keeps naming the action it
// started, and its click stops that run. The indicator takes the icon's
// place, not the caption's, so the button keeps its width in both modes.
ToolButton {
    id: iconButton

    property string iconName: ""
    property int iconSize: Theme.iconSizeMedium
    property bool highlight: false
    property bool stopMode: false

    font: Theme.button
    implicitHeight: 28
    implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
    leftPadding: 5
    rightPadding: 5
    Layout.minimumWidth: implicitWidth

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            // The icon's place: in stop mode the run's indicator stands here
            // and the caption beside it stays untouched.
            Item {
                id: iconSlot
                width: iconButton.iconSize
                height: iconButton.iconSize
                implicitWidth: width
                implicitHeight: height

                Icon {
                    anchors.centerIn: parent
                    name: iconButton.iconName
                    size: iconButton.iconSize
                    visible: iconButton.iconName.length > 0 && !iconButton.stopMode
                    color: iconButton.highlight ? Theme.accentAttention
                          : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
                }

                Item {
                    anchors.fill: parent
                    visible: iconButton.stopMode

                    // The ring is bigger than the icon it replaces: the
                    // indicator is the button's state, and it should be read
                    // at a glance. It keeps the icon's place as its center,
                    // so it grows symmetrically into the button's padding.
                    Icon {
                        id: stopRing
                        anchors.centerIn: parent
                        name: "loader-circle"
                        size: Theme.iconSizeIndicator
                        color: Theme.textSecondary

                        RotationAnimation on rotation {
                            from: 0
                            to: 360
                            duration: 900
                            loops: Animation.Infinite
                            running: iconButton.stopMode
                        }
                    }

                    // The stop sign in the ring's center: the shipped rounded
                    // square, drawn by the same component as the ring — a
                    // filled block would land on whole pixels and sit half a
                    // pixel off the ring's center.
                    Icon {
                        anchors.centerIn: stopRing
                        name: "square"
                        // Five eighths of the ring, rounded down to an even
                        // size: an odd square lands on half a pixel and
                        // renders off the center.
                        size: Math.floor(stopRing.width * 5 / 16) * 2
                        color: Theme.textPrimary
                    }
                }
            }

            Text {
                visible: iconButton.text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: iconButton.text
                font: iconButton.font
                color: iconButton.highlight ? Theme.accentAttention
                      : (iconButton.enabled ? Theme.textPrimary : Theme.textMuted)
            }
        }
    }
}
