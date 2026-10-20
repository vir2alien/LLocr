pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A captioned action with a menu arrow: the main click runs the default
// variant, the arrow opens the alternatives (the rework plan's §7 split
// buttons).
Row {
    id: splitRoot

    // The toolbar may shrink a layout child below its implicit width
    // (QML-I7): pin the cell so labels clip on the right instead of the
    // caption being squeezed out.
    Layout.minimumWidth: implicitWidth

    spacing: 0

    property string text: ""
    property string iconName: ""
    property string mainTip: ""
    property bool mainEnabled: true
    property bool arrowEnabled: true
    signal mainActivated()
    signal menuRequested(Item anchor)

    ToolButton {
        id: mainButton
        text: splitRoot.text
        enabled: splitRoot.mainEnabled
        ToolTip.visible: splitRoot.mainTip.length > 0 && mainButton.hovered
        ToolTip.text: splitRoot.mainTip

        onClicked: splitRoot.mainActivated()

        // A custom contentItem does not size the control by itself: without
        // the explicit implicit width every split button collapses to its
        // padding and the toolbar draws them on top of each other.
        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
        implicitHeight: topPadding + bottomPadding + contentItem.implicitHeight

        contentItem: Item {
            implicitWidth: textRow.implicitWidth
            implicitHeight: textRow.implicitHeight

            Row {
                id: textRow
                anchors.centerIn: parent
                spacing: 5

                Icon {
                    visible: splitRoot.iconName.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    name: splitRoot.iconName
                    size: 15
                    color: mainButton.enabled ? Theme.textPrimary : Theme.textMuted
                }
                Text {
                    visible: mainButton.text.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: mainButton.text
                    font: mainButton.font
                    color: mainButton.enabled ? Theme.textPrimary : Theme.textMuted
                }
            }
        }
    }
    ToolButton {
        id: arrowButton
        text: "\u25be"
        enabled: splitRoot.arrowEnabled
        onClicked: splitRoot.menuRequested(arrowButton)
    }
}
