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
        font.pointSize: Theme.captionSize
        text: root.label
        onToggled: root.toggled(checked)
    }
    LLOLabel {
        Layout.fillWidth: true
        visible: root.help.length > 0
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        wrapMode: Text.WordWrap
        text: root.help
    }
}