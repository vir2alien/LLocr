pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "Common"

ApplicationWindow {
    id: root
    // The managed runtime owns a server process whose output is the log; the
    // external runtime writes nothing the app can read, so the window shows
    // the client's own request journal.
    readonly property bool externalMode: Settings.connectionMode !== "managed"
    title: externalMode ? qsTr("Request log") : qsTr("llama-server log")
    width: 640
    height: 420
    modality: Qt.NonModal

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    onVisibleChanged: {
        RuntimeLog.setLogViewVisible(visible)
        if (visible) {
            logScroll.autoScroll = true
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
                visible: !root.externalMode
                text: qsTr("Open directory")
                onClicked: RuntimeLog.openServerLogFolder()
            }
            LLOButton {
                text: qsTr("Clear view")
                onClicked: RuntimeLog.clearServerLog()
            }
            Item { Layout.fillWidth: true }
            LLOLabel {
                text: qsTr("%n line(s)", "", RuntimeLog.lineCount)
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

            Component.onCompleted: RuntimeLog.attachLogView(textDocument)
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: RuntimeLog.lineCount === 0
        text: root.externalMode ? qsTr("No requests yet.") : qsTr("No log output yet.")
        color: Theme.textMuted
    }
}
