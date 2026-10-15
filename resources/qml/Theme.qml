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
}
