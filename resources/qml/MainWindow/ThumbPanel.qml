pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Rectangle {
    id: panelRoot

    // Below this width the caption «Страницы» hides and only «Выбрать все»
    // stays — the narrowest column must still fit it uncropped.
    readonly property bool compactHeader: width < 240

    LLOLabel {
        anchors.centerIn: parent
        visible: Controller.hasImage && Controller.pageCount === 0
        text: qsTr("No pages")
        color: Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            visible: Controller.hasImage
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            Layout.preferredHeight: 24
            Layout.minimumWidth: implicitWidth

            LLOLabel {
                visible: !panelRoot.compactHeader
                text: qsTr("Pages")
                color: Theme.textMuted
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                id: selectAllButton
                flat: true
                font: Theme.caption
                leftPadding: 4
                rightPadding: 4
                Layout.minimumWidth: implicitWidth
                visible: Controller.pageCount > 1
                text: allSelected ? qsTr("Select none")
                                  : qsTr("Select all (%1)").arg(Controller.pageCount)
                readonly property bool allSelected: Controller.pageCount > 0
                                                    && Controller.selectedPageCount === Controller.pageCount
                onClicked: allSelected ? Controller.pageModel.clearSelection()
                                       : Controller.pageModel.selectAll()
            }
        }

        ListView {
            id: thumbList
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The cards keep a breathing margin on three sides; the top one
            // comes from the header row above.
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            Layout.bottomMargin: Theme.spacing
            spacing: Theme.spacing
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            cacheBuffer: 1000

            interactive: draggedIndex === -1

            model: Controller.pageModel

            property int draggedIndex: -1
            property Item draggedItem: null

            // While a drag is active the view is non-interactive, so reaching a
            // page beyond the viewport needs an explicit edge-driven scroll; the
            // card is shifted back by the same delta to stay under the cursor.
            Timer {
                id: dragAutoScroll
                interval: 16
                repeat: true
                running: thumbList.draggedIndex !== -1 && thumbList.draggedItem !== null

                onTriggered: {
                    const zone = 56
                    const maxSpeed = 22
                    const pos = thumbList.draggedItem.y + thumbList.draggedItem.height / 2
                                - thumbList.contentY
                    let dy = 0
                    if (pos < zone)
                        dy = -maxSpeed * (zone - pos) / zone
                    else if (pos > thumbList.height - zone)
                        dy = maxSpeed * (pos - thumbList.height + zone) / zone
                    dy = Math.max(-maxSpeed, Math.min(maxSpeed, dy))

                    const maxScroll = Math.max(0, thumbList.contentHeight - thumbList.height)
                    const applied = Math.max(0, Math.min(maxScroll, thumbList.contentY + dy))
                                    - thumbList.contentY
                    if (applied !== 0) {
                        thumbList.contentY += applied
                        thumbList.draggedItem.y -= applied
                    }
                }
            }

            displaced: Transition {
                NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutQuad }
            }

            delegate: ThumbDelegate {
                listView: thumbList
                width: thumbList.width - Theme.spacingSmall
                height: width * 1.3 + 22
            }
        }//ListView
    }//ColumnLayout
}
