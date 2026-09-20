pragma ComponentBehavior: Bound

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

    readonly property bool serverActive: Runtime.state === Runtime.Starting || Runtime.state === Runtime.Ready

    function requestRestart() {
        if (Controller.busy) {
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
        if (Controller.busy) {
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
        case Runtime.Starting:  return Theme.warning
        case Runtime.Ready:     return Theme.success
        case Runtime.Failed:    return Theme.error
        default: return Theme.nothing
        }
    }

    function stateText() {
        if (Settings.connectionMode === "external")
            return qsTr("External")
        switch (Runtime.state) {
        case Runtime.NotConfigured: return qsTr("Runtime: not configured")
        case Runtime.Stopped:       return qsTr("Runtime: stopped")
        case Runtime.Starting:      return Runtime.statusMessage.length
                                           ? Runtime.statusMessage
                                           : qsTr("Runtime: starting…")
        case Runtime.Ready:         return qsTr("Runtime: ready")
        case Runtime.Stopping:      return qsTr("Runtime: stopping…")
        case Runtime.Failed:        return Runtime.statusMessage.length
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
        function onLaunchModelAliasChanged() { root.markLaunchDirty() }
        function onLaunchHostChanged() { root.markLaunchDirty() }
        function onLaunchModelPathChanged() { root.markLaunchDirty() }
        function onLaunchMmprojPathChanged() { root.markLaunchDirty() }
        function onLaunchPresetIdChanged() { root.markLaunchDirty() }
        function onLaunchProfileIdChanged() { root.markLaunchDirty() }
        function onServerPathChanged() { root.markLaunchDirty() }
        // The check role is launched with its own paths/profile (ADR 72/74);
        // its changes are equally launch-relevant.
        function onCheckLaunchModelPathChanged() { root.markLaunchDirty() }
        function onCheckLaunchMmprojPathChanged() { root.markLaunchDirty() }
        function onCheckLaunchProfileIdChanged() { root.markLaunchDirty() }
    }

    Connections {
        target: LaunchProfilesOcr
        function onProfileChanged() { root.markLaunchDirty() }
    }

    Connections {
        target: LaunchProfilesValidate
        function onProfileChanged() { root.markLaunchDirty() }
    }

    Connections {
        target: Runtime
        function onStateChanged() {
            if (Runtime.state === Runtime.Ready)
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
            visible: Runtime.state === Runtime.Ready && root.launchDirty && !root.bannerDismissed
            color: Theme.surfaceAlt
            border.color: Theme.border
            border.width: 1

            RowLayout {
                id: banner
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
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

        LLOLabel {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing * 2
            Layout.rightMargin: Theme.spacing * 2
            visible: Controller.currentPageWarning.length > 0
            text: qsTr("Blank replacement — this page could not be decoded. %1")
                       .arg(Controller.currentPageWarning)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.error
        }

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

                LLOLabel {
                    text: Controller.statusMessage
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: Controller.busy || Controller.importing
                                                 || Runtime.busyState === Runtime.StartingRuntime
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
                                     && Runtime.state !== Runtime.Stopping))
                    onClicked: {
                        if (root.serverActive)
                            root.requestStop()
                        else
                            Runtime.startServer()
                    }

                    ToolTip {
                        visible: runtimeToggleButton.hovered
                        delay: 600
                        font.pointSize: Theme.captionSize
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
                        if (root.logWindow)
                            root.logWindow.show()
                    }

                    ToolTip {
                        visible: runtimeBadge.containsMouse
                        text: qsTr("Server log — click to open. %1").arg(root.stateText())
                        delay: 600
                        font.pointSize: Theme.captionSize
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

                        LLOLabel {
                            id: stateLabel
                            color: Runtime.state === Runtime.Failed ? Theme.error : Theme.textSecondary
                            text: root.stateText()
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
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
        width: 420
        title: qsTr("Restart server?")
        standardButtons: Dialog.Cancel | Dialog.Ok

        LLOLabel {
            width: parent.width
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
        width: 420
        title: qsTr("Stop server?")
        standardButtons: Dialog.Cancel | Dialog.Ok

        LLOLabel {
            width: parent.width
            color: Theme.textPrimary
            text: qsTr("Recognition is in progress. Stopping the server will "
                       + "interrupt the current job. Continue?")
        }

        onAccepted: root.doStop()
    }
}