pragma Singleton

import QtQuick

import LLocr

QtObject {
    readonly property bool dark: UiController.dark

    // --- Surfaces (back to front) ---
    readonly property color background: dark ? "#1c1c1c" : "#f2f2f2"
    readonly property color surface: dark ? "#242424" : "#fafafa"
    readonly property color surfaceAlt: dark ? "#2b2b2b" : "#ffffff"
    readonly property color surfaceSunken: dark ? "#161616" : "#e8e8e8"

    readonly property color divider: dark ? "#333333" : "#dcdcdc"
    readonly property color border: dark ? "#3d3d3d" : "#cfcfcf"

    readonly property color textPrimary: dark ? "#ececec" : "#1a1a1a"
    readonly property color textSecondary: dark ? "#b4b4b4" : "#4a4a4a"
    readonly property color textMuted: dark ? "#7a7a7a" : "#8c8c8c"

    readonly property color accent: dark ? "#b0b0b0" : "#4a4a4a"
    readonly property color selected: dark ? "#333333" : "#e2e2e2"
    // The one warm accent: the «Исправить (N)» badge lights up when a check
    // has left problem blocks behind.
    readonly property color accentAttention: "#EF6C00"

    readonly property color error: "#c0392b"
    readonly property color success: "#27ae60"
    readonly property color warning: "#e6b800"
    readonly property color warningBg: dark ? "#3a2f12" : "#fdf3d7"
    readonly property color nothing: "#8a8a8a"

    readonly property color helpColor: textSecondary

    // --- The block inspector -------------------------------------------------
    // The version cards of the block panel: the one in view carries the accent,
    // the other one is muted.
    readonly property color cardSurface: dark ? "#2f2f2f" : "#ffffff"
    readonly property color cardSurfaceMuted: dark ? "#262626" : "#f1f5f9"
    readonly property color cardBorder: dark ? "#3f3f3f" : "#e2e8f0"
    readonly property color cardAccent: dark ? "#60a5fa" : "#3b82f6"
    readonly property color cardAccentBg: dark ? "#1c3450" : "#eff6ff"
    // The diff tints: what the correction dropped from the original and what it
    // put in. A soft background, no strikethrough — a strike reads as a minus
    // sign inside a formula.
    readonly property color diffRemovedBg: dark ? "#4a2020" : "#fee2e2"
    readonly property color diffRemovedText: dark ? "#fca5a5" : "#991b1b"
    readonly property color diffAddedBg: dark ? "#123a25" : "#dcfce7"
    readonly property color diffAddedText: dark ? "#86efac" : "#166534"

    // --- Overlay (bounding boxes on the image preview) ---
    // One hue per block kind; fill = ~6% translucency, outer/inner = the
    // double frame around the box.
    readonly property color overlayTextOuter: "#2E7D32"
    readonly property color overlayTextInner: "#E8F5E9"
    readonly property color overlayTextFill: "#102E7D32"
    readonly property color overlayTableOuter: "#8E24AA"
    readonly property color overlayTableInner: "#F3E5F5"
    readonly property color overlayTableFill: "#108E24AA"
    readonly property color overlayFormulaOuter: "#EF6C00"
    readonly property color overlayFormulaInner: "#FFF3E0"
    readonly property color overlayFormulaFill: "#10EF6C00"
    readonly property color overlayImageOuter: "#2196F3"
    readonly property color overlayImageInner: "#FFFFFF"
    readonly property color overlayImageFill: "#102194F3"
    // Service blocks (running heads/feet, page numbers): page furniture, not
    // content — grey so they never read as a text block.
    readonly property color overlayServiceOuter: "#757575"
    readonly property color overlayServiceInner: "#D9D9D9"
    readonly property color overlayServiceFill: "#10757575"

    readonly property int spacingSmall: 4
    readonly property int spacing: 8
    readonly property int spacingLarge: 16
    readonly property int spacingXLarge: 24
    readonly property int paddingWindow: 24
    readonly property int rowHeightLarge: 36
    readonly property int radius: 4
    readonly property int dialogRadius: 6
    readonly property int controlHeight: 28
    readonly property int controlRadius: 3

    // --- Text pt sizes ---
    readonly property real platformTextScale: Qt.platform.os === "osx" ? 4 / 3 : 1
    readonly property int iconSize: 9 * platformTextScale
    readonly property int captionSize: 8 * platformTextScale
    readonly property int footnoteSize: 9 * platformTextScale
    readonly property int bodySmallSize: 10 * platformTextScale
    readonly property int bodySize: 11 * platformTextScale
    readonly property int titleSize: 16 * platformTextScale

    readonly property color linkColor: accent

    readonly property font caption: Qt.font({
        pointSize: captionSize
    })

    // The code-like blocks — a formula, a table, a snippet — are read in a
    // monospace face: the brackets, the subscripts and the spaces must not
    // drift apart.
    readonly property string monoFamily: Qt.platform.os === "osx" ? "Menlo"
                                       : (Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono")
    readonly property font mono: Qt.font({
        family: monoFamily,
        pointSize: bodySmallSize
    })
    readonly property font monoSmall: Qt.font({
        family: monoFamily,
        pointSize: captionSize
    })
}
