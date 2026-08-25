import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// SetupWizard → Step 1 "Welcome": choose between a managed local server
// (recommended) and an existing external server/API. Choosing External finishes
// the wizard immediately (setupVersion = 1) — the rest of the setup is unused.
Item {
    id: root

    // Whether "Local server" was chosen (enables the Next button).
    property bool complete: false

    // Emitted when the user picks the External path; the wizard handles finishing.
    signal externalChosen()

    function chooseLocal() {
        Settings.connectionMode = "managed"
        root.complete = true
    }

    function chooseExternal() {
        // §4.4 / ADR 31: an External profile counts as set up, no probing needed.
        Settings.connectionMode = "external"
        Settings.setupVersion = 1
        Settings.setupDismissed = false
        root.externalChosen()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Welcome to LLM OCR")
            font.pixelSize: Theme.fontTitle
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textSecondary
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
                    Label {
                        text: index === 0
                              ? qsTr("Local server (recommended)")
                              : qsTr("I already have a server or API")
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        color: Theme.textPrimary
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSmall
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