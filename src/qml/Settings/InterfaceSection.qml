pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The interface settings apply at once — the theme must be seen while it is
// picked — so the section keeps a snapshot and puts the old values back when
// the window is cancelled (ADR 297).
Item {
    id: root

    signal edited()

    property string snapshotLanguage: ""
    property int snapshotTheme: -1

    function loadValues() {
        root.snapshotLanguage = Settings.language
        root.snapshotTheme = UiController.mode
    }

    function saveValues() {
    }

    function cancelValues() {
        if (root.snapshotLanguage !== Settings.language) {
            Settings.language = root.snapshotLanguage
            I18n.setLanguage(Settings.language)
        }
        if (root.snapshotTheme !== UiController.mode)
            UiController.mode = root.snapshotTheme
    }

    function resetValues() {
        Settings.language = "system"
        I18n.setLanguage("system")
        UiController.mode = UiController.System
    }

    InterfaceSettings {
        anchors.fill: parent
        onEdited: root.edited()
    }
}
