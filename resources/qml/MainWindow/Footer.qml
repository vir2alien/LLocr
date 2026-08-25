import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// Application footer: recognition status on the left, the managed-runtime
// indicator on the right (§ G-UI). The indicator shows the runtime state as a
// colored dot + text, surfaces model-loading progress from stderr while the
// server starts, opens the log window on click, and displays a "restart
// required" banner when launch/* settings change while the server is Ready.
Item {
    id: root
    implicitHeight: column.implicitHeight
    width: parent ? parent.width : 0

    // The shared log window, owned by Main.qml and passed in.
    property var logWindow: null

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // ----- Restart banner (launch settings changed while Ready) --------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: banner.implicitHeight + 12
            visible: Runtime.state === 3 && root.launchDirty
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
                    onClicked: {
                        Runtime.restartServer()
                        root.launchDirty = false
                    }
                }
            }
        }

        // ----- Toolbar row --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight
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
                    running: controller.busy
                    visible: controller.busy
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                }

                // ----- Managed-runtime indicator (§ G-UI task 1) -----------
                MouseArea {
                    id: runtimeBadge
                    Layout.preferredWidth: badgeRow.implicitWidth
                    Layout.preferredHeight: Theme.controlHeight
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.logWindow)
                            root.logWindow.show()
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

                // Indeterminate progress while the server starts, mirroring the
                // stderr progress shown in the label (§ G-UI task 3).
                ProgressBar {
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 6
                    visible: Runtime.busyState === 1
                    indeterminate: true
                }
            }
        }
    }

    // True while any launch/* setting changed since the server last reached
    // Ready (or was restarted).
    property bool launchDirty: false

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
}