import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// SetupWizard → Step 5 "Done": summary. Entering this step (or pressing Finish)
// marks the wizard as complete (setupVersion = 1).
Item {
    id: root

    property bool complete: true

    // The wizard calls this on the Finish click; also safe to call multiple times.
    function markDone() {
        Settings.setupVersion = 1
        Settings.setupDismissed = false
    }

    Component.onCompleted: root.markDone()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Done")
            font.pixelSize: Theme.fontTitle
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textSecondary
            text: qsTr("Your local runtime is ready. Drop an image or PDF onto the "
                       + "window to start recognizing.")
        }

        Item { Layout.preferredHeight: 8 }

        Frame {
            Layout.fillWidth: true
            padding: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("Summary")
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    color: Theme.textPrimary
                }

                RowLayout { spacing: 8
                    Label { text: qsTr("Mode:"); font.pixelSize: Theme.fontSmall; color: Theme.textSecondary }
                    Label { text: qsTr("Local server (managed)"); font.pixelSize: Theme.fontSmall; color: Theme.textPrimary }
                }
                RowLayout { spacing: 8
                    Label { text: qsTr("Runtime:"); font.pixelSize: Theme.fontSmall; color: Theme.textSecondary }
                    Label { text: Settings.serverPath.trim(); font.pixelSize: Theme.fontSmall; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout { spacing: 8
                    Label { text: qsTr("Model:"); font.pixelSize: Theme.fontSmall; color: Theme.textSecondary }
                    Label { text: Settings.launchModelPath.trim(); font.pixelSize: Theme.fontSmall; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout { spacing: 8
                    Label { text: qsTr("Auto-start:"); font.pixelSize: Theme.fontSmall; color: Theme.textSecondary }
                    Label { text: Settings.autoStart ? qsTr("On") : qsTr("Off"); font.pixelSize: Theme.fontSmall; color: Theme.textPrimary }
                }
                RowLayout { spacing: 8
                    Label { text: qsTr("Port:"); font.pixelSize: Theme.fontSmall; color: Theme.textSecondary }
                    Label { text: Settings.launchPort > 0 ? String(Settings.launchPort) : qsTr("auto"); font.pixelSize: Theme.fontSmall; color: Theme.textPrimary }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}