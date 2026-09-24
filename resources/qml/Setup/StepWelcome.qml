pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // 0 = managed, download llama.cpp via the app
    // 1 = managed, user-specified llama.cpp binary
    // 2 = external server
    property int choice: -1
    property bool complete: choice >= 0

    function applyChoice() {
        if (root.choice === 2) {
            Settings.connectionMode = "external"
        } else {
            Settings.connectionMode = "managed"
            Settings.serverPathIsManaged = (root.choice === 0)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Choose how to work with the LLM.")
            font.bold: true
            color: Theme.textPrimary
        }

        Repeater {
            model: [
                {
                    title: qsTr("LLM OCR will manage the server (LLM OCR downloads llama.cpp)"),
                    hint: qsTr("A prebuilt llama.cpp is downloaded and updated by the app.")
                },
                {
                    title: qsTr("LLM OCR will manage the server (I will specify the llama.cpp binary)"),
                    hint: qsTr("You already have a llama-server binary on this machine.")
                },
                {
                    title: qsTr("I will run the server with models myself"),
                    hint: qsTr("Point to an existing OpenAI-compatible endpoint.")
                }
            ]

            delegate: Rectangle {
                id: choiceCard

                required property int index
                required property string title
                required property string hint

                Layout.fillWidth: true
                Layout.preferredHeight: 82
                radius: Theme.radius
                color: root.choice === choiceCard.index
                       ? Theme.surfaceSunken
                       : (mouse.containsMouse ? Theme.surfaceAlt : Theme.surface)
                border.color: root.choice === choiceCard.index
                              ? Theme.accent : Theme.border
                border.width: root.choice === choiceCard.index ? 2 : 1

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.choice = choiceCard.index
                        root.applyChoice()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    anchors.rightMargin: 20
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        LLOLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: choiceCard.title
                            font.bold: true
                            color: Theme.textPrimary
                        }
                        LLOLabel {
                            Layout.fillWidth: true
                            font.pointSize: Theme.captionSize
                            color: Theme.helpColor
                            text: choiceCard.hint
                        }
                    }

                    LLOLabel {
                        visible: root.choice === choiceCard.index
                        text: "\u2713"
                        font.bold: true
                        color: Theme.accent
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
