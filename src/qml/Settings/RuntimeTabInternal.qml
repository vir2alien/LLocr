pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The managed runtime: the builds on disk and the updates that can be
// installed over them. The two are blocks of their own — what runs on the
// machine and where the next build comes from are two things.
ColumnLayout {
    id: root

    spacing: Theme.spacing

    signal edited()

    property var backendOptions: []

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
        syncChannelBox()
        syncBackendBox()
    }

    function syncChannelBox() {
        if (!channelBox)
            return
        channelBox.currentIndex = RuntimeInstaller.channel === "beta" ? 1 : 0
    }

    function syncBackendBox() {
        if (!backendBox)
            return
        const idx = RuntimeInstaller.availableBackends.indexOf(RuntimeInstaller.backend)
        backendBox.currentIndex = idx >= 0 ? idx : 0
    }

    Connections {
        target: RuntimeInstaller
        function onBackendChanged() {
            syncBackendBox()
        }
        function onChannelChanged() {
            syncChannelBox()
        }
    }

    // The builds on disk are a card of their own: what runs on the machine
    // is one thing, where the next build comes from is another.
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: buildsCard.implicitHeight + Theme.spacing * 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius + 2

        ColumnLayout {
            id: buildsCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            // The cleanup belongs to the section, not to the action row: it
            // acts on the installed builds, and the list right below shows them.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    text: qsTr("llama.cpp builds")
                    font.pixelSize: Theme.bodySize
                    font.bold: true
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

            LLOLabel {
                Layout.fillWidth: true
                font.pixelSize: Theme.footnoteSize
                color: Theme.textMuted
                text: qsTr("Platform: %1 · recommended backend: %2")
                    .arg(RuntimeInstaller.platformLabel)
                    .arg(RuntimeInstaller.backendDisplayName(RuntimeInstaller.recommendedBackend))
            }
        }
    }

    // The updates are a card of their own. One fact in one place: the
    // candidate is one row, the check lives in the header, and there is one
    // action — the button that starts the installation (ADR 315).
    Rectangle {
        id: updatesCard

        readonly property bool fetching: RuntimeInstaller.state === RuntimeInstaller.Fetching
        readonly property bool installing: RuntimeInstaller.state === RuntimeInstaller.Downloading
                                            || RuntimeInstaller.state === RuntimeInstaller.Installing
        readonly property bool failed: RuntimeInstaller.state === RuntimeInstaller.Error
        // Nothing to offer: the catalog never arrived, so there is no
        // candidate and no version to pick.
        readonly property bool catalogEmpty: RuntimeInstaller.releaseCount === 0
        // The candidate build is on disk already: there is nothing to
        // download, the build is switched to (ADR 317).
        readonly property bool candidateInstalled: RuntimeInstaller.candidateInstalled
        readonly property bool offered: RuntimeInstaller.hasUpdate
        // Nothing to install: the runtime is the latest build.
        readonly property bool upToDate: !updatesCard.offered
                                       && !updatesCard.failed && !updatesCard.fetching
                                       && !updatesCard.installing && !updatesCard.catalogEmpty

        Layout.fillWidth: true
        implicitHeight: updatesColumn.implicitHeight + Theme.spacing * 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        radius: Theme.controlRadius + 2

        // The candidate as one line: the tag, the date and the size of the
        // download. The facts belong to one place (ADR 315). The size is the
        // size of a download — a build that is on disk is not downloaded.
        function candidateText() {
            const parts = []
            if (RuntimeInstaller.candidateTag.length)
                parts.push(RuntimeInstaller.candidateTag)
            if (RuntimeInstaller.candidateDate.length)
                parts.push(RuntimeInstaller.candidateDate)
            if (RuntimeInstaller.candidateSize.length && !updatesCard.candidateInstalled)
                parts.push(RuntimeInstaller.candidateSize)
            return parts.join(" · ")
        }

        ColumnLayout {
            id: updatesColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            // The header: the card's name, and the check — when the catalog
            // was looked at and a compact button that looks again.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                LLOLabel {
                    text: qsTr("Updates")
                    font.pixelSize: Theme.bodySize
                    font.bold: true
                    color: Theme.textPrimary
                }
                Item { Layout.fillWidth: true }
                LLOLabel {
                    visible: RuntimeInstaller.checkedAtLabel.length > 0
                    text: qsTr("Checked at %1").arg(RuntimeInstaller.checkedAtLabel)
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textMuted
                }
                IconToolButton {
                    iconName: "rotate-cw"
                    text: ""
                    enabled: !RuntimeInstaller.busy
                    onClicked: RuntimeInstaller.checkForUpdates()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Check for updates")
                }
            }

            // The yellow alert is for the exceptions only: the check fell
            // back to the cache or to the beta list.
            Rectangle {
                Layout.fillWidth: true
                visible: RuntimeInstaller.catalogWarning.length > 0
                implicitHeight: warningText.implicitHeight + 2 * Theme.spacingSmall
                radius: Theme.controlRadius
                color: Theme.warningBg
                border.color: Theme.warning
                border.width: 1

                LLOLabel {
                    id: warningText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textPrimary
                    text: RuntimeInstaller.catalogWarning
                }
            }

            // The failure: the check or the installation. The reason comes
            // from the runtime layer, the button repeats the attempt.
            RowLayout {
                Layout.fillWidth: true
                visible: updatesCard.failed
                spacing: Theme.spacing

                Rectangle {
                    implicitWidth: errorBadge.implicitWidth + Theme.spacing * 2
                    implicitHeight: errorBadge.implicitHeight + Theme.spacingSmall * 2
                    radius: Theme.controlRadius
                    color: Theme.diffRemovedBg
                    border.color: Theme.error
                    border.width: 1

                    LLOLabel {
                        id: errorBadge
                        anchors.centerIn: parent
                        font.pixelSize: Theme.footnoteSize
                        color: Theme.error
                        text: updatesCard.catalogEmpty ? qsTr("Could not check")
                                                       : qsTr("Could not install")
                    }
                }
                LLOLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textSecondary
                    text: RuntimeInstaller.statusMessage
                }
                LLOButton {
                    text: qsTr("Retry")
                    onClicked: {
                        if (updatesCard.catalogEmpty)
                            RuntimeInstaller.checkForUpdates()
                        else
                            RuntimeInstaller.startDownloadAndInstall()
                    }
                }
            }

            // The check in flight: one quiet line, the same one the wizard
            // shows.
            LLOLabel {
                Layout.fillWidth: true
                visible: updatesCard.fetching
                font.pixelSize: Theme.footnoteSize
                color: Theme.textMuted
                text: RuntimeInstaller.statusMessage
            }

            // The candidate: one row with the version and the one action.
            RowLayout {
                Layout.fillWidth: true
                visible: updatesCard.offered
                spacing: Theme.spacingSmall

                LLOLabel {
                    text: updatesCard.candidateInstalled ? qsTr("Installed")
                          : qsTr("Available")
                    font.pixelSize: Theme.bodySize
                }

                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.pixelSize: Theme.bodySmallSize
                    color: Theme.textPrimary
                    text: updatesCard.candidateText()
                }

                LLOButton {
                    visible: updatesCard.offered
                    text: updatesCard.candidateInstalled ? qsTr("Activate") : qsTr("Update")
                    emphasis: true
                    enabled: !RuntimeInstaller.busy
                    onClicked: {
                        if (updatesCard.candidateInstalled)
                            RuntimeInstaller.activateCandidate()
                        else
                            RuntimeInstaller.startDownloadAndInstall()
                    }
                }
                LLOButton {
                    text: qsTr("Changes")
                    subtle: true
                    onClicked: RuntimeInstaller.openReleasePage()
                }
                UpdateVersionMenu {}
            }

            // Nothing to install: the runtime is the latest build.
            RowLayout {
                Layout.fillWidth: true
                visible: updatesCard.upToDate
                spacing: Theme.spacingSmall

                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.pixelSize: Theme.bodySmallSize
                    color: Theme.textSecondary
                    text: qsTr("Active %1 — this is the latest build.").arg(RuntimeInstaller.installedBuild)
                }
                UpdateVersionMenu {}
            }

            // The installation: the row becomes a progress bar with the one
            // action that stops it.
            RowLayout {
                Layout.fillWidth: true
                visible: updatesCard.installing
                spacing: Theme.spacingSmall

                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.pixelSize: Theme.bodySmallSize
                    color: Theme.textPrimary
                    text: RuntimeInstaller.state === RuntimeInstaller.Installing
                          ? qsTr("Installing %1…").arg(RuntimeInstaller.candidateTag)
                          : qsTr("Downloading %1…").arg(RuntimeInstaller.candidateTag)
                }
                LLOLabel {
                    font.pixelSize: Theme.bodySmallSize
                    color: Theme.textSecondary
                    text: Math.round(RuntimeInstaller.progress * 100) + "%"
                }
                LLOButton {
                    text: qsTr("Cancel")
                    onClicked: RuntimeInstaller.cancelInstall()
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
                visible: updatesCard.installing
                from: 0
                to: 1
                value: RuntimeInstaller.progress
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: updatesCard.offered && !updatesCard.candidateInstalled
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.textMuted
                text: qsTr("The installation runs in the background — you can keep working.")
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: Theme.spacingSmall
                color: Theme.divider
            }

            // The quiet settings under the action: where the builds come
            // from and for which backend.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                LLOLabel {
                    text: qsTr("Channel")
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textMuted
                }
                LLOComboBox {
                    id: channelBox
                    Layout.minimumWidth: 90
                    model: [qsTr("Stable"), qsTr("Beta")]
                    enabled: !RuntimeInstaller.busy
                    onActivated: {
                        RuntimeInstaller.channel = currentIndex === 1 ? "beta" : "stable"
                        root.edited()
                    }
                }

                LLOLabel {
                    text: qsTr("Backend")
                    font.pixelSize: Theme.footnoteSize
                    color: Theme.textMuted
                }
                LLOComboBox {
                    id: backendBox
                    Layout.minimumWidth: 110
                    model: root.backendOptions
                    enabled: !RuntimeInstaller.busy
                    onActivated: {
                        RuntimeInstaller.backend =
                            RuntimeInstaller.availableBackends[currentIndex]
                        root.edited()
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
