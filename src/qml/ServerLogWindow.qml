pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "Common"

ApplicationWindow {
    id: root
    // The two sources of the runtime log: the server process writes the first
    // one, the client's request journal writes the second. Both are shown, in
    // every mode — the journal holds the requests to the local server and to
    // the external endpoints alike.
    title: qsTr("Runtime log")
    width: 720
    height: 560
    modality: Qt.NonModal

    // A method call in a binding is evaluated once and never again, so the
    // button follows the signal the log view sends instead of the path itself.
    property bool serverLogFileExists: false

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    Component.onCompleted: {
        root.serverLogFileExists = RuntimeLog.serverLogPath().length > 0
    }

    onVisibleChanged: {
        RuntimeLog.setLogViewVisible(visible)
        if (visible) {
            logScroll.autoScroll = true
            logScroll.scrollToBottom()
            journalScroll.autoScroll = true
            journalScroll.scrollToBottom()
        }
    }

    Connections {
        target: RuntimeLog
        function onServerLogChanged() {
            root.serverLogFileExists = RuntimeLog.serverLogPath().length > 0
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
                visible: root.serverLogFileExists
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
            LLOLabel {
                text: qsTr("%n request(s)", "", RuntimeLog.journalLineCount)
                color: Theme.textMuted
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: Theme.spacing

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("Server output")
                color: Theme.textMuted
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceSunken
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius

                Flickable {
                    id: logScroll
                    anchors.fill: parent
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
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.captionSize
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
                    text: qsTr("No log output yet.")
                    color: Theme.textMuted
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            spacing: Theme.spacingSmall

            LLOLabel {
                text: qsTr("Request journal")
                color: Theme.textMuted
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceSunken
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius

                Flickable {
                    id: journalScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: journalArea.contentWidth
                    contentHeight: journalArea.contentHeight

                    property bool autoScroll: true

                    function isAtBottom() {
                        return contentHeight - height - contentY < 24
                    }

                    function scrollToBottom() {
                        Qt.callLater(function () {
                            journalScroll.contentY =
                                Math.max(0, journalScroll.contentHeight - journalScroll.height)
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
                        id: journalArea
                        readOnly: true
                        wrapMode: TextEdit.NoWrap
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.captionSize
                        font.preferShaping: false
                        color: Theme.textPrimary
                        selectByMouse: true

                        Component.onCompleted: RuntimeLog.attachJournalView(textDocument)
                    }

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                }

                LLOLabel {
                    anchors.centerIn: parent
                    visible: RuntimeLog.journalLineCount === 0
                    text: qsTr("No requests yet.")
                    color: Theme.textMuted
                }
            }
        }
    }
}
