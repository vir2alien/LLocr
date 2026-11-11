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
    required property int orientationProblem

    // A complete aggregate outranks the recognition mode (the rework plan's §6).
    readonly property bool aggregateDone: pageVerification === 1 || pageVerification === 2
    readonly property string statusText: PageStatus.statusText(pageRecognitionMode, pageVerification, orientationProblem)
    // The orientation the check could not settle: the page needs a turn by
    // hand, and the marker says so.
    readonly property bool orientationProblemFound: orientationProblem !== 0

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
            spacing: Theme.spacingTiny

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
                    font.pixelSize: Theme.captionSize
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
                           : (orientationProblemFound ? Theme.error
                           : (edited ? Theme.textPrimary
                           : (aggregateDone ? (pageVerification === 2 ? Theme.success : Theme.error)
                           : (recognized ? Theme.textSecondary
                                         : "transparent"))))
                    border.width: recognized || edited || hasDuplicates || aggregateDone || orientationProblemFound ? 0 : 1
                    border.color: Theme.textMuted

                    Accessible.role: Accessible.StaticText
                    Accessible.name: hasDuplicates ? qsTr("Has duplicates")
                                     : (orientationProblemFound ? statusText
                                     : (edited ? qsTr("Edited")
                                     : (statusText.length > 0 ? statusText
                                                              : (recognized ? qsTr("Recognized")
                                                                            : qsTr("Not recognized")))))
                }
                LLOLabel {
                    text: qsTr("Page %1").arg(pageIndex + 1)
                    font.pixelSize: Theme.captionSize
                    color: hasDuplicates ? "#d32f2f"
                            : (recognized ? Theme.textSecondary
                            : Theme.textMuted)
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }
                LLOLabel {
                    visible: statusText.length > 0
                    text: statusText
                    font.pixelSize: Theme.captionSize
                    color: orientationProblemFound || (aggregateDone && pageVerification === 1) ? Theme.error : Theme.textMuted
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.fillWidth: true
                }
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

    // The page's own actions. The move buttons and the delete are separated by
    // a gap of one icon, so a stray click lands on the gap rather than on the
    // trash.
    Row {
        id: pageActions
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 2
        spacing: 0
        z: 2
        visible: thumbHover.hovered && !Controller.busy && !Controller.importing && !Controller.checkBusy && listView.draggedIndex === -1
        opacity: visible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 100 } }

        // The button lives in a wrapper that stays enabled: a disabled control
        // gets no hover events, so the tip of a button that cannot act has to
        // come from the wrapper (the header does the same).
        component PageAction: Item {
            id: action
            property string iconName: ""
            property string tip: ""
            property bool actionEnabled: true
            signal triggered()

            implicitWidth: 22
            implicitHeight: 22

            HoverHandler { id: actionHover }

            ToolTip.visible: actionHover.hovered
            ToolTip.text: action.tip

            ToolButton {
                id: actionButton
                anchors.fill: parent
                padding: 0
                enabled: action.actionEnabled

                background: Rectangle {
                    radius: Theme.controlRadius
                    color: actionButton.hovered ? Theme.border : Theme.surfaceAlt
                    border.color: Theme.divider
                    border.width: 1
                }

                contentItem: Icon {
                    name: action.iconName
                    size: Theme.iconSizeMedium
                    color: action.actionEnabled ? Theme.textPrimary : Theme.textMuted
                }

                Accessible.name: action.tip

                onClicked: action.triggered()
            }
        }

        PageAction {
            iconName: "chevron-up"
            tip: qsTr("Move page up")
            actionEnabled: pageIndex > 0
            onTriggered: Controller.movePage(pageIndex, pageIndex - 1)
        }
        PageAction {
            iconName: "chevron-down"
            tip: qsTr("Move page down")
            actionEnabled: pageIndex < Controller.pageCount - 1
            onTriggered: Controller.movePage(pageIndex, pageIndex + 1)
        }
        PageAction {
            id: positionButton
            iconName: "hash"
            tip: qsTr("Move page to position")
            onTriggered: positionPopup.open()
        }
        Item {
            width: Theme.iconSizeMedium
            height: 1
        }
        PageAction {
            iconName: "trash-2"
            tip: qsTr("Delete page")
            onTriggered: Controller.removePage(pageIndex)
        }
    }// Row pageActions

    // The target position: a number the page is put at.
    Popup {
        id: positionPopup
        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: Theme.spacing
        width: 230

        x: Theme.spacing
        y: Theme.spacing

        onOpened: {
            // The position is taken when the popup opens: a binding over
            // mapToItem would be evaluated once and would not follow the card.
            const anchor = positionButton.mapToItem(Overlay.overlay, 0, 0)
            x = Math.max(Theme.spacing,
                         Math.min(Overlay.overlay.width - width - Theme.spacing,
                                  anchor.x - width + positionButton.width))
            y = anchor.y + positionButton.height + Theme.spacingSmall

            positionField.text = pageIndex + 1
            positionField.selectAll()
            positionField.forceActiveFocus()
        }

        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingSmall

            LLOLabel {
                Layout.fillWidth: true
                text: qsTr("Move page to position")
                font.pixelSize: Theme.bodySmallSize
                color: Theme.textPrimary
            }

            RowLayout {
                spacing: Theme.spacingSmall

                TextField {
                    id: positionField
                    Layout.fillWidth: true
                    implicitHeight: 24
                    font: Theme.bodySmall
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: Controller.pageCount }
                    selectByMouse: true
                    onAccepted: positionPopup.applyPosition()
                }

                LLOButton {
                    text: qsTr("Move")
                    onClicked: positionPopup.applyPosition()
                }
            }
        }

        function applyPosition() {
            const target = parseInt(positionField.text)
            if (isNaN(target) || target < 1 || target > Controller.pageCount) {
                positionField.text = ""
                positionField.forceActiveFocus()
                return
            }
            Controller.movePage(pageIndex, target - 1)
            positionPopup.close()
        }
    }// Popup positionPopup

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
            font.pixelSize: Theme.bodySize
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

