pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

import LLocr

// Monochrome SVG glyph from qrc:/icons/ui/<name>.svg, colorized at runtime so
// one asset set serves both themes (the SVGs ship with stroke=currentColor).
Item {
    id: root

    property string name: ""
    property color color: Theme.textPrimary
    property int size: 18

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
