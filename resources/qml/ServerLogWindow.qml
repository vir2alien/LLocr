import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// Read-only live view of the managed server log (ring-buffer tail). Non-modal:
// it can stay open while Start/Stop/Recognition run. Content refreshes
// automatically because it binds to Runtime.serverLog, whose change signal
// fires on every appended line.
//
// §H.1: toolbar with "Copy log" (whole ring buffer → clipboard), "Open
// directory" (logs/ via the platform file manager) and "Clear view". New lines
// auto-scroll to the bottom unless the user has scrolled up to inspect
// earlier output.
ApplicationWindow {
    id: root
    title: qsTr("llama-server log")
    width: 640
    height: 420
    modality: Qt.NonModal

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: ToolBar {
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("llama-server log")
                font.bold: true
                font.pixelSize: Theme.fontNormal
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }

            // Live-update indicator: bright green right after an append, then
            // fades back to a dim neutral colour.
            Rectangle {
                id: liveDot
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: Theme.textMuted
                opacity: 0.5
            }
            Label {
                text: qsTr("live")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    footer: ToolBar {
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: Theme.spacingSmall

            Button {
                text: qsTr("Copy log")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: Runtime.copyServerLog()
            }
            Button {
                text: qsTr("Open directory")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: Runtime.openServerLogFolder()
            }
            Button {
                text: qsTr("Clear view")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: Runtime.clearServerLog()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1 line(s)").arg(logArea.lineCount)
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 10
        clip: true

        TextArea {
            id: logArea
            text: Runtime.serverLog || qsTr("No log output yet.")
            readOnly: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pixelSize: Theme.fontSmall
            color: Theme.textPrimary
            selectByMouse: true

            background: Rectangle {
                color: Theme.surfaceSunken
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius
            }

            onTextChanged: {
                // Auto-scroll to the newest line, unless the user is inspecting
                // earlier output.
                if (!root.userScrolledUp && scroll.verticalScrollBar.visible)
                    scroll.verticalScrollBar.position =
                        1.0 - scroll.verticalScrollBar.size
                // Flash the live indicator.
                root.liveDot.color = Theme.success
                root.liveDot.opacity = 1.0
                root.liveFlash.restart()
            }
        }
    }

    // True once the user scrolls away from the bottom (stops auto-following).
    property bool userScrolledUp: false

    Connections {
        target: scroll.verticalScrollBar
        function onPositionChanged() {
            root.userScrolledUp =
                scroll.verticalScrollBar.visible
                && scroll.verticalScrollBar.position
                     + scroll.verticalScrollBar.size < 0.99
        }
    }

    Timer {
        id: liveFlash
        interval: 500
        repeat: false
        onTriggered: {
            root.liveDot.color = Theme.textMuted
            root.liveDot.opacity = 0.5
        }
    }
}