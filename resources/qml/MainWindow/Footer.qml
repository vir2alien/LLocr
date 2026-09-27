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

    // The rule itself lives in C++ (Runtime.launchConfigDirty): the live server's
    // launch configuration is compared with the one the settings now produce, so
    // a setting added there cannot be forgotten here (ADR 113). QML keeps only the
    // presentational state — whether the user hid the banner for this session.
    readonly property bool launchDirty: Runtime.launchConfigDirty

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

        // A llama-server from a previous run kept the model (and the VRAM)
        // loaded: offer to get rid of it instead of silently starting a second
        // one (ADR 107). Never killed without a click.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: orphanBanner.implicitHeight + 12
            visible: Runtime.orphanDetected
            color: Theme.surfaceAlt
            border.color: Theme.warning
            border.width: 1

            RowLayout {
                id: orphanBanner
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    color: Theme.textPrimary
                    text: Runtime.orphanInfo
                    wrapMode: Text.Wrap
                }
                LLOButton {
                    text: qsTr("Stop it")
                    onClicked: Runtime.terminateOrphan()
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

        LLOLabel {//Parser diagnostic
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing * 2
            Layout.rightMargin: Theme.spacing * 2
            visible: Controller.parseWarning.length > 0
            text: Controller.parseWarning
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.warning
        }

        LLOLabel {//Check error
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing * 2
            Layout.rightMargin: Theme.spacing * 2
            visible: Controller.checkErrorMessage.length > 0
            text: Controller.checkErrorMessage
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

                LLOLabel {//Recognition status
                    text: Controller.statusMessage
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }
                LLOLabel {//Check status
                    visible: Controller.checkRunning || Controller.checkFinished
                    text: Controller.checkRunning
                          ? qsTr("Check %1 / %2").arg(Controller.checkProgressDone)
                                                   .arg(Controller.checkProgressTotal)
                          : qsTr("Check finished")
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: Controller.busy || Controller.importing
                                                 || Controller.checkBusy
                                                 || Runtime.busyState === Runtime.StartingRuntime
                    visible: running
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                LLOButton {
                    visible: Controller.checkBusy
                    enabled: Controller.checkBusy
                    text: qsTr("Stop")
                    onClicked: Controller.stopCheck()
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