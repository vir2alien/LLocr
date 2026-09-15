pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Settings.serverPath.trim().length > 0

    onVisibleChanged: {
        if (visible)
            RuntimeInstaller.rescanInstalledBuilds()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Runtime (llama.cpp)")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("LLM OCR manages a local llama-server process. First obtain its "
                       + "binary — by downloading a prebuilt build or pointing to one "
                       + "you already have.")
        }

        GroupBox {//Option A: install via RuntimeInstaller
            Layout.fillWidth: true
            title: qsTr("Download llama.cpp")
            font.pointSize: Theme.captionSize

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LLOLabel {
                        text: qsTr("Backend")
                    }
                    ComboBox {
                        id: backendBox
                        Layout.fillWidth: true
                        implicitHeight: Theme.controlHeight
                        model: RuntimeInstaller.availableBackends
                        enabled: !RuntimeInstaller.busy
                        onActivated: RuntimeInstaller.backend =
                            RuntimeInstaller.availableBackends[currentIndex]
                    }
                }

                LLOLabel {
                    Layout.fillWidth: true
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: qsTr("Platform: %1 · recommended: %2")
                        .arg(RuntimeInstaller.platformLabel)
                        .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
                }

                InstallerStatusLabel {
                    Layout.fillWidth: true
                    isError: RuntimeInstaller.state === RuntimeInstaller.Error
                    busy: RuntimeInstaller.busy
                    statusText: RuntimeInstaller.installedBuild.length
                          ? qsTr("Installed: %1 (%2)")
                                .arg(RuntimeInstaller.installedBuild)
                                .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.installedBackend))
                          : (RuntimeInstaller.state === RuntimeInstaller.Idle
                             ? qsTr("Ready to install.")
                             : RuntimeInstaller.statusMessage)
                    visible: statusText.length > 0
                }

                LLOLabel {
                    visible: RuntimeInstaller.installedBuildCount > 0
                    Layout.fillWidth: true
                    text: qsTr("Installed builds")
                    color: Theme.textPrimary
                }

                RuntimeBuildsList {
                    id: wizardBuildsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(RuntimeInstaller.installedBuildCount, 3) * 34
                    rowHeight: 34
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 12
                    visible: RuntimeInstaller.busy
                    from: 0
                    to: 1
                    value: RuntimeInstaller.progress
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LLOButton {
                        text: RuntimeInstaller.state === RuntimeInstaller.Downloading ? qsTr("Cancel")
                                                                                       : qsTr("Download and install")
                        enabled: !(RuntimeInstaller.state === RuntimeInstaller.Fetching || RuntimeInstaller.state === RuntimeInstaller.Installing)
                        onClicked: RuntimeInstaller.state === RuntimeInstaller.Downloading
                                       ? RuntimeInstaller.cancelInstall()
                                       : RuntimeInstaller.startDownloadAndInstall()
                    }
                    LLOButton {
                        text: qsTr("Check for updates")
                        enabled: !RuntimeInstaller.busy
                        onClicked: RuntimeInstaller.checkForUpdates()
                    }
                    Item { Layout.fillWidth: true }
                }

                LLOLabel {
                    Layout.fillWidth: true
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: qsTr("On Windows, a freshly downloaded binary can be flagged by "
                               + "SmartScreen or antivirus; if launch fails, pick the file "
                               + "manually below.")
                }
            }
        }//GroupBox

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Option B: existing binary
        LLOLabel {
            text: qsTr("Use an existing llama-server binary")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: pathField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                placeholderText: qsTr("path to llama-server")
                text: Settings.serverPath
                onEditingFinished: Settings.serverPath = text.trim()
            }
            LLOButton {
                text: qsTr("Browse…")
                onClicked: binaryPicker.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            LLOButton {
                text: qsTr("Probe")
                onClicked: Runtime.probeRuntimePath(Settings.serverPath.trim())
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Settings.serverPath.length ? Theme.textSecondary : Theme.textMuted
                text: Settings.serverPath.trim().length
                      ? Runtime.statusMessage
                      : qsTr("No binary selected yet.")
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
            }
        }

        Item { Layout.fillHeight: true }
    }

    FileDialog {
        id: binaryPicker
        title: qsTr("Select llama-server binary")
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            Settings.serverPath = path
        }
    }
}