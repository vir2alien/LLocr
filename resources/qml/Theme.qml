pragma Singleton

import QtQuick

QtObject {
    readonly property bool dark: uiController.dark

    // --- Surfaces (back to front) ---
    readonly property color background: dark ? "#1c1c1c" : "#f2f2f2"
    readonly property color surface: dark ? "#242424" : "#fafafa"
    readonly property color surfaceAlt: dark ? "#2b2b2b" : "#ffffff"
    readonly property color surfaceSunken: dark ? "#161616" : "#e8e8e8"

    // --- Lines ---
    readonly property color divider: dark ? "#333333" : "#dcdcdc"
    readonly property color border: dark ? "#3d3d3d" : "#cfcfcf"

    // --- Text ---
    readonly property color textPrimary: dark ? "#ececec" : "#1a1a1a"
    readonly property color textSecondary: dark ? "#b4b4b4" : "#4a4a4a"
    readonly property color textMuted: dark ? "#7a7a7a" : "#8c8c8c"

    // --- Interaction ---
    readonly property color accent: dark ? "#b0b0b0" : "#4a4a4a"
    readonly property color selected: dark ? "#333333" : "#e2e2e2"

    // --- Overlay (bounding boxes on the image preview) ---
    readonly property color overlayOuter: overlayTextOuter
    readonly property color overlayInner: overlayTextInner
    readonly property color overlayTextOuter: "#1a1a1a"
    readonly property color overlayTextInner: "#f5f5f5"
    readonly property color overlayImageOuter: "#2196F3"
    readonly property color overlayImageInner: "#FFFFFF"
    readonly property color overlayImageFill: "#102194F3"

    // --- Metrics ---
    readonly property int spacingSmall: 4
    readonly property int spacing: 8
    readonly property int spacingLarge: 16
    readonly property int radius: 4
    readonly property int dialogRadius: 6
    readonly property int controlHeight: 28
    readonly property int controlRadius: 3

    readonly property int fontSmall: 11
    readonly property int fontCaption: 12
    readonly property int fontNormal: 13
    readonly property int fontTitle: 15
}
