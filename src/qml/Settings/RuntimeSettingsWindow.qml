pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window
    title: qsTr("Runtime settings")
    width: 860
    height: 680
    modality: Qt.NonModal

    // The combos in this window (mode, backend, release) apply immediately;
    // the snapshot makes Cancel able to undo them, like the text fields.
    property var applySnapshot: null

    function takeApplySnapshot() {
        applySnapshot = {
            connectionMode: Settings.connectionMode,
            backend: RuntimeInstaller.backend,
            selectedRelease: RuntimeInstaller.selectedRelease
        }
    }

    function restoreApplySnapshot() {
        if (!applySnapshot)
            return
        Settings.connectionMode = applySnapshot.connectionMode
        RuntimeInstaller.backend = applySnapshot.backend
        RuntimeInstaller.selectedRelease = applySnapshot.selectedRelease
    }

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        LLOButton {
            subtle: true
            text: qsTr("Restore defaults")
            onClicked: {
                Settings.resetRuntimeDefaults()
                rtTab.loadValues()
            }
        }
        Item { Layout.fillWidth: true }
        LLOButton {
            text: qsTr("Cancel")
            onClicked: {
                window.restoreApplySnapshot()
                rtTab.loadValues()
                window.close()
            }
        }
        LLOButton {
            emphasis: true
            text: qsTr("Save")
            onClicked: {
                rtTab.saveValues()
                Settings.forceSave()
                window.close()
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            window.takeApplySnapshot()
            Runtime.refreshSingleInstanceLock()
            rtTab.loadValues()
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        contentWidth: availableWidth
        contentHeight: rtTab.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        RuntimeTab {
            id: rtTab
            width: parent.width
        }
    }
}