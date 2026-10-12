pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "Common"

ApplicationWindow {
    id: root
    title: qsTr("Problem log")
    width: 720
    height: 460
    modality: Qt.NonModal

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
            logScroll.scrollToBottom()
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: Rectangle {
        implicitHeight: headerRow.implicitHeight + 2 * 10
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: headerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Problem log")
                font.bold: true
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }

            LLOLabel {
                visible: Log.errorCount > 0
                text: qsTr("%n error(s)", "", Log.errorCount)
                color: Theme.error
            }
            LLOLabel {
                visible: Log.warningCount > 0
                text: qsTr("%n warning(s)", "", Log.warningCount)
                color: Theme.warning
            }
        }
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
                enabled: Log.count > 0
                onClicked: Log.copyLog()
            }
            LLOButton {
                text: qsTr("Clear log")
                enabled: Log.count > 0
                onClicked: Log.clear()
            }
            Item { Layout.fillWidth: true }
            LLOLabel {
                text: qsTr("%n entr(y/ies)", "", Log.count)
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
            // Wrapped, not elided: a decoder message is a paragraph, and
            // truncating it here would defeat the point of the window.
            wrapMode: TextEdit.Wrap
            font.family: "monospace"
            font.pointSize: Theme.captionSize
            font.preferShaping: false
            color: Theme.textPrimary
            selectByMouse: true

            Component.onCompleted: Log.attachLogView(textDocument)
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    LLOLabel {
        anchors.centerIn: parent
        visible: Log.count === 0
        text: qsTr("Nothing went wrong so far.")
        color: Theme.textMuted
    }
}
