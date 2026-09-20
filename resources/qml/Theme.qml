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

    // --- Status ---
    readonly property color error: "#c0392b"
    readonly property color success: "#27ae60"
    readonly property color warning: "#e6b800"
    readonly property color warningBg: dark ? "#3a2f12" : "#fdf3d7"
    readonly property color nothing: "#8a8a8a"

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

    // --- Text pt sizes ---
    readonly property real platformTextScale: Qt.platform.os === "osx" ? 4 / 3 : 1
    readonly property real textScale: platformTextScale
    readonly property int iconSize: 9 * textScale
    readonly property real captionSize: 8 * textScale
    readonly property real footnoteSize: 9 * textScale
    readonly property real bodySmallSize: 10 * textScale
    readonly property real bodySize: 11 * textScale
    readonly property real subtitleSize: 13 * textScale
    readonly property real titleSize: 16 * textScale
    readonly property real h2Size: 19 * textScale
    readonly property real h1Size: 23 * textScale
    readonly property real displaySize: 30 * textScale
    // --- Font Colors ---
    readonly property color captionColor: textMuted
    readonly property color footnoteColor: textMuted
    readonly property color bodySmallColor: textPrimary
    readonly property color bodyColor: textPrimary
    readonly property color subtitleColor: textSecondary
    readonly property color titleColor: textPrimary
    readonly property color h2Color: textPrimary
    readonly property color h1Color: textPrimary
    readonly property color displayColor: textPrimary
    readonly property color linkColor: accent
    readonly property color disabledTextColor: textMuted
    // --- Fonts ---
    readonly property font caption: Qt.font({
        pointSize: captionSize
    })
    readonly property font footnote: Qt.font({
        pointSize: footnoteSize
    })
    readonly property font bodySmall: Qt.font({
        pointSize: bodySmallSize
    })
    readonly property font body: Qt.font({
        pointSize: bodySize
    })
    readonly property font subtitle: Qt.font({
        pointSize: subtitleSize
    })
    readonly property font title: Qt.font({
        pointSize: titleSize,
        weight: Font.DemiBold
    })
    readonly property font h2: Qt.font({
        pointSize: h2Size,
        weight: Font.Bold
    })
    readonly property font h1: Qt.font({
        pointSize: h1Size,
        weight: Font.Bold
    })
    readonly property font display: Qt.font({
        pointSize: displaySize,
        weight: Font.Bold
    })


}
