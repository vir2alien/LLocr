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
    required property bool selected
    required property bool recognized
    required property bool edited
    required property bool hasDuplicates
    required property int pageRecognitionMode
    required property int pageVerification

    // A complete aggregate outranks the recognition mode (the rework plan's §6).
    readonly property bool aggregateDone: pageVerification === 1 || pageVerification === 2
    readonly property string statusText: PageStatus.statusText(pageRecognitionMode, pageVerification)

    property ListView listView
    property bool dragActive: dragHandler.active

    z: dragActive ? 10 : 1

    HoverHandler { id: thumbHover }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radius
        color: current || selected ? Theme.selected : "transparent"
        border.color: dragActive ? Theme.accent
                      : (current ? Theme.accent
                      : (selected ? Theme.border : Theme.divider))
        border.width: 1

        scale: delegateRoot.dragActive ? 1.03 : 1.0
        Behavior on scale { NumberAnimation { duration: 100 } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 3

            Image {
                id: pageThumb
                Layout.fillWidth: true
                Layout.fillHeight: true
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                source: "image://ocr/page/" + pageIndex
                        + "?r=" + Controller.docRevision

                LLOLabel {
                    anchors.centerIn: parent
                    visible: pageThumb.status === Image.Error
                    text: qsTr("Load error")
                    font.pointSize: Theme.captionSize
                    color: Theme.error
                }
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
                           : (aggregateDone ? (pageVerification === 2 ? Theme.success : Theme.error)
                           : (recognized ? Theme.textSecondary
                                         : "transparent")))
                    border.width: recognized || edited || hasDuplicates || aggregateDone ? 0 : 1
                    border.color: Theme.textMuted

                    Accessible.role: Accessible.StaticText
                    Accessible.name: hasDuplicates ? qsTr("Has duplicates")
                                     : (edited ? qsTr("Edited")
                                     : (statusText.length > 0 ? statusText
                                                              : (recognized ? qsTr("Recognized")
                                                                            : qsTr("Not recognized"))))
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

            LLOLabel {
                Layout.fillWidth: true
                visible: statusText.length > 0
                text: statusText
                font.pointSize: Theme.captionSize
                color: aggregateDone && pageVerification === 1 ? Theme.error : Theme.textMuted
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: (mouse) => {
                // Ctrl/Cmd adds the page to (or removes it from) the toolbar
                // scope without moving the current page; a plain click selects
                // the page alone.
                const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                if (additive) {
                    Controller.pageModel.setSelected(pageIndex, !selected)
                } else {
                    Controller.pageModel.selectSingle(pageIndex)
                    Controller.currentPage = pageIndex
                }
            }
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
        visible: thumbHover.hovered && !Controller.busy && !Controller.importing && !Controller.checkBusy && listView.draggedIndex === -1
        enabled: !Controller.busy && !Controller.importing && !Controller.checkBusy
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
        width: 24
        height: 24
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 2
        radius: 3
        z: 2
        visible: (thumbHover.hovered || dragActive) && !Controller.busy && !Controller.importing && !Controller.checkBusy
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
            enabled: !Controller.busy && !Controller.importing && !Controller.checkBusy
            cursorShape: Qt.SizeVerCursor

            yAxis.enabled: true
            xAxis.enabled: false

            property int fromIndex: -1

            onActiveChanged: {
                if (active) {
                    fromIndex = index
                    listView.draggedIndex = index
                    listView.draggedItem = delegateRoot
                } else {
                    const centerY = delegateRoot.y + delegateRoot.height / 2
                    let toIndex = Math.floor(centerY / (delegateRoot.height + listView.spacing))
                    toIndex = Math.max(0, Math.min(listView.count - 1, toIndex))

                    listView.draggedIndex = -1
                    listView.draggedItem = null

                    if (fromIndex !== -1 && fromIndex !== toIndex)
                        Controller.movePage(fromIndex, toIndex)
                    else
                        delegateRoot.y = index * (delegateRoot.height + listView.spacing) // вернуть на место
                }
            }
        }//DragHandler
    }//Rectangle
}

