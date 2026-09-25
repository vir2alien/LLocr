pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

ScrollView {
    id: root
    contentWidth: availableWidth
    contentHeight: formLayout.implicitHeight

    // true = "Download llama.cpp via app" variant (install UI only);
    // false = "Specify llama.cpp binary" variant (binary path only).
    property bool downloadMode: false

    property var backendOptions: []
    property var releaseOptions: []

    function loadValues() {
        // Re-assigning (not only the initial binding) — the user's editing
        // breaks the text binding, so a reset/reopen must restore the value.
        serverPathField.text = Settings.serverPath
        buildInstallOptions()
    }

    // The initial text binding is one-shot; external writers (install,
    // activate, restore defaults) must reach the field too. The focus guard
    // keeps mid-typing user input from being overridden.
    Connections {
        target: Settings
        function onServerPathChanged() {
            if (!serverPathField.activeFocus)
                serverPathField.text = Settings.serverPath
        }
    }

    function buildInstallOptions() {
        RuntimeInstaller.rescanInstalledBuilds()
        var b = []
        for (var bi = 0; bi < RuntimeInstaller.availableBackends.length; bi++)
            b.push(RuntimeInstaller.backendDisplayName(RuntimeInstaller.availableBackends[bi]))
        backendOptions = b
        if (backendBox && backendBox.currentIndex >= 0)
            RuntimeInstaller.backend =
                RuntimeInstaller.availableBackends[backendBox.currentIndex]

        var r = []
        for (var ri = 0; ri < RuntimeInstaller.releaseCount; ri++)
            r.push(RuntimeInstaller.releaseLabel(ri))
        releaseOptions = r
    }

    Connections {
        target: RuntimeInstaller
        function onCatalogChanged() {
            var r = []
            for (var ri = 0; ri < RuntimeInstaller.releaseCount; ri++)
                r.push(RuntimeInstaller.releaseLabel(ri))
            releaseOptions = r
            if (releaseBox)
                releaseBox.currentIndex = RuntimeInstaller.selectedRelease
        }
        function onBackendChanged() {
            if (backendBox) {
                var idx = RuntimeInstaller.availableBackends.indexOf(RuntimeInstaller.backend)
                backendBox.currentIndex = idx >= 0 ? idx : 0
            }
        }
        function onInstalledChanged() { }
    }

    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: formLayout
        width: parent.width
        spacing: 4

        // --- Variant 1: user-specified llama-server binary ----------------
        LLOLabel {
            visible: !root.downloadMode
            text: qsTr("llama-server binary")
        }
        RowLayout {
            visible: !root.downloadMode
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: serverPathField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                placeholderText: qsTr("path to llama-server")
                text: Settings.serverPath
                onEditingFinished: Settings.serverPath = text.trim()
            }
            LLOButton {
                text: qsTr("Browse…")
                onClicked: serverPicker.open()
            }
        }

        // Probe result (the check runs when a binary is picked).
        LLOLabel {
            id: probeStatusLabel
            visible: !root.downloadMode
            Layout.fillWidth: true
            text: Runtime.statusMessage.length
                  ? Runtime.statusMessage
                  : (Settings.serverPath.length
                     ? qsTr("Not probed yet")
                     : qsTr("No server binary selected"))
            elide: Text.ElideMiddle
            wrapMode: Text.NoWrap
            font.pointSize: Theme.captionSize
            color: Settings.serverPath.length && !Runtime.lockedOut
                   ? Theme.textSecondary : Theme.textMuted
        }

        // --- Variant 2: download llama.cpp via the app --------------------
        LLOLabel {
            visible: root.downloadMode
            text: qsTr("Install llama.cpp")
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            visible: root.downloadMode
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: RuntimeInstaller.installedBuild.length
                  ? qsTr("Installed: %1 (%2)")
                        .arg(RuntimeInstaller.installedBuild)
                        .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.installedBackend))
                  : qsTr("No runtime installed yet")
        }

        LLOLabel {
            visible: root.downloadMode && RuntimeInstaller.installedBuildCount > 0
            text: qsTr("Installed builds")
        }

        RuntimeBuildsList {
            id: buildsList
            visible: root.downloadMode && RuntimeInstaller.installedBuildCount > 0
            Layout.fillWidth: true
            // Not capped and not interactive: the surrounding ScrollView
            // scrolls the whole tab, so a long list just grows (a nested
            // interactive Flickable would trap the wheel).
            Layout.preferredHeight: buildsList.implicitHeight
            maxVisibleRows: -1
            scrollable: false
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.downloadMode && RuntimeInstaller.hasUpdate
            implicitHeight: updatePlaque.implicitHeight + 2 * 8
            color: Theme.warningBg
            border.color: Theme.warning
            border.width: 1
            radius: Theme.controlRadius

            ColumnLayout {
                id: updatePlaque
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                LLOLabel {
                    Layout.fillWidth: true
                    font.pointSize: Theme.captionSize
                    font.bold: true
                    color: Theme.textPrimary
                    text: qsTr("A newer build %1 is available%2")
                        .arg(RuntimeInstaller.updateBuild())
                        .arg(RuntimeInstaller.updateTimestampLabel().length
                             ? qsTr(" (checked %1)").arg(RuntimeInstaller.updateTimestampLabel())
                             : "")
                }
                LLOLabel {
                    Layout.fillWidth: true
                    text: Runtime.state === Runtime.Ready
                          ? qsTr("Updating will install it after the running server is stopped.")
                          : qsTr("You can keep working — updating installs in the background.")
                }
                RowLayout {
                    spacing: 6
                    LLOButton {
                        text: Runtime.state === Runtime.Ready ? qsTr("Stop server and update")
                                                              : qsTr("Update")
                        enabled: !RuntimeInstaller.busy
                        onClicked: {
                            if (Runtime.state === Runtime.Ready)
                                Runtime.stopServer()
                            RuntimeInstaller.installUpdate()
                        }
                    }
                    LLOButton {
                        text: qsTr("View changes")
                        onClicked: RuntimeInstaller.openReleasePage()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        LLOLabel {
            visible: root.downloadMode
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Platform: %1 · recommended backend: %2")
                .arg(RuntimeInstaller.platformLabel)
                .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
        }

        GridLayout {
            visible: root.downloadMode
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 4
            columnSpacing: 8

            LLOLabel {
                text: qsTr("Backend")
            }
            ComboBox {
                id: backendBox
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                model: backendOptions
                enabled: !RuntimeInstaller.busy
                onActivated: RuntimeInstaller.backend =
                    RuntimeInstaller.availableBackends[currentIndex]
            }

            LLOLabel {
                text: qsTr("Release")
            }
            RowLayout {
                ComboBox {
                    id: releaseBox
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    model: releaseOptions
                    enabled: !RuntimeInstaller.busy
                    onActivated: RuntimeInstaller.selectedRelease = currentIndex
                }

                LLOButton {
                    text: qsTr("Check for updates")
                    enabled: !RuntimeInstaller.busy
                    onClicked: RuntimeInstaller.checkForUpdates()
                }
            }

        }

        InstallerStatusLabel {
            visible: root.downloadMode
            id: installStatusLabel
            isError: RuntimeInstaller.state === RuntimeInstaller.Error
            busy: RuntimeInstaller.busy
            statusText: RuntimeInstaller.state === RuntimeInstaller.Idle
                  ? qsTr("Open this tab or press \u201cCheck for updates\u201d to load releases.")
                  : RuntimeInstaller.statusMessage
        }

        ProgressBar {
            id: installProgress
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            visible: root.downloadMode && RuntimeInstaller.busy
            from: 0
            to: 1
            value: RuntimeInstaller.progress
        }

        RowLayout {
            visible: root.downloadMode
            Layout.fillWidth: true
            spacing: 6
            LLOButton {
                text: RuntimeInstaller.state === RuntimeInstaller.Downloading ? qsTr("Cancel")
                                                                              : qsTr("Download and install")
                enabled: !(RuntimeInstaller.state === RuntimeInstaller.Fetching || RuntimeInstaller.state === RuntimeInstaller.Installing)
                onClicked: {
                    if (RuntimeInstaller.state === RuntimeInstaller.Downloading)
                        RuntimeInstaller.cancelInstall()
                    else
                        RuntimeInstaller.startDownloadAndInstall()
                }
            }
            LLOButton {
                text: qsTr("Clean up unused builds")
                enabled: !RuntimeInstaller.busy
                onClicked: RuntimeInstaller.cleanupUnusedBuilds()
            }
            LLOLabel {
                Layout.fillWidth: true
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: RuntimeInstaller.state === RuntimeInstaller.Idle
                      ? qsTr("Press “Check for updates” to see if a newer release is available.")
                      : (RuntimeInstaller.hasUpdate
                         ? qsTr("A newer release is available.")
                         : qsTr("Your runtime build is up to date."))
            }
        }
    }//ColumnLayout

    FileDialog {
        id: serverPicker
        title: qsTr("Select llama-server binary")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Executables (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            Settings.serverPath = path
            Runtime.probeRuntimePath(path)
        }
    }
}
