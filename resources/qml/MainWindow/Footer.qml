import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Item {
    id: root
    implicitHeight: column.implicitHeight
    width: parent ? parent.width : 0

    property bool launchDirty: false
    property bool bannerDismissed: false
    property var logWindow: null

    readonly property bool serverActive: Runtime.state === 2 || Runtime.state === 3

    signal openSettingsRequested(int tab)

    function requestRestart() {
        if (controller.busy) {
            restartConfirmDialog.open()
            return
        }
        doRestart()
    }

    function doRestart() {
        Runtime.restartServer()
        root.launchDirty = false
        root.bannerDismissed = false
    }

    function requestStop() {
        if (controller.busy) {
            stopConfirmDialog.open()
            return
        }
        doStop()
    }

    function doStop() {
        Runtime.stopServer()
    }

    function toggleToolTipText() {
        if (Runtime.lockedOut)
            return qsTr("The local runtime is owned by another LLocr instance.")
        if (root.serverActive)
            return qsTr("Stop the managed llama-server and unload the model.")
        if (!Runtime.configValid)
            return qsTr("Managed server is not fully configured — open "
                        + "Settings → Runtime.")
        return qsTr("Start the managed llama-server with the selected model.")
    }

    function dotColor(state) {
        switch (state) {
        case 2:  return Theme.warning // Starting — yellow
        case 3:  return Theme.success // Ready — green
        case 5:  return Theme.error   // Failed — red
        default: return Theme.nothing // Stopped / NotConfigured / Stopping — grey
        }
    }

    function stateText() {
        if (Settings.connectionMode === "external")
            return qsTr("External")
        switch (Runtime.state) {
        case 0:  return qsTr("Runtime: not configured")
        case 1:  return qsTr("Runtime: stopped")
        case 2:  return Runtime.statusMessage.length
                       ? Runtime.statusMessage
                       : qsTr("Runtime: starting…")
        case 3:  return qsTr("Runtime: ready")
        case 4:  return qsTr("Runtime: stopping…")
        case 5:  return Runtime.statusMessage.length
                       ? Runtime.statusMessage
                       : qsTr("Runtime: failed")
        default: return qsTr("Runtime: unknown")
        }
    }

    function markLaunchDirty() {
        root.launchDirty = true
    }

    Connections {
        target: Settings
        function onLaunchPortChanged() { root.markLaunchDirty() }
        function onLaunchCtxSizeChanged() { root.markLaunchDirty() }
        function onLaunchGpuLayersChanged() { root.markLaunchDirty() }
        function onLaunchThreadsChanged() { root.markLaunchDirty() }
        function onLaunchBatchSizeChanged() { root.markLaunchDirty() }
        function onLaunchParallelChanged() { root.markLaunchDirty() }
        function onLaunchFlashAttnChanged() { root.markLaunchDirty() }
        function onLaunchCacheTypeKChanged() { root.markLaunchDirty() }
        function onLaunchCacheTypeVChanged() { root.markLaunchDirty() }
        function onLaunchNoMmapChanged() { root.markLaunchDirty() }
        function onLaunchJinjaChanged() { root.markLaunchDirty() }
        function onLaunchExtraArgsChanged() { root.markLaunchDirty() }
        function onLaunchModelAliasChanged() { root.markLaunchDirty() }
        function onLaunchHostChanged() { root.markLaunchDirty() }
        function onLaunchModelPathChanged() { root.markLaunchDirty() }
        function onLaunchMmprojPathChanged() { root.markLaunchDirty() }
        function onLaunchPresetIdChanged() { root.markLaunchDirty() }
    }

    Connections {
        target: Runtime
        function onStateChanged() {
            if (Runtime.state === 3)
                root.launchDirty = false
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        Rectangle {//Restart banner
            Layout.fillWidth: true
            Layout.preferredHeight: banner.implicitHeight + 12
            visible: Runtime.state === 3 && root.launchDirty && !root.bannerDismissed
            color: Theme.surfaceAlt
            border.color: Theme.border
            border.width: 1

            RowLayout {
                id: banner
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing
                spacing: Theme.spacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textPrimary
                    text: qsTr("Launch settings changed — restart the server to apply them.")
                }
                LLOButton {
                    text: qsTr("Restart")
                    enabled: !Runtime.lockedOut
                    onClicked: root.requestRestart()
                }
                LLOButton {
                    text: qsTr("Hide")
                    flat: true
                    onClicked: root.bannerDismissed = true
                }
            }
        }//Rectangle

        Rectangle {//Toolbar row
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + 8
            color: Theme.surface

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing * 2
                spacing: Theme.spacingSmall

                Label {
                    text: controller.statusMessage
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: controller.busy || Runtime.busyState === 1
                    visible: running
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                LLOButton {
                    id: runtimeToggleButton
                    visible: Settings.connectionMode === "managed"
                    hoverEnabled: true
                    text: root.serverActive ? qsTr("Stop server") : qsTr("Start server")
                    enabled: !Runtime.lockedOut
                             && (root.serverActive
                                 || (Runtime.configValid
                                     && Runtime.state !== 4))
                    onClicked: {
                        if (root.serverActive)
                            root.requestStop()
                        else
                            Runtime.startServer()
                    }

                    ToolTip {
                        visible: runtimeToggleButton.hovered
                        delay: 600
                        font.pixelSize: Theme.fontCaption
                        text: root.toggleToolTipText()
                    }
                }

                MouseArea {//Managed-runtime indicator
                    id: runtimeBadge
                    Layout.preferredWidth: badgeRow.implicitWidth
                    Layout.preferredHeight: Theme.controlHeight
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (Settings.connectionMode === "managed"
                                && Runtime.state === 0) {
                            root.openSettingsRequested(4)
                        } else if (root.logWindow) {
                            root.logWindow.show()
                        }
                    }

                    ToolTip {
                        visible: runtimeBadge.containsMouse
                        text: qsTr("Server log — click to open. %1").arg(root.stateText())
                        delay: 600
                        font.pixelSize: Theme.fontCaption
                    }

                    RowLayout {
                        id: badgeRow
                        anchors.fill: parent
                        spacing: 6

                        Rectangle {
                            id: stateDot
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: root.dotColor(Runtime.state)
                            border.color: Theme.border
                            border.width: 1
                        }

                        Label {
                            id: stateLabel
                            font.pixelSize: Theme.fontCaption
                            color: Runtime.state === 5 ? Theme.error : Theme.textSecondary
                            text: root.stateText()
                            elide: Text.ElideRight
                        }
                    }
                }//MouseArea
            }//RowLayout
        }//Rectangle
    }//ColumnLayout

    Dialog {
        id: restartConfirmDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Restart server?")
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: 340
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
            text: qsTr("Recognition is in progress. Restarting the server will "
                       + "interrupt the current job. Continue?")
        }

        onAccepted: root.doRestart()
    }

    Dialog {
        id: stopConfirmDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Stop server?")
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: 340
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
            text: qsTr("Recognition is in progress. Stopping the server will "
                       + "interrupt the current job. Continue?")
        }

        onAccepted: root.doStop()
    }
}