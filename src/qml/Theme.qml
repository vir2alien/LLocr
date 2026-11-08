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
    // A row of a dense list — the block types in the verification settings.
    readonly property int rowHeightCompact: 30
    readonly property int radius: 4
    readonly property int dialogRadius: 6
    readonly property int controlHeight: 28
    readonly property int controlRadius: 3

    // --- Text and icon sizes (px, ADR 293) ----------------------------------
    // Every size is a px value at scale 1 multiplied by the per-OS factor —
    // one dial per platform to tune the rendered look. Text is pixelSize:
    // pt renders differently per OS, px is predictable (supersedes the ADR 77
    // point-size compensation). The application-wide font family is the
    // system stack set in main.cpp; these sizes ride on it.
    readonly property real uiScaleMac: 1.0
    readonly property real uiScaleWindows: 1.0
    readonly property real uiScaleLinux: 1.0
    readonly property real uiScale: Qt.platform.os === "osx" ? uiScaleMac
                                  : Qt.platform.os === "windows" ? uiScaleWindows
                                  : uiScaleLinux

    function px(size) {
        return Math.round(size * uiScale)
    }

    // The text ladder, smallest up: badges/timestamps 11 → captions/hints 12 →
    // dense tables and lists 13 → body/buttons/inputs/menus 14 → section
    // subheading 16 → heading 3 18 → heading 2 20 → heading 1 / page title 24.
    readonly property int captionSize: px(11)
    readonly property int footnoteSize: px(12)
    readonly property int bodySmallSize: px(13)
    readonly property int bodySize: px(14)
    readonly property int subheadingSize: px(16)
    readonly property int headingSize3: px(18)
    readonly property int headingSize2: px(20)
    readonly property int headingSize1: px(24)

    // The icon ladder: inline arrows/indicators 12 → row buttons and hint
    // icons 16 → toolbars 18 → the header stop ring 26.
    readonly property int iconSizeSmall: px(12)
    readonly property int iconSizeMedium: px(16)
    readonly property int iconSizeLarge: px(18)
    readonly property int iconSizeXlarge: px(26)

    readonly property color linkColor: accent

    readonly property font caption: Qt.font({
        pixelSize: captionSize
    })

    readonly property font bodySmall: Qt.font({
        pixelSize: bodySmallSize
    })

    readonly property font body: Qt.font({
        pixelSize: bodySize
    })

    readonly property font buttonSmall: Qt.font({
        pixelSize: bodySmallSize
    })
    readonly property font button: Qt.font({
        pixelSize: bodySize
    })

    readonly property font captionUpper: {
        const f = Qt.font({
            pixelSize: captionSize
        })
        f.capitalization = Font.AllUppercase
        f.letterSpacing = 0.6
        return f
    }

    // The code-like blocks — a formula, a table, a snippet — are read in a
    // monospace face: the brackets, the subscripts and the spaces must not
    // drift apart.
    readonly property string monoFamily: Qt.platform.os === "osx" ? "Menlo"
                                       : (Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono")
    readonly property font mono: Qt.font({
        family: monoFamily,
        pixelSize: bodySmallSize
    })
    readonly property font monoSmall: Qt.font({
        family: monoFamily,
        pixelSize: captionSize
    })
}
