pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ScrollView {
    contentWidth: availableWidth
    contentHeight: formLayout.implicitHeight

    property var logWindowRef: null

    property var backendOptions: []
    property var releaseOptions: []

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
        LLOLabel {
            text: qsTr("llama-server binary")
        }
        RowLayout {
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            LLOLabel {
                id: probeStatusLabel
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
        }

        Item { implicitHeight: 4 }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            LLOButton {
                text: qsTr("Check")
                onClicked: Runtime.probeRuntimePath(Settings.serverPath.trim())
            }
            LLOButton {
                text: qsTr("Start")
                enabled: canManage && Runtime.state !== Runtime.Starting && Runtime.state !== Runtime.Ready
                onClicked: Runtime.startServer()
            }
            LLOButton {
                text: qsTr("Stop")
                enabled: canManage && (Runtime.state === Runtime.Starting || Runtime.state === Runtime.Ready)
                onClicked: Runtime.stopServer()
            }
            LLOButton {
                text: qsTr("Restart")
                enabled: canManage && Runtime.state === Runtime.Ready
                onClicked: Runtime.restartServer()
            }
            Item { Layout.fillWidth: true }
        }

        LLOButton {
            text: qsTr("Show log")
            onClicked: {
                if (logWindowRef)
                    logWindowRef.show()
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Managed mode uses this binary to run a local llama-server. "
                       + "Recognition in External mode is unaffected.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        LLOLabel {
            text: qsTr("Install llama.cpp")
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
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
            visible: RuntimeInstaller.installedBuildCount > 0
            text: qsTr("Installed builds")
        }

        RuntimeBuildsList {
            id: buildsList
            visible: RuntimeInstaller.installedBuildCount > 0
            Layout.fillWidth: true
            // Not capped and not interactive: the surrounding ScrollView
            // scrolls the whole tab, so a long list just grows (a nested
            // interactive Flickable would trap the wheel).
            Layout.preferredHeight: RuntimeInstaller.installedBuildCount * 36
            scrollable: false
        }

        Rectangle {
            Layout.fillWidth: true
            visible: RuntimeInstaller.hasUpdate
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
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Platform: %1 · recommended backend: %2")
                .arg(RuntimeInstaller.platformLabel)
                .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
        }

        GridLayout {
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
}