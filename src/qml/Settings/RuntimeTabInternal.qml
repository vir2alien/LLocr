pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ScrollView {
    id: root
    contentWidth: availableWidth
    contentHeight: formLayout.implicitHeight

    property var backendOptions: []
    property var releaseOptions: []

    function loadValues() {
        buildInstallOptions()
    }

    function buildInstallOptions() {
        RuntimeInstaller.rescanInstalledBuilds()
        var b = []
        for (var bi = 0; bi < RuntimeInstaller.availableBackends.length; bi++)
            b.push(RuntimeInstaller.backendDisplayName(RuntimeInstaller.availableBackends[bi]))
        backendOptions = b
        // Reassigning the model resets the ComboBox to index 0, so the box is
        // synced from the source of truth — pushing its post-reset index back
        // into the installer pinned the first available backend on every
        // window open.
        syncBackendBox()

        var r = []
        for (var ri = 0; ri < RuntimeInstaller.releaseCount; ri++)
            r.push(RuntimeInstaller.releaseLabel(ri))
        releaseOptions = r
        // The same reset-to-zero applies to the release list, so the box is
        // synced from the source of truth as well.
        syncReleaseBox()
    }

    function syncBackendBox() {
        if (!backendBox)
            return
        const idx = RuntimeInstaller.availableBackends.indexOf(RuntimeInstaller.backend)
        backendBox.currentIndex = idx >= 0 ? idx : 0
    }

    function syncReleaseBox() {
        if (!releaseBox)
            return
        const idx = RuntimeInstaller.selectedRelease
        releaseBox.currentIndex = idx >= 0 && idx < releaseOptions.length ? idx : 0
    }

    Connections {
        target: RuntimeInstaller
        function onCatalogChanged() {
            var r = []
            for (var ri = 0; ri < RuntimeInstaller.releaseCount; ri++)
                r.push(RuntimeInstaller.releaseLabel(ri))
            releaseOptions = r
            syncReleaseBox()
        }
        function onBackendChanged() {
            syncBackendBox()
        }
        function onSelectedReleaseChanged() {
            syncReleaseBox()
        }
    }

    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: formLayout
        width: parent.width
        spacing: 4

        // The cleanup belongs to the section, not to the action row: it acts
        // on the installed builds, and the list right below shows them.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOLabel {
                Layout.fillWidth: true
                text: qsTr("Install llama.cpp")
                font.pixelSize: Theme.bodySize
                color: Theme.textPrimary
            }
            LLOButton {
                text: qsTr("Clean up unused builds")
                enabled: !RuntimeInstaller.busy
                onClicked: RuntimeInstaller.cleanupUnusedBuilds()
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: RuntimeInstaller.installedBuildCount === 0
            font.pixelSize: Theme.footnoteSize
            color: Theme.textMuted
            text: qsTr("No runtime installed yet")
        }

        LLOLabel {
            visible: RuntimeInstaller.installedBuildCount > 0
            text: qsTr("Installed builds")
            font: Theme.bodySmall
        }

        RuntimeBuildsList {
            id: buildsList
            visible: RuntimeInstaller.installedBuildCount > 0
            Layout.fillWidth: true
            // Not capped and not interactive: the surrounding ScrollView
            // scrolls the whole tab, so a long list just grows (a nested
            // interactive Flickable would trap the wheel).
            Layout.preferredHeight: buildsList.implicitHeight
            maxVisibleRows: -1
            scrollable: false
            onActionError: (msg) => buildsError.text = msg
        }

        LLOLabel {
            id: buildsError
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.footnoteSize
            color: Theme.error
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
                    font.pixelSize: Theme.footnoteSize
                    font.bold: true
                    color: Theme.textPrimary
                    text: qsTr("A newer build %1 is available%2")
                        .arg(RuntimeInstaller.updateBuild)
                        .arg(RuntimeInstaller.updateTimestampLabel.length
                             ? qsTr(" (checked %1)").arg(RuntimeInstaller.updateTimestampLabel)
                             : "")
                }
                LLOLabel {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textSecondary
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
            font.pixelSize: Theme.footnoteSize
            color: Theme.textMuted
            text: qsTr("Platform: %1 · recommended backend: %2")
                .arg(RuntimeInstaller.platformLabel)
                .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
        }

        InstallerStatusLabel {
            isError: RuntimeInstaller.state === RuntimeInstaller.Error
            busy: RuntimeInstaller.busy
            statusText: RuntimeInstaller.state === RuntimeInstaller.Idle
                  ? qsTr("Open this tab or press “Check for updates” to load releases.")
                  : RuntimeInstaller.statusMessage
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            visible: RuntimeInstaller.busy
            from: 0
            to: 1
            value: RuntimeInstaller.progress
        }

        // One row for the whole install: what to install, which release, and
        // the two actions.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Backend")
                font: Theme.bodySmall
            }
            LLOComboBox {
                id: backendBox
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                model: backendOptions
                enabled: !RuntimeInstaller.busy
                onActivated: RuntimeInstaller.backend =
                    RuntimeInstaller.availableBackends[currentIndex]
            }

            LLOLabel {
                text: qsTr("Release")
                font: Theme.bodySmall
            }
            LLOComboBox {
                id: releaseBox
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                model: releaseOptions
                enabled: !RuntimeInstaller.busy
                onActivated: RuntimeInstaller.selectedRelease = currentIndex
            }

            LLOButton {
                text: qsTr("Check for updates")
                enabled: !RuntimeInstaller.busy
                onClicked: RuntimeInstaller.checkForUpdates()
            }
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
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pixelSize: Theme.footnoteSize
            color: Theme.textMuted
            text: RuntimeInstaller.state === RuntimeInstaller.Idle
                  ? qsTr("Press “Check for updates” to see if a newer release is available.")
                  : (RuntimeInstaller.hasUpdate
                     ? qsTr("A newer release is available.")
                     : qsTr("Your runtime build is up to date."))
        }
    }//ColumnLayout
}
