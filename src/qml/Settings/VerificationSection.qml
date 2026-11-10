pragma ComponentBehavior: Bound

import QtQuick

import LLocr

// The block types are edited in the live model and are written to the user
// file on save; the session-start state is the undo for them.
Item {
    id: root

    signal edited()

    // The state of every block type at the start of the session: the toggles
    // apply at once (the model is the truth while the window is open), and
    // Cancel puts the types back where the session found them.
    property var blockTypeSnapshot: null

    function beginSession() {
        Verification.loadValues()
        root.blockTypeSnapshot = root.captureBlockTypes()
    }

    function captureBlockTypes() {
        const model = Verification.blockModel
        const state = {}
        for (let row = 0; row < model.totalCount; ++row)
            state[model.typeAt(row)] = model.enabledAt(row)
        return state
    }

    function restoreBlockTypes() {
        if (!root.blockTypeSnapshot)
            return
        const model = Verification.blockModel
        for (const type in root.blockTypeSnapshot) {
            const row = model.rowOfType(type)
            if (row >= 0)
                model.setEnabled(row, root.blockTypeSnapshot[type])
        }
    }

    // The reload inside a session must keep the toggles of this session: the
    // file is read at the start of the session, not on every load.
    function loadValues() {
        blocksTab.loadValues()
    }

    function saveValues() {
        blocksTab.saveValues()
        Verification.save()
    }

    function cancelValues() {
        root.restoreBlockTypes()
        blocksTab.loadValues()
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
