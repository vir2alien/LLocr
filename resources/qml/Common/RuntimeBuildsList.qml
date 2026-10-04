pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 46
    property int maxVisibleRows: 3
    property bool scrollable: true

    signal actionError(string message)

    function fmtBytes(bytes) {
        if (bytes <= 0) return ""
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    Text {
        id: fontProbe
        visible: false
        font.pointSize: Theme.captionSize
    }
    FontMetrics {
        id: metrics
        font: fontProbe.font
    }

    readonly property int sizeColumn: Math.ceil(metrics.advanceWidth("1023.9 GiB"))
                                     + Theme.spacingSmall
    readonly property int actionColumn: Math.ceil(Math.max(metrics.advanceWidth(qsTr("Active")),
                                                           metrics.advanceWidth(qsTr("Activate"))))
                                       + 2 * Theme.spacingLarge

    visible: count > 0
    clip: true
    implicitHeight: count <= 0
                    ? 0
                    : (maxVisibleRows > 0 ? Math.min(count, maxVisibleRows) : count) * rowHeight
    model: RuntimeInstaller.installedBuilds
    interactive: root.scrollable
    ScrollBar.vertical: ScrollBar { policy: root.scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    delegate: Rectangle {
        id: buildRow
        required property int index
        required property string tag
        required property string build
        required property string backendDisplay
        required property string serverPath
        required property bool binaryFound
        required property bool active
        required property double sizeBytes

        function showPopup(popup, button) {
            const margin = Theme.spacing
            const anchor = button.mapToItem(popup.parent, 0, button.height + 2)
            popup.x = Math.max(margin,
                               Math.min(anchor.x, popup.parent.width - popup.width - margin))
            popup.y = anchor.y + popup.height + margin < popup.parent.height
                    ? anchor.y
                    : Math.max(margin, button.mapToItem(popup.parent, 0, -popup.height - 2).y)
            popup.open()
        }

        width: root.width
        height: root.rowHeight
        color: active ? Theme.surfaceSunken : "transparent"
        border.color: active ? Theme.accent : "transparent"
        border.width: active ? 1 : 0
        radius: Theme.radius

        Item {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: controls.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.textPrimary
                    text: buildRow.build.length ? buildRow.build : buildRow.tag
                }
                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: buildRow.binaryFound
                          ? (buildRow.backendDisplay.length
                             ? buildRow.backendDisplay
                             : buildRow.tag)
                          : qsTr("%1 — binary missing").arg(buildRow.tag)
                }
            }

            RowLayout {
                id: controls
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                LLOLabel {
                    Layout.preferredWidth: root.sizeColumn
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: root.fmtBytes(buildRow.sizeBytes)
                }

                LLOButton {
                    Layout.minimumWidth: root.actionColumn
                    text: buildRow.active ? qsTr("Active") : qsTr("Activate")
                    enabled: !buildRow.active && !RuntimeInstaller.busy
                             && Runtime.state !== Runtime.Starting && buildRow.binaryFound
                    onClicked: {
                        if (Runtime.state === Runtime.Ready)
                            Runtime.stopServer()
                        const err = RuntimeInstaller.activateBuild(buildRow.index)
                        if (err.length)
                            root.actionError(err)
                    }
                }

                LLOButton {
                    id: menuButton
                    implicitWidth: 26
                    text: ""
                    subtle: true
                    enabled: !RuntimeInstaller.busy
                    onClicked: {
                        rowMenu.measureLabels()
                        buildRow.showPopup(rowMenu, menuButton)
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                width: 2.5
                                height: 2.5
                                radius: 1.25
                                color: !menuButton.enabled ? Theme.textMuted
                                     : menuButton.hovered ? Theme.textPrimary : Theme.textSecondary
                            }
                        }
                    }
                }
            }//RowLayout
        }

        Popup {
            id: rowMenu
            parent: Overlay.overlay
            padding: 2
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            property real labelColumn: 0

            function measureLabels() {
                let w = 0
                for (const label of [qsTr("Open folder"), qsTr("Delete build")]) {
                    w = Math.max(w, metrics.advanceWidth(label))
                }
                labelColumn = w
            }

            width: labelColumn + 2 * Theme.spacing + 2 * padding

            background: Rectangle {
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius
            }

            contentItem: Column {
                spacing: 0

                MenuItem {
                    width: rowMenu.availableWidth
                    height: Theme.controlHeight
                    text: ""
                    enabled: buildRow.binaryFound
                    onTriggered: {
                        rowMenu.close()
                        const err = RuntimeInstaller.openBuildFolder(buildRow.index)
                        if (err.length)
                            root.actionError(err)
                    }

                    LLOLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: parent.enabled ? Theme.textPrimary : Theme.textMuted
                        text: qsTr("Open folder")
                    }
                }
                MenuItem {
                    width: rowMenu.availableWidth
                    height: Theme.controlHeight
                    text: ""
                    enabled: !buildRow.active && !RuntimeInstaller.busy
                    onTriggered: {
                        rowMenu.close()
                        deleteBuildDialog.pendingIndex = buildRow.index
                        deleteBuildDialog.pendingLabel = buildRow.build.length
                                ? buildRow.build : buildRow.tag
                        deleteBuildDialog.open()
                    }

                    LLOLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: parent.enabled ? Theme.textPrimary : Theme.textMuted
                        text: qsTr("Delete build")
                    }
                }
            }//contentItem
        }//Popup

        Dialog {
            id: deleteBuildDialog
            parent: Overlay.overlay
            anchors.centerIn: parent
            modal: true
            width: 380
            title: qsTr("Delete build")

            property int pendingIndex: -1
            property string pendingLabel: ""

            onOpened: deleteButton.forceActiveFocus()

            contentItem: LLOLabel {
                width: deleteBuildDialog.availableWidth
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: Theme.textSecondary
                text: qsTr("%1 will be deleted from disk.").arg(deleteBuildDialog.pendingLabel)
            }

            footer: Item {
                implicitHeight: buttons.implicitHeight + Theme.spacingLarge

                RowLayout {
                    id: buttons
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: Theme.spacingLarge
                    anchors.rightMargin: Theme.spacingLarge
                    anchors.bottomMargin: Theme.spacing
                    spacing: Theme.spacing

                    Item { Layout.fillWidth: true }
                    LLOButton {
                        text: qsTr("Cancel")
                        onClicked: deleteBuildDialog.close()
                    }
                    LLOButton {
                        id: deleteButton
                        text: qsTr("Delete")
                        emphasis: true
                        Keys.onEscapePressed: deleteBuildDialog.close()
                        onClicked: {
                            const err = RuntimeInstaller.removeBuild(deleteBuildDialog.pendingIndex)
                            deleteBuildDialog.close()
                            if (err.length)
                                root.actionError(err)
                        }
                    }
                }
            }
        }
    }
}
