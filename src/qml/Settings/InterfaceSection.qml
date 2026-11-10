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

    // The snapshot is armed once per session: the window is shown and hidden
    // again inside one session (the wizard steps aside), and a reload must not
    // take the undo with it.
    function beginSession() {
        root.snapshotLanguage = Settings.language
        root.snapshotTheme = UiController.mode
    }

    // The controls read the language and the theme from the settings
    // themselves, so there is nothing to load here.
    function loadValues() {
    }

    function saveValues() {
    }

    function cancelValues() {
        if (root.snapshotLanguage !== "" && root.snapshotLanguage !== Settings.language) {
            Settings.language = root.snapshotLanguage
            I18n.setLanguage(Settings.language)
        }
        if (root.snapshotTheme >= 0 && root.snapshotTheme !== UiController.mode)
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
