pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr
import "../Common"

// The runtime section mixes a draft (the timeouts, the endpoint) with live
// writes (the connection mode, the backend, the release): the live ones are
// restored from the snapshot when the window is cancelled.
Item {
    id: root

    signal edited()

    property var applySnapshot: null

    function takeApplySnapshot() {
        root.applySnapshot = {
            connectionMode: Settings.connectionMode,
            backend: RuntimeInstaller.backend,
            selectedRelease: RuntimeInstaller.selectedRelease
        }
    }

    function restoreApplySnapshot() {
        if (!root.applySnapshot)
            return
        Settings.connectionMode = root.applySnapshot.connectionMode
        RuntimeInstaller.backend = root.applySnapshot.backend
        RuntimeInstaller.selectedRelease = root.applySnapshot.selectedRelease
    }

    function loadValues() {
        root.takeApplySnapshot()
        rtTab.loadValues()
        Runtime.refreshSingleInstanceLock()
    }

    function saveValues() {
        rtTab.saveValues()
    }

    function cancelValues() {
        root.restoreApplySnapshot()
        rtTab.loadValues()
    }

    function resetValues() {
        Settings.resetRuntimeDefaults()
        rtTab.loadValues()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: rtTab.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        RuntimeTab {
            id: rtTab
            width: parent.width
            onEdited: root.edited()
        }
    }
}
