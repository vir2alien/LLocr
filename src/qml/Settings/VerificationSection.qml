pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The block types are edited in the live model and are written to the user
// file on save; the model's loadValues() re-reads the saved state, so it is
// also the undo.
Item {
    id: root

    signal edited()

    function loadValues() {
        blocksTab.loadValues()
        Verification.loadValues()
    }

    function saveValues() {
        blocksTab.saveValues()
        Verification.save()
    }

    function cancelValues() {
        root.loadValues()
    }

    function resetValues() {
        Verification.resetToDefaults()
        blocksTab.resetValues()
        root.loadValues()
    }

    VerificationBlocksTab {
        id: blocksTab
        anchors.fill: parent
        onEdited: root.edited()
    }
}
