import QtQuick
import QtQuick.Layouts

import LLocr

LLOLabel {
    id: root

    property string statusText: ""
    property bool isError: false
    property bool busy: false

    Layout.fillWidth: true
    font.pointSize: Theme.captionSize
    color: root.isError ? Theme.error
         : (root.busy ? Theme.textSecondary : Theme.textMuted)
    text: root.statusText
}
