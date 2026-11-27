pragma Singleton

import QtQuick

import LLocr

QtObject {
    // The page status (ADR 338): one vocabulary with the block. The
    // orientation the check could not settle outranks everything — the page
    // needs a turn by hand. A page without a markup block has nothing to
    // say. The recognition mode says how far the page came: the markup
    // alone («Размечено»), every block recognized («Распознано»). The
    // verification aggregate then speaks: an unanswered block keeps the page
    // at «Распознано», an error makes it erroneous, and a clean page is
    // «Проверено» — or «Отредактировано», when the user wrote a block of it.
    function statusKind(mode, verification, edited, orientationProblem) {
        if (orientationProblem === 1 || orientationProblem === 2)
            return "orientation"
        if (mode === 0 || mode === 2)
            return "none"
        if (mode === 1)
            return "markup"
        if (verification === 1)
            return "errors"
        if (verification === 2)
            return edited ? "edited" : "verified"
        return "recognized"
    }

    function statusText(mode, verification, edited, orientationProblem) {
        const kind = statusKind(mode, verification, edited, orientationProblem)
        if (kind === "orientation")
            return orientationProblem === 1 ? qsTr("On its side") : qsTr("Orientation unclear")
        if (kind === "none")
            return ""
        if (kind === "markup")
            return qsTr("Marked up")
        if (kind === "errors")
            return qsTr("Errors")
        if (kind === "edited")
            return qsTr("Edited")
        if (kind === "verified")
            return qsTr("Verified")
        return qsTr("Recognized")
    }

    function statusColor(mode, verification, edited, orientationProblem) {
        const kind = statusKind(mode, verification, edited, orientationProblem)
        if (kind === "orientation" || kind === "errors")
            return Theme.error
        if (kind === "edited")
            return Theme.warning
        if (kind === "verified")
            return Theme.success
        if (kind === "none")
            return "transparent"
        return Theme.nothing
    }
}
