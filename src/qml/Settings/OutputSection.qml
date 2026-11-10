pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The output settings are a draft: the widgets hold the values until the
// window saves them.
Item {
    id: root

    signal edited()

    function loadValues() {
        outputTab.loadValues()
    }

    function saveValues() {
        outputTab.saveValues()
    }

    function cancelValues() {
        outputTab.loadValues()
    }

    function resetValues() {
        Settings.resetOutputDefaults()
        outputTab.loadValues()
    }

    OutputTab {
        id: outputTab
        anchors.fill: parent
        onEdited: root.edited()
    }
}
