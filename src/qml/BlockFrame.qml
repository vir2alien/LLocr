pragma Singleton

import QtQuick

import LLocr

// The frame of a block in the preview (ADR 238, ADR 338). The rule lives here
// and not in the delegate of the preview, so the test can read the shipped
// file — the same reason the page status has its own singleton.
QtObject {
    // The color of the frame of the block. The block the user wrote by hand
    // is yellow: the frame says what the block is now, not what the model
    // said about the text before it (ADR 338). The reader's confirmation is a
    // verification of its own (ADR 261): a re-read that reproduced the text is
    // green, a re-read that could not read the block is red.
    function stateColor(effectiveStatus, fixStatus, documentDiverged) {
        if (documentDiverged || effectiveStatus === 6)
            return Theme.warning
        if (fixStatus === 6)
            return Theme.error
        if (fixStatus === 4)
            return Theme.success
        if (effectiveStatus === 1 || effectiveStatus === 5)
            return Theme.success
        if (effectiveStatus === 3 || effectiveStatus === 4 || effectiveStatus === 7)
            return Theme.error
        return Theme.nothing
    }

    // The frame of a block that carries no text draws nothing: the markup
    // alone is not a block yet (ADR 238). A block the reader could not read
    // carries a verdict without text, and its frame is the only sign that the
    // block needs a human (ADR 261).
    function frameColor(effectiveStatus, fixStatus, documentDiverged, hasText) {
        if (!hasText && fixStatus !== 6)
            return "transparent"
        return stateColor(effectiveStatus, fixStatus, documentDiverged)
    }
}
