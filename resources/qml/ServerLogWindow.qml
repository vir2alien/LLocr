import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// Read-only live view of the managed server log (ring-buffer tail). Non-modal:
// it can stay open while Start/Stop/Recognition run. Content refreshes
// automatically because it binds to Runtime.serverLog, whose change signal
// fires on every appended line.
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

    header: Item {
        implicitHeight: 38
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("llama-server log")
            font.bold: true
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
        }
    }

    ScrollView {
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

            // Keep the view pinned to the newest line.
            onTextChanged: {
                if (activeFocus === false)
                    cursorPosition = text.length
            }
        }
    }
}