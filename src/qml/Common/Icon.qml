pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

import LLocr

// Monochrome SVG glyph from qrc:/icons/ui/<name>.svg, colorized at runtime so
// one asset set serves both themes. The assets ship with a white stroke: the
// colorization mixes the source by its luminance, so a black glyph would stay
// black on the dark background (ADR 298).
Item {
    id: root

    property string name: ""
    property color color: Theme.textPrimary
    property int size: Theme.iconSizeLarge

    width: root.size
    height: root.size

    Image {
        id: glyph
        anchors.fill: parent
        source: root.name ? "qrc:/icons/ui/" + root.name + ".svg" : ""
        visible: false
        fillMode: Image.PreserveAspectFit
        mipmap: true
    }

    MultiEffect {
        anchors.fill: parent
        source: glyph
        colorizationColor: root.color
        colorization: 1.0
    }
}
