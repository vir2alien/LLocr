import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "Common"

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

            LLOLabel {
                text: qsTr("llama-server log")
                font.bold: true
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                id: liveDot
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                Layout.alignment: Qt.AlignVCenter
                radius: 4
                color: Theme.textMuted
                opacity: 0.5
            }
            LLOLabel {
                text: qsTr("live")
                color: Theme.textMuted
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

            LLOButton {
                text: qsTr("Copy log")
                onClicked: RuntimeLog.copyServerLog()
            }
            LLOButton {
                text: qsTr("Open directory")
                onClicked: RuntimeLog.openServerLogFolder()
            }
            LLOButton {
                text: qsTr("Clear view")
                onClicked: RuntimeLog.clearServerLog()
            }
            Item { Layout.fillWidth: true }
            LLOLabel {
                text: qsTr("%1 line(s)").arg(logArea.lineCount)
                color: Theme.textMuted
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 10
        clip: true

        ScrollBar.vertical: ScrollBar {
            id: vScroller
            policy: ScrollBar.AsNeeded
        }

        TextArea {
            id: logArea
            text: RuntimeLog.serverLog || qsTr("No log output yet.")
            readOnly: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pointSize: Theme.captionSize
            color: Theme.textPrimary
            selectByMouse: true

            background: Rectangle {
                color: Theme.surfaceSunken
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius
            }

            onTextChanged: {
                if (!root.userScrolledUp && vScroller.visible)
                    vScroller.position = 1.0 - vScroller.size
                if (root.liveDot && root.liveFlash) {
                    root.liveDot.color = Theme.success
                    root.liveDot.opacity = 1.0
                    root.liveFlash.restart()
                }
            }
        }
    }

    property bool userScrolledUp: false

    Connections {
        target: vScroller
        function onPositionChanged() {
            root.userScrolledUp =
                vScroller.visible
                && vScroller.position + vScroller.size < 0.99
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