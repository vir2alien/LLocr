import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// Application footer: recognition status on the left, the managed-runtime
// controls on the right (§ G-UI). A Start/Stop toggle (Managed mode only)
// launches the llama-server with the selected model without opening Settings;
// next to it the indicator shows the runtime state as a colored dot + text,
// the busy spinner covers recognition and server startup, and the footer
// displays a "restart required" banner when launch/* settings change while
// the server is Ready.
Item {
    id: root
    implicitHeight: column.implicitHeight
    width: parent ? parent.width : 0

    // The shared log window, owned by Main.qml and passed in.
    property var logWindow: null

    // Asks the parent (Main.qml) to open Settings on the given tab index
    // (§H.1 empty-state navigation).
    signal openSettingsRequested(int tab)

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // ----- Restart banner (launch settings changed while Ready) --------
        Rectangle {
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
                Button {
                    text: qsTr("Restart")
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.fontCaption
                    enabled: !Runtime.lockedOut
                    onClicked: root.requestRestart()
                }
                // §H.1: the banner must remind, not block — allow hiding it for
                // the current session.
                Button {
                    text: qsTr("Hide")
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.fontCaption
                    flat: true
                    onClicked: root.bannerDismissed = true
                }
            }
        }

        // ----- Toolbar row --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            // Slightly taller than the controls so the 28 px busy spinner is
            // clearly visible instead of filling the row edge to edge.
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

                // Spinner for in-flight work: recognition (controller.busy)
                // or the managed server starting / loading the model. The
                // status label on the left carries the stderr-derived detail
                // ("Loading model… N%", §H.7). The dedicated footer progress
                // bar was dropped — llama.cpp's stderr percent updates too
                // coarsely to be informative, spinner + status text read
                // better.
                BusyIndicator {
                    running: controller.busy || Runtime.busyState === 1
                    visible: running
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }

                // ----- Managed start/stop toggle ----------------------------
                // Lets the user launch the managed llama-server (with the
                // selected model) straight from the main window, without
                // opening Settings → Runtime. Hidden in External mode.
                Button {
                    id: runtimeToggleButton
                    visible: Settings.connectionMode === "managed"
                    hoverEnabled: true
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: Theme.fontCaption
                    text: root.serverActive ? qsTr("Stop server")
                                            : qsTr("Start server")
                    // Starting/Ready → Stop is always available (unless the
                    // runtime is owned by another instance); Start needs a
                    // valid binary + model on disk (§3.8 configValid) and a
                    // server that is not already shutting down.
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

                // ----- Managed-runtime indicator (§ G-UI task 1) -----------
                MouseArea {
                    id: runtimeBadge
                    Layout.preferredWidth: badgeRow.implicitWidth
                    Layout.preferredHeight: Theme.controlHeight
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // §H.1 empty state: not-configured → Settings → Runtime.
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
                }
            }
        }
    }

    // The user asked to restart. Interrupting an in-flight recognition is
    // destructive, so confirm before restarting (§H.1.4).
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

    // Managed server is starting or ready → the footer toggle acts as Stop.
    readonly property bool serverActive: Runtime.state === 2
                                         || Runtime.state === 3

    // The user asked to stop. Same §H.1.4 courtesy as restart: confirm when a
    // recognition job is in flight, otherwise stop immediately.
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

    // Tooltip for the footer Start/Stop toggle: what it does, or why it is
    // disabled (not configured / runtime owned by another instance).
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

    // True while any launch/* setting changed since the server last reached
    // Ready (or was restarted).
    property bool launchDirty: false

    // §H.1: user dismissed the restart banner for this session.
    property bool bannerDismissed: false

    function dotColor(state) {
        switch (state) {
        case 2:  return "#e6b800"   // Starting — yellow
        case 3:  return Theme.success // Ready — green
        case 5:  return Theme.error   // Failed — red
        default: return "#8a8a8a"   // Stopped / NotConfigured / Stopping — grey
        }
    }

    function stateText() {
        if (Settings.connectionMode === "external")
            return qsTr("External")
        switch (Runtime.state) {
        case 0:  return qsTr("Runtime: not configured")
        case 1:  return qsTr("Runtime: stopped")
        case 2:  return Runtime.statusMessage.length
                       ? Runtime.statusMessage          // "loading model … 43%" from stderr
                       : qsTr("Runtime: starting…")
        case 3:  return qsTr("Runtime: ready")
        case 4:  return qsTr("Runtime: stopping…")
        case 5:  return Runtime.statusMessage.length
                       ? Runtime.statusMessage          // §7.5 error message
                       : qsTr("Runtime: failed")
        default: return qsTr("Runtime: unknown")
        }
    }

    // Any launch/* change while the server is Ready must trigger the banner.
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

    // Clear the flag once the server reaches Ready with the current parameters
    // (restart or manual start).
    Connections {
        target: Runtime
        function onStateChanged() {
            if (Runtime.state === 3)
                root.launchDirty = false
        }
    }

    // Confirmation before a server restart that would interrupt a running
    // recognition job. Restart only needs confirming when a job is in flight;
    // otherwise it applies immediately.
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

    // Stopping the server while a recognition job is running is destructive
    // (the job loses its connection); confirm, mirroring restartConfirmDialog.
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