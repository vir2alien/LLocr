pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The manual choice of a version: a quiet button that opens the list of the
// releases. The list is the only place where the choice lives — the main
// flow does not carry a select (ADR 315).
Item {
    id: root

    implicitWidth: menuButton.implicitWidth
    implicitHeight: menuButton.implicitHeight

    Text {
        id: fontProbe
        visible: false
        font.pixelSize: Theme.bodySize
    }
    FontMetrics {
        id: metrics
        font: fontProbe.font
    }

    // The width of the list: the longest tag, the date and the marker of the
    // active build. The list is measured, not guessed — the tags differ.
    readonly property int listWidth: {
        let widest = 0
        for (let i = 0; i < RuntimeInstaller.releaseCount; ++i) {
            widest = Math.max(widest,
                              metrics.advanceWidth(RuntimeInstaller.releaseTag(i))
                              + metrics.advanceWidth(RuntimeInstaller.releaseDate(i))
                              + metrics.advanceWidth(qsTr("· active")))
        }
        return widest + 3 * Theme.spacingSmall + 2 * Theme.spacing
    }

    function open() {
        const margin = Theme.spacing
        const anchor = menuButton.mapToItem(versionMenu.parent, 0, menuButton.height + 2)
        versionMenu.x = Math.max(margin,
                                 Math.min(anchor.x, versionMenu.parent.width - versionMenu.width - margin))
        versionMenu.y = anchor.y + versionMenu.height + margin < versionMenu.parent.height
                ? anchor.y
                : Math.max(margin, menuButton.mapToItem(versionMenu.parent, 0, -versionMenu.height - 2).y)
        versionMenu.open()
    }

    LLOButton {
        id: menuButton
        anchors.fill: parent
        implicitWidth: 26
        text: ""
        subtle: true
        enabled: !RuntimeInstaller.busy
        onClicked: root.open()

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacingTiny
            Repeater {
                model: 3
                delegate: Rectangle {
                    width: 2.5
                    height: 2.5
                    radius: 1.25
                    color: !menuButton.enabled ? Theme.textMuted
                         : menuButton.hovered ? Theme.textPrimary : Theme.textSecondary
                }
            }
        }
    }

    Popup {
        id: versionMenu
        parent: Overlay.overlay
        padding: 2
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        width: Math.max(240, root.listWidth)

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius
        }

        contentItem: Column {
            id: versionMenuContent
            spacing: 0

            Repeater {
                model: RuntimeInstaller.releaseCount
                delegate: MenuItem {
                    required property int index

                    width: versionMenuContent.width
                    height: Theme.controlHeight
                    text: ""
                    onTriggered: {
                        versionMenu.close()
                        RuntimeInstaller.selectedRelease = index
                    }

                    contentItem: RowLayout {
                        spacing: Theme.spacingSmall

                        LLOLabel {
                            font.pixelSize: Theme.bodySize
                            color: Theme.textPrimary
                            text: RuntimeInstaller.releaseTag(index)
                        }
                        Item { Layout.fillWidth: true }
                        LLOLabel {
                            font.pixelSize: Theme.bodySize
                            color: Theme.textMuted
                            text: RuntimeInstaller.releaseDate(index)
                        }
                        LLOLabel {
                            visible: RuntimeInstaller.releaseIsActive(index)
                            font.pixelSize: Theme.bodySize
                            color: Theme.success
                            text: qsTr("· active")
                        }
                    }
                }
            }
        }
    }
}
