import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// SetupWizard → Step 5 "Done": summary. Marked complete when the wizard
// actually reaches this step / Finish is pressed (SetupWizard calls markDone()).
// Do NOT use Component.onCompleted here: StackLayout instantiates all children
// eagerly, so it would fire at application startup and suppress the wizard trigger.
Item {
    id: root

    property bool complete: true

    // The wizard calls this on the Finish click; also safe to call multiple times.
    function markDone() {
        Settings.setupVersion = 1
        Settings.setupDismissed = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Done")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
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

                LLOLabel {
                    text: qsTr("Summary")
                    font.bold: true
                    color: Theme.textPrimary
                }

                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Mode:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: qsTr("Local server (managed)"); font.pointSize: Theme.captionSize; color: Theme.textPrimary }
                }
                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Runtime:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.serverPath.trim(); font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Model:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.launchModelPath.trim(); font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Auto-start:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.autoStart ? qsTr("On") : qsTr("Off"); font.pointSize: Theme.captionSize; color: Theme.textPrimary }
                }
                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Port:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.launchPort > 0 ? String(Settings.launchPort) : qsTr("auto"); font.pointSize: Theme.captionSize; color: Theme.textPrimary }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}