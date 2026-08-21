import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Item {
    id: delegateRoot

    property int pageIdx: model.pageIndex
    property bool dragActive: dragHandler.active

    z: dragActive ? 10 : 1

    HoverHandler { id: thumbHover }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radius
        color: model.current ? Theme.selected : "transparent"
        border.color: dragActive ? Theme.accent
                      : (model.current ? Theme.accent : Theme.divider)
        border.width: 1

        scale: delegateRoot.dragActive ? 1.03 : 1.0
        Behavior on scale { NumberAnimation { duration: 100 } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 3

            Image {
                Layout.fillWidth: true
                Layout.fillHeight: true
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                source: "image://ocr/page/" + model.pageIndex
                        + "?r=" + controller.docRevision
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Rectangle {
                    width: 9
                    height: 9
                    radius: model.edited ? 2 : 5
                    color: model.hasDuplicates ? "#d32f2f"
                           : (model.edited ? Theme.textPrimary
                           : (model.recognized ? Theme.textSecondary
                                               : "transparent"))
                    border.width: model.recognized || model.edited || model.hasDuplicates ? 0 : 1
                    border.color: Theme.textMuted

                    Accessible.role: Accessible.StaticText
                    Accessible.name: model.hasDuplicates ? qsTr("Has duplicates")
                                     : (model.edited ? qsTr("Edited")
                                     : (model.recognized ? qsTr("Recognized")
                                                         : qsTr("Not recognized")))
                }
                Label {
                    text: qsTr("Page %1").arg(model.pageIndex + 1)
                    font.pixelSize: Theme.fontSmall
                    color: model.hasDuplicates ? "#d32f2f"
                            : (model.recognized ? Theme.textSecondary
                            : Theme.textMuted)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        TapHandler {
            onTapped: controller.currentPage = model.pageIndex
        }
    }// Rectangle card

    ToolButton {
        id: deletePageButton
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 2
        implicitWidth: 20
        implicitHeight: 20
        padding: 0
        visible: thumbHover.hovered && !controller.busy && thumbList.draggedIndex === -1
        enabled: !controller.busy
        opacity: visible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 100 } }
        text: "\u2715"

        background: Rectangle {
            radius: width / 2
            color: deletePageButton.hovered ? Theme.border : Theme.surfaceAlt
            border.color: Theme.divider
            border.width: 1
        }

        ToolTip.visible: hovered
        ToolTip.text: qsTr("Delete page")
        Accessible.name: qsTr("Delete page %1").arg(model.pageIndex + 1)

        onClicked: controller.removePage(model.pageIndex)
    }// ToolButton deletePageButton

    Rectangle {
        id: dragGrip
        width: 24
        height: 24
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 2
        radius: 3
        z: 2
        visible: (thumbHover.hovered || dragActive) && !controller.busy
        opacity: visible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 100 } }
        color: dragActive ? Theme.accent : Theme.surfaceAlt
        border.color: Theme.divider
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "\u22EE\u22EE"
            color: Theme.textSecondary
            font.pixelSize: 9
        }

        DragHandler {
            id: dragHandler
            target: delegateRoot
            enabled: !controller.busy
            cursorShape: Qt.SizeVerCursor

            // Захватываем только вертикальное перемещение
            yAxis.enabled: true
            xAxis.enabled: false

            property int fromIndex: -1

            onActiveChanged: {
                if (active) {
                    fromIndex = index
                    thumbList.draggedIndex = index
                } else {
                    // Вычисляем целевой индекс по центру делегата
                    const centerY = delegateRoot.y + delegateRoot.height / 2
                    let toIndex = Math.floor(centerY / (delegateRoot.height + thumbList.spacing))
                    toIndex = Math.max(0, Math.min(thumbList.count - 1, toIndex))

                    thumbList.draggedIndex = -1

                    if (fromIndex !== -1 && fromIndex !== toIndex)
                        controller.movePage(fromIndex, toIndex)
                    else
                        delegateRoot.y = index * (delegateRoot.height + thumbList.spacing) // вернуть на место
                }
            }
        }
    }//Rectangle dragGrip
}//delegate
