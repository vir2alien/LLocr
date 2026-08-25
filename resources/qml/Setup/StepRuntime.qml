import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr

// SetupWizard → Step 2 "Runtime": obtain a llama-server binary. Either download
// and install one (via RuntimeInstaller) or point at an existing binary.
// The step is complete once `Settings.serverPath` points at a file.
Item {
    id: root

    // Can the user move forward?
    property bool complete: Settings.serverPath.trim().length > 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Runtime (llama.cpp)")
            font.pixelSize: Theme.fontTitle
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textSecondary
            text: qsTr("LLM OCR manages a local llama-server process. First obtain its "
                       + "binary — by downloading a prebuilt build or pointing to one "
                       + "you already have.")
        }

        // ----- Option A: install via RuntimeInstaller --------------------
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Download llama.cpp")
            font.pixelSize: Theme.fontCaption

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: qsTr("Backend")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
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

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    text: qsTr("Platform: %1 · recommended: %2")
                        .arg(RuntimeInstaller.platformLabel)
                        .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: RuntimeInstaller.state === 6 ? Theme.error
                         : (RuntimeInstaller.busy ? Theme.textSecondary : Theme.textMuted)
                    text: RuntimeInstaller.installedBuild.length
                          ? qsTr("Installed: %1 (%2)")
                                .arg(RuntimeInstaller.installedBuild)
                                .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.installedBackend))
                          : (RuntimeInstaller.state === 0
                             ? qsTr("Ready to install.")
                             : RuntimeInstaller.statusMessage)
                    visible: text.length > 0
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
                    Button {
                        text: RuntimeInstaller.state === 3 ? qsTr("Cancel")
                                                           : qsTr("Download and install")
                        implicitHeight: Theme.controlHeight
                        enabled: !(RuntimeInstaller.state === 1 || RuntimeInstaller.state === 4)
                        onClicked: RuntimeInstaller.state === 3
                                       ? RuntimeInstaller.cancelInstall()
                                       : RuntimeInstaller.startDownloadAndInstall()
                    }
                    Button {
                        text: qsTr("Check for updates")
                        implicitHeight: Theme.controlHeight
                        enabled: !RuntimeInstaller.busy
                        onClicked: RuntimeInstaller.checkForUpdates()
                    }
                    Item { Layout.fillWidth: true }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    // §7.8: fresh unsigned binaries may be blocked.
                    text: qsTr("On Windows, a freshly downloaded binary can be flagged by "
                               + "SmartScreen or antivirus; if launch fails, pick the file "
                               + "manually below.")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // ----- Option B: existing binary --------------------------------------
        Label {
            text: qsTr("Use an existing llama-server binary")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
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
            Button {
                text: qsTr("Browse…")
                implicitHeight: Theme.controlHeight
                onClicked: binaryPicker.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Probe")
                implicitHeight: Theme.controlHeight
                onClicked: Runtime.probeRuntimePath(Settings.serverPath.trim())
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Settings.serverPath.length ? Theme.textSecondary : Theme.textMuted
                text: Settings.serverPath.trim().length
                      ? Runtime.statusMessage
                      : qsTr("No binary selected yet.")
                elide: Text.ElideMiddle
            }
        }

        Item { Layout.fillHeight: true }
    }

    FileDialog {
        id: binaryPicker
        title: qsTr("Select llama-server binary")
        onAccepted: {
            Settings.serverPath = selectedFile
            pathField.text = selectedFile
        }
    }
}