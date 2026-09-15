pragma ComponentBehavior: Bound

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

    Rectangle {
        anchors.fill: parent
        anchors.margins: 10
        color: Theme.surfaceSunken
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius
    }

    Flickable {
        id: logScroll
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        contentWidth: logArea.contentWidth
        contentHeight: logArea.contentHeight

        function scrollToBottom() {
            Qt.callLater(function () {
                logScroll.contentY =
                    Math.max(0, logScroll.contentHeight - logScroll.height)
            })
        }

        TextArea.flickable: TextArea {
            id: logArea
            text: root.visible ? (RuntimeLog.serverLog || qsTr("No log output yet.")) : ""
            readOnly: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pointSize: Theme.captionSize
            color: Theme.textPrimary
            selectByMouse: true

            onTextChanged: {
                if (logScroll.atYEnd)
                    logScroll.scrollToBottom()
                if (liveDot && liveFlash) {
                    liveDot.color = Theme.success
                    liveDot.opacity = 1.0
                    liveFlash.restart()
                }
            }
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    Timer {
        id: liveFlash
        interval: 500
        repeat: false
        onTriggered: {
            liveDot.color = Theme.textMuted
            liveDot.opacity = 0.5
        }
    }
}