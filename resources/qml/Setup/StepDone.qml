pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: true

    function markDone() {
        Settings.setupVersion = 1
        Settings.setupDismissed = false
    }

    function summaryValue(text) {
        return text.trim().length > 0 ? text.trim() : qsTr("not set")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Summary")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: Settings.connectionMode === "external"
                  ? qsTr("Setup is complete. The app will use your external server; "
                         + "drop an image or PDF onto the window to start recognizing.")
                  : qsTr("Setup is complete. The app will run its own llama-server; "
                         + "drop an image or PDF onto the window to start recognizing.")
        }

        Item { Layout.preferredHeight: 8 }

        Frame {
            Layout.fillWidth: true
            padding: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                LLOLabel {
                    text: qsTr("Configuration")
                    font.bold: true
                    color: Theme.textPrimary
                }

                RowLayout { spacing: 8
                    LLOLabel { text: qsTr("Mode:"); font.pointSize: Theme.captionSize }
                    LLOLabel {
                        font.pointSize: Theme.captionSize; color: Theme.textPrimary
                        text: Settings.connectionMode === "external"
                              ? qsTr("External server")
                              : (Settings.serverPathIsManaged
                                 ? qsTr("Managed (downloaded llama.cpp)")
                                 : qsTr("Managed (own llama.cpp binary)"))
                    }
                }
                RowLayout {
                    visible: Settings.connectionMode !== "external"
                    spacing: 8
                    LLOLabel { text: qsTr("Runtime:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: root.summaryValue(Settings.serverPath); font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout {
                    visible: Settings.connectionMode === "external"
                    spacing: 8
                    LLOLabel { text: qsTr("Endpoint:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: root.summaryValue(Settings.baseUrl); font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle }
                }
                RowLayout {
                    spacing: 8
                    LLOLabel { text: qsTr("OCR model:"); font.pointSize: Theme.captionSize }
                    LLOLabel {
                        font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle; Layout.fillWidth: true
                        text: Settings.connectionMode === "external"
                              ? root.summaryValue(Settings.modelName)
                              : root.summaryValue(Settings.launchModelPath)
                    }
                }
                RowLayout {
                    spacing: 8
                    LLOLabel { text: qsTr("Check model:"); font.pointSize: Theme.captionSize }
                    LLOLabel {
                        font.pointSize: Theme.captionSize; color: Theme.textPrimary; elide: Text.ElideMiddle; Layout.fillWidth: true
                        text: Settings.connectionMode === "external"
                              ? root.summaryValue(Settings.checkModelName)
                              : root.summaryValue(Settings.checkLaunchModelPath)
                    }
                }
                RowLayout {
                    visible: Settings.connectionMode !== "external"
                    spacing: 8
                    LLOLabel { text: qsTr("Auto-start:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.autoStart ? qsTr("On") : qsTr("Off"); font.pointSize: Theme.captionSize; color: Theme.textPrimary }
                }
                RowLayout {
                    visible: Settings.connectionMode !== "external"
                    spacing: 8
                    LLOLabel { text: qsTr("Port:"); font.pointSize: Theme.captionSize }
                    LLOLabel { text: Settings.launchPort > 0 ? String(Settings.launchPort) : qsTr("auto"); font.pointSize: Theme.captionSize; color: Theme.textPrimary }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
