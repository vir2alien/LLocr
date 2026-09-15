import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 32

    signal installClicked(int index)

    visible: count > 0
    clip: true
    model: ModelInstaller.searchCount
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: searchRow
        required property int index
        property var sInfo: ModelInstaller.searchResult(index)

        width: root.width
        height: root.rowHeight
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6
            LLOLabel {
                Layout.preferredWidth: 180
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: searchRow.sInfo.title
            }
            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: searchRow.sInfo.id
            }
            LLOButton {
                text: qsTr("Install")
                enabled: !ModelInstaller.busy
                onClicked: root.installClicked(searchRow.index)
            }
        }
    }
}
