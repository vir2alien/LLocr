import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string label: ""
    property string help: ""
    property alias checked: box.checked

    signal toggled(bool value)

    spacing: 2

    LLOCheckBox {
        id: box
        Layout.fillWidth: true
        font.pixelSize: Theme.bodySmallSize
        text: root.label
        onToggled: root.toggled(checked)
    }
    LLOLabel {
        Layout.fillWidth: true
        visible: root.help.length > 0
        font.pixelSize: Theme.captionSize
        color: Theme.helpColor
        wrapMode: Text.WordWrap
        text: root.help
    }
}