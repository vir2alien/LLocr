pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

Item {
    id: delegateRoot

    required property int index
    required property int pageIndex
    required property bool current
    required property bool recognized
    required property bool edited
    required property bool hasDuplicates

    property ListView listView
    property int pageIdx: pageIndex
    property bool dragActive: dragHandler.active

    z: dragActive ? 10 : 1

    HoverHandler { id: thumbHover }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radius
        color: current ? Theme.selected : "transparent"
        border.color: dragActive ? Theme.accent
                      : (current ? Theme.accent : Theme.divider)
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
                source: "image://ocr/page/" + pageIndex
                        + "?r=" + Controller.docRevision
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Rectangle {
                    Layout.preferredWidth: 9
                    Layout.preferredHeight: 9
                    radius: edited ? 2 : 5
                    color: hasDuplicates ? "#d32f2f"
                           : (edited ? Theme.textPrimary
                           : (recognized ? Theme.textSecondary
                                         : "transparent"))
                    border.width: recognized || edited || hasDuplicates ? 0 : 1
                    border.color: Theme.textMuted

                    Accessible.role: Accessible.StaticText
                    Accessible.name: hasDuplicates ? qsTr("Has duplicates")
                                     : (edited ? qsTr("Edited")
                                     : (recognized ? qsTr("Recognized")
                                                   : qsTr("Not recognized")))
                }
                LLOLabel {
                    text: qsTr("Page %1").arg(pageIndex + 1)
                    font.pointSize: Theme.captionSize
                    color: hasDuplicates ? "#d32f2f"
                            : (recognized ? Theme.textSecondary
                            : Theme.textMuted)
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                }
            }
        }

        TapHandler {
            onTapped: Controller.currentPage = pageIndex
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
        visible: thumbHover.hovered && !Controller.busy && !Controller.importing && listView.draggedIndex === -1
        enabled: !Controller.busy && !Controller.importing
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
        Accessible.name: qsTr("Delete page %1").arg(pageIndex + 1)

        onClicked: Controller.removePage(pageIndex)
    }// ToolButton

    Rectangle {
        id: dragGrip
        width: 24
        height: 24
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 2
        radius: 3
        z: 2
        visible: (thumbHover.hovered || dragActive) && !Controller.busy && !Controller.importing
        opacity: visible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 100 } }
        color: dragActive ? Theme.accent : Theme.surfaceAlt
        border.color: Theme.divider
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "\u22EE\u22EE"
            color: Theme.textSecondary
            font.pointSize: Theme.bodySize
        }

        DragHandler {
            id: dragHandler
            target: delegateRoot
            enabled: !Controller.busy && !Controller.importing
            cursorShape: Qt.SizeVerCursor

            yAxis.enabled: true
            xAxis.enabled: false

            property int fromIndex: -1

            onActiveChanged: {
                if (active) {
                    fromIndex = index
                    listView.draggedIndex = index
                } else {
                    const centerY = delegateRoot.y + delegateRoot.height / 2
                    let toIndex = Math.floor(centerY / (delegateRoot.height + listView.spacing))
                    toIndex = Math.max(0, Math.min(listView.count - 1, toIndex))

                    listView.draggedIndex = -1

                    if (fromIndex !== -1 && fromIndex !== toIndex)
                        Controller.movePage(fromIndex, toIndex)
                    else
                        delegateRoot.y = index * (delegateRoot.height + listView.spacing) // вернуть на место
                }
            }
        }//DragHandler
    }//Rectangle
}

