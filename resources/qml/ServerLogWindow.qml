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

    function logText() {
        return RuntimeLog.serverLog || qsTr("No log output yet.")
    }

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    onVisibleChanged: {
        if (visible) {
            logScroll.autoScroll = true
            logScroll.showText()
            logScroll.scrollToBottom()
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
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

        property bool autoScroll: true

        function isAtBottom() {
            return contentHeight - height - contentY < 24
        }

        function scrollToBottom() {
            Qt.callLater(function () {
                logScroll.contentY =
                    Math.max(0, logScroll.contentHeight - logScroll.height)
            })
        }

        function showText() {
            if (autoScroll) {
                logArea.text = root.logText()
                return
            }
            const x = contentX
            const y = contentY
            logArea.text = root.logText()
            // Assigning text resets the cursor to the start and TextArea.flickable
            // follows the cursor, scrolling the view to the top.
            Qt.callLater(function () {
                logScroll.contentX = x
                logScroll.contentY = y
            })
        }

        onContentYChanged: {
            if (dragging || moving)
                autoScroll = isAtBottom()
        }

        onContentHeightChanged: {
            if (autoScroll)
                scrollToBottom()
        }

        onHeightChanged: {
            if (autoScroll)
                scrollToBottom()
        }

        TextArea.flickable: TextArea {
            id: logArea
            readOnly: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pointSize: Theme.captionSize
            font.preferShaping: false
            color: Theme.textPrimary
            selectByMouse: true

            onTextChanged: {
                if (logScroll.autoScroll)
                    logScroll.scrollToBottom()
            }
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    Connections {
        target: RuntimeLog
        function onServerLogChanged() {
            if (root.visible)
                logScroll.showText()
        }
    }
}