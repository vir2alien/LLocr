import QtQuick
import QtQuick.Layouts

LLOLabel {
    property string note: ""

    visible: note.length > 0
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    font.pointSize: Theme.captionSize
    color: Theme.warning
    text: qsTr("The managed runtime cannot run this model. %1").arg(note)
}