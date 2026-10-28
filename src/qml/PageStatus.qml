pragma Singleton

import QtQuick

QtObject {
    // The page status mapping (the rework plan's §6): a complete verification
    // aggregate outranks the recognition mode; a page that was never
    // recognized has no status to show. The orientation the check could not
    // settle outranks both — the page needs a turn by hand.
    function statusText(mode, verification, orientationProblem) {
        if (orientationProblem === 1)
            return qsTr("On its side")
        if (orientationProblem === 2)
            return qsTr("Orientation unclear")
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
