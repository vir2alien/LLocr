import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: false

    signal externalChosen()

    function chooseLocal() {
        Settings.connectionMode = "managed"
        root.complete = true
    }

    function chooseExternal() {
        Settings.connectionMode = "external"
        Settings.setupVersion = 1
        Settings.setupDismissed = false
        root.externalChosen()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Welcome to LLM OCR")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("This assistant recognizes text from images and PDFs using a "
                       + "local LLM. Choose how to connect to a model.")
        }

        Item { implicitHeight: 6 }

        Repeater {
            model: 2
            Rectangle {
                width: root.width - 40
                height: 82
                radius: Theme.radius
                color: mouse.containsMouse ? Theme.surfaceAlt : Theme.surface
                border.color: Theme.border
                border.width: 1

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: index === 0 ? root.chooseLocal() : root.chooseExternal()
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    LLOLabel {
                        text: index === 0
                              ? qsTr("Local server (recommended)")
                              : qsTr("I already have a server or API")
                        font.bold: true
                        color: Theme.textPrimary
                    }
                    LLOLabel {
                        Layout.fillWidth: true
                        font.pointSize: Theme.captionSize
                        color: Theme.textMuted
                        text: index === 0
                              ? qsTr("LLM OCR downloads and runs llama.cpp locally. "
                                     + "Everything stays on this machine.")
                              : qsTr("Point to an existing OpenAI-compatible endpoint "
                                     + "and configure it in Settings.")
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}