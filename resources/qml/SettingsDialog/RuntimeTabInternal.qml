import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ScrollView {
    contentWidth: availableWidth
    contentHeight: formLayout.implicitHeight

    property var backendOptions: []
    property var releaseOptions: []

    function buildInstallOptions() {
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
        Label {
            text: qsTr("llama-server binary")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
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
            Button {
                text: qsTr("Browse…")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: serverPicker.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Auto-detect")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: {
                    Settings.serverPath = Runtime.autoDiscoverPath()
                    serverPathField.text = Settings.serverPath
                }
            }
            Label {
                id: probeStatusLabel
                Layout.fillWidth: true
                text: Runtime.statusMessage.length
                      ? Runtime.statusMessage
                      : (Settings.serverPath.length
                         ? qsTr("Not probed yet")
                         : qsTr("No server binary selected"))
                elide: Text.ElideMiddle
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Settings.serverPath.length && !Runtime.lockedOut
                       ? Theme.textSecondary : Theme.textMuted
            }
        }

        Item { implicitHeight: 4 }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                text: qsTr("Check")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                onClicked: Runtime.probeRuntimePath(Settings.serverPath.trim())
            }
            Button {
                text: qsTr("Start")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: canManage && Runtime.state !== 2 && Runtime.state !== 3
                onClicked: Runtime.startServer()
            }
            Button {
                text: qsTr("Stop")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: canManage && (Runtime.state === 2 || Runtime.state === 3)
                onClicked: Runtime.stopServer()
            }
            Button {
                text: qsTr("Restart")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: canManage && Runtime.state === 3
                onClicked: Runtime.restartServer()
            }
            Item { Layout.fillWidth: true }
        }

        Button {
            text: qsTr("Show log")
            implicitHeight: Theme.controlHeight
            font.pixelSize: Theme.fontCaption
            onClicked: {
                if (dialog.logWindowRef)
                    dialog.logWindowRef.show()
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
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

        Label {
            text: qsTr("Install llama.cpp")
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: RuntimeInstaller.installedBuild.length
                  ? qsTr("Installed: %1 (%2)")
                        .arg(RuntimeInstaller.installedBuild)
                        .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.installedBackend))
                  : qsTr("No runtime installed yet")
        }

        Rectangle {
            Layout.fillWidth: true
            visible: RuntimeInstaller.hasUpdate
            implicitHeight: updatePlaque.implicitHeight + Theme.spacing
            color: Theme.warningBg
            border.color: Theme.warning
            border.width: 1
            radius: Theme.controlRadius

            ColumnLayout {
                id: updatePlaque
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    color: Theme.textPrimary
                    text: qsTr("A newer build %1 is available%2")
                        .arg(RuntimeInstaller.updateBuild())
                        .arg(RuntimeInstaller.updateTimestampLabel().length
                             ? qsTr(" (checked %1)").arg(RuntimeInstaller.updateTimestampLabel())
                             : "")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                    text: Runtime.state === 3
                          ? qsTr("Updating will install it after the running server is stopped.")
                          : qsTr("You can keep working — updating installs in the background.")
                }
                RowLayout {
                    spacing: 6
                    Button {
                        text: Runtime.state === 3 ? qsTr("Stop server and update")
                                                : qsTr("Update")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontCaption
                        enabled: !RuntimeInstaller.busy
                        onClicked: {
                            if (Runtime.state === 3)
                                Runtime.stopServer()
                            RuntimeInstaller.installUpdate()
                        }
                    }
                    Button {
                        text: qsTr("View changes")
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: Theme.fontCaption
                        onClicked: RuntimeInstaller.openReleasePage()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
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

            Label {
                text: qsTr("Backend")
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
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

            Label {
                text: qsTr("Release")
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
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

                Button {
                    text: qsTr("Check for updates")
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.fontCaption
                    enabled: !RuntimeInstaller.busy
                    onClicked: RuntimeInstaller.checkForUpdates()
                }
            }

        }

        Label {
            id: installStatusLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: RuntimeInstaller.state === 6 ? Theme.error
                 : (RuntimeInstaller.busy ? Theme.textSecondary : Theme.textMuted)
            text: RuntimeInstaller.state === 0
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
            Button {
                text: RuntimeInstaller.state === 3 ? qsTr("Cancel")
                                                   : qsTr("Download and install")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: !(RuntimeInstaller.state === 1 || RuntimeInstaller.state === 4)
                onClicked: {
                    if (RuntimeInstaller.state === 3)
                        RuntimeInstaller.cancelInstall()
                    else
                        RuntimeInstaller.startDownloadAndInstall()
                }
            }
            Button {
                text: qsTr("Clean up unused builds")
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontCaption
                enabled: !RuntimeInstaller.busy
                onClicked: RuntimeInstaller.cleanupUnusedBuilds()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
                text: RuntimeInstaller.state === 0
                      ? qsTr("Press “Check for updates” to see if a newer release is available.")
                      : (RuntimeInstaller.hasUpdate
                         ? qsTr("A newer release is available.")
                         : qsTr("Your runtime build is up to date."))
            }
        }
    }//ColumnLayout
}