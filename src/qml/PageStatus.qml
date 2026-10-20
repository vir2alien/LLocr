pragma Singleton

import QtQuick

QtObject {
    // The page status mapping (the rework plan's §6): a complete verification
    // aggregate outranks the recognition mode; a page that was never
    // recognized has no status to show.
    function statusText(mode, verification) {
        if (verification === 2)
            return qsTr("Verified (no errors)")
        if (verification === 1)
            return qsTr("Verified (with errors)")
        if (mode === 1)
            return qsTr("Markup only")
        if (mode === 2)
            return qsTr("Recognized (without markup)")
        if (mode === 3)
            return qsTr("Recognized")
        return ""
    }
}
