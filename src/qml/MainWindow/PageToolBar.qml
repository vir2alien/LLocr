pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

ToolBar {
    id: pageToolBarRoot

    visible: Controller.hasImage

    // The page preview the zoom acts on (set by ImagePanel).
    property var preview: null

    // The add buttons do not add a block on the spot: they arm the draw mode,
    // and the user draws the block on the page with the mouse (ADR 189).
    function beginDraw(name) {
        if (!pageToolBarRoot.preview)
            return
        pageToolBarRoot.preview.beginDrawBlock(name)
    }

    function cancelDraw() {
        if (!pageToolBarRoot.preview)
            return
        pageToolBarRoot.preview.cancelDrawBlock()
    }

    // The default single-page selection follows the navigation until the
    // user builds a multi-selection of their own.
    function goTo(pageIndex) {
        if (Controller.selectedPageCount === 1 && Controller.pageModel.isSelected(Controller.currentPage))
            Controller.pageModel.selectSingle(pageIndex)
        Controller.currentPage = pageIndex
    }

    leftPadding: Theme.spacing
    rightPadding: Theme.spacing
    topPadding: Theme.spacingSmall
    bottomPadding: Theme.spacingSmall

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }
    }

    contentItem: RowLayout {
        spacing: Theme.spacingSmall

        RowLayout {
            visible: Controller.pageCount > 1
            spacing: 0

            LLOLabel {
                text: qsTr("Page")
                color: Theme.textSecondary
            }
            ToolButton {
                text: "\u2039"
                Layout.minimumWidth: implicitWidth
                enabled: Controller.currentPage > 0
                onClicked: pageToolBarRoot.goTo(Controller.currentPage - 1)
            }
            LLOLabel {
                text: (Controller.currentPage + 1) + " / " + Controller.pageCount
                horizontalAlignment: Text.AlignHCenter
                Layout.minimumWidth: 56
            }
            ToolButton {
                text: "\u203a"
                Layout.minimumWidth: implicitWidth
                enabled: Controller.currentPage < Controller.pageCount - 1
                onClicked: pageToolBarRoot.goTo(Controller.currentPage + 1)
            }
        }

        ToolSeparator { visible: Controller.pageCount > 1 }

        Row {
            spacing: 0

            BlockAddButton { iconName: "type"; blockName: "Text" }
            BlockAddButton { iconName: "image"; blockName: "Image" }
            BlockAddButton { iconName: "table"; blockName: "Table" }
            BlockAddButton { iconName: "sigma"; blockName: "Formula" }
            BlockAddButton { iconName: "code"; blockName: "Code" }

            ToolButton {
                id: allBlocksButton
                implicitWidth: 32
                implicitHeight: 28
                padding: 0
                enabled: Controller.actionsIdle
                Row {
                    anchors.centerIn: parent
                    spacing: 0
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "layout-grid"
                        size: 18
                        color: allBlocksButton.enabled ? Theme.textPrimary : Theme.textMuted
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\u25be"
                        font: Theme.caption
                        color: allBlocksButton.enabled ? Theme.textPrimary : Theme.textMuted
                    }
                }
                onClicked: allBlocksMenu.popup(allBlocksButton, 0, allBlocksButton.height + 2)
            }
        }

        ToolSeparator {}

        Row {
            spacing: 0
            StubIconButton { iconName: "rotate-ccw" }
            StubIconButton { iconName: "rotate-cw" }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            id: zoomButton
            implicitWidth: 58
            implicitHeight: 28
            padding: 0
            enabled: pageToolBarRoot.preview !== null
            Row {
                anchors.centerIn: parent
                spacing: 0
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: pageToolBarRoot.preview
                          ? Math.round(pageToolBarRoot.preview.zoom * 100) + "%" : ""
                    color: Theme.textPrimary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u25be"
                    font: Theme.caption
                    color: Theme.textPrimary
                }
            }
            onClicked: zoomMenu.popup(zoomButton, 0, zoomButton.height + 2)
        }
    }

    Menu {
        id: zoomMenu

        MenuItem {
            text: qsTr("Zoom in")
            onTriggered: pageToolBarRoot.preview.zoomBy(1.25)
        }
        MenuItem {
            text: qsTr("Zoom out")
            onTriggered: pageToolBarRoot.preview.zoomBy(1 / 1.25)
        }
        MenuItem {
            text: qsTr("Fit page")
            onTriggered: pageToolBarRoot.preview.resetZoom()
        }
    }

    Menu {
        id: allBlocksMenu

        Instantiator {
            model: Object.keys(BlockNames.nameMap)
            delegate: MenuItem {
                required property string modelData
                text: BlockNames.displayName(modelData)
                onTriggered: pageToolBarRoot.beginDraw(modelData)
            }
            onObjectAdded: (index, object) => allBlocksMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => allBlocksMenu.removeItem(object)
        }
    }

    component BlockAddButton: ToolButton {
        id: addButton

        required property string iconName
        required property string blockName

        implicitWidth: 32
        implicitHeight: 28
        Layout.minimumWidth: implicitWidth
        padding: 0
        enabled: Controller.actionsIdle

        // The button that armed the draw mode says so: it is the way out.
        readonly property bool drawing: pageToolBarRoot.preview
                                         && pageToolBarRoot.preview.drawBlockName === addButton.blockName

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.controlRadius
            visible: addButton.drawing
            color: Theme.selected
            border.color: Theme.border
            border.width: 1
        }

        Icon {
            anchors.centerIn: parent
            name: addButton.iconName
            size: 18
            color: addButton.enabled ? Theme.textPrimary : Theme.textMuted
        }

        onClicked: {
            if (addButton.drawing)
                pageToolBarRoot.cancelDraw()
            else
                pageToolBarRoot.beginDraw(addButton.blockName)
        }

        ToolTip.visible: addButton.hovered
        ToolTip.text: addButton.drawing ? qsTr("Cancel")
                                        : BlockNames.displayName(addButton.blockName)
    }

    component StubIconButton: ToolButton {
        id: stubButton

        property string iconName: ""

        implicitWidth: 32
        implicitHeight: 28
        Layout.minimumWidth: implicitWidth
        padding: 0
        enabled: false

        Icon {
            anchors.centerIn: parent
            name: stubButton.iconName
            size: 18
            color: Theme.textMuted
        }

        ToolTip.visible: stubButton.hovered
        ToolTip.text: qsTr("Not implemented yet")
    }
}
