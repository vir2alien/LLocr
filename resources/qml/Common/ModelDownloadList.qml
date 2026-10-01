pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 46
    property int maxVisibleRows: 6
    property bool isVerifyModelRole: false

    signal actionError(string message)
    signal downloadRequested(string title, string quantId, string license, string runtimeNote)

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
    readonly property int quantColumn: Math.ceil(metrics.advanceWidth("Q4_K_XL"))
                                      + 2 * Theme.spacingLarge
    readonly property int actionColumn: Math.ceil(Math.max(metrics.advanceWidth(qsTr("Use")),
                                                             metrics.advanceWidth(qsTr("Download")),
                                                             metrics.advanceWidth(qsTr("Active"))))
                                       + 2 * Theme.spacingLarge

    visible: count > 0
    clip: true
    implicitHeight: count <= 0
                    ? 0
                    : (maxVisibleRows > 0 ? Math.min(count, maxVisibleRows) : count) * rowHeight
    model: isVerifyModelRole ? ModelInstaller.checkQuantModels : ModelInstaller.quantModels
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: row

        required property string key
        required property string title
        required property string subtitle
        required property bool profile
        required property string license
        required property string runtimeNote
        required property var quants
        required property int selected
        required property string selectedLabel
        required property bool selectedInstalled
        required property bool selectedActive
        required property double selectedSize
        required property bool selectedDownloadable
        required property bool active

        readonly property bool anyInstalled: {
            for (let i = 0; i < quants.length; ++i) {
                if (quants[i].installed)
                    return true
            }
            return false
        }

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
                    text: row.title
                }
                LLOLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: row.subtitle
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
                    text: row.selectedInstalled && row.selectedSize > 0
                          ? root.fmtBytes(row.selectedSize) : ""
                }

                LLOButton {
                    id: quantButton
                    visible: row.profile
                    Layout.minimumWidth: root.quantColumn
                    text: row.selectedLabel
                    enabled: quants.length > 1 && !ModelInstaller.busy
                    rightPadding: 18
                    onClicked: {
                        quantPopup.measureColumns()
                        row.showPopup(quantPopup, quantButton)
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"
                        font.pointSize: Theme.captionSize
                        color: Theme.textSecondary
                    }
                }

                LLOButton {
                    Layout.minimumWidth: root.actionColumn
                    text: row.selectedActive ? qsTr("Active")
                          : row.selectedInstalled ? qsTr("Use") : qsTr("Download")
                    enabled: !row.selectedActive && !ModelInstaller.busy
                             && (row.selectedInstalled ? true : row.selectedDownloadable)
                    onClicked: {
                        if (row.selectedInstalled) {
                            const err = ModelInstaller.useQuant(row.key, row.selectedLabel,
                                                                root.isVerifyModelRole)
                            if (err.length)
                                root.actionError(err)
                            return
                        }
                        ModelInstaller.downloadQuant(row.key, row.selectedLabel,
                                                     root.isVerifyModelRole)
                        root.downloadRequested(row.title, row.selectedLabel, row.license,
                                               row.runtimeNote)
                    }
                }

                LLOButton {
                    id: menuButton
                    implicitWidth: 26
                    text: ""
                    subtle: true
                    enabled: !ModelInstaller.busy
                    onClicked: {
                        rowMenu.measureLabels()
                        row.showPopup(rowMenu, menuButton)
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
            id: quantPopup
            parent: Overlay.overlay
            padding: 2
            width: chrome + nameColumn + sizeColumn + 2 * padding
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            property real nameColumn: 0
            property real sizeColumn: 0
            readonly property int chrome: 10 + 4 * Theme.spacingSmall

            background: Rectangle {
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
                radius: Theme.controlRadius
            }

            function measureColumns() {
                let name = 0
                let size = 0
                for (let i = 0; i < row.quants.length; ++i) {
                    const quant = row.quants[i]
                    name = Math.max(name, metrics.advanceWidth(quant.id))
                    if (quant.size > 0)
                        size = Math.max(size, metrics.advanceWidth(root.fmtBytes(quant.size)))
                }
                nameColumn = name
                sizeColumn = size
            }

            contentItem: Column {
                spacing: 0

                Repeater {
                    id: quantRepeater
                    model: row.quants
                    delegate: MenuItem {
                        required property var modelData

                        text: ""
                        height: Theme.controlHeight
                        width: quantPopup.chrome + quantPopup.nameColumn
                               + quantPopup.sizeColumn
                        onTriggered: {
                            ModelInstaller.selectQuant(row.key, modelData.id)
                            quantPopup.close()
                        }

                        Item {
                            id: entryRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall

                            Text {
                                id: entryMark
                                x: 0
                                width: 10
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.id === row.selectedLabel
                                horizontalAlignment: Text.AlignHCenter
                                text: "✓"
                                font.pointSize: Theme.captionSize
                                color: Theme.accent
                            }
                            LLOLabel {
                                id: entryName
                                x: 10 + Theme.spacingSmall
                                width: Math.min(quantPopup.nameColumn, 200)
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                font.pointSize: Theme.captionSize
                                color: Theme.textPrimary
                                text: modelData.id
                            }
                            LLOLabel {
                                id: entrySize
                                x: 10 + 2 * Theme.spacingSmall + quantPopup.nameColumn
                                width: Math.min(quantPopup.sizeColumn, 120)
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.size > 0
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                font.pointSize: Theme.captionSize
                                color: Theme.textMuted
                                text: root.fmtBytes(modelData.size)
                            }
                        }
                    }
                }//Repeater
            }//ContentItem
        }//Popup

        Popup {
            id: rowMenu
            parent: Overlay.overlay
            padding: 2
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            property real labelColumn: 0

            function measureLabels() {
                let w = 0
                for (const label of [qsTr("Open folder"), qsTr("Delete quantization"),
                                      qsTr("Delete model")]) {
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
                    enabled: row.selectedInstalled
                    onTriggered: {
                        const err = ModelInstaller.openQuantFolder(row.key, row.selectedLabel,
                                                                   root.isVerifyModelRole)
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
                    visible: row.profile
                    text: ""
                    enabled: row.selectedInstalled && !ModelInstaller.busy
                    onTriggered: {
                        const err = ModelInstaller.removeQuant(row.key, row.selectedLabel,
                                                               root.isVerifyModelRole)
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
                        text: qsTr("Delete quantization")
                    }
                }
                MenuItem {
                    width: rowMenu.availableWidth
                    height: Theme.controlHeight
                    visible: row.profile
                    text: ""
                    enabled: row.anyInstalled && !ModelInstaller.busy
                    onTriggered: {
                        deleteModelDialog.pendingKey = row.key
                        deleteModelDialog.pendingTitle = row.title
                        deleteModelDialog.open()
                    }

                    LLOLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.NoWrap
                        font.pointSize: Theme.captionSize
                        color: parent.enabled ? Theme.textPrimary : Theme.textMuted
                        text: qsTr("Delete model")
                    }
                }
            }//contentItem
        }//Popup
    }//delegate

    Dialog {
        id: deleteModelDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 380
        title: qsTr("Delete model")

        property string pendingKey: ""
        property string pendingTitle: ""

        onOpened: deleteButton.forceActiveFocus()

        contentItem: LLOLabel {
            width: deleteModelDialog.availableWidth
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.textSecondary
            text: qsTr("%1 will be deleted from disk with every quantization of it.")
                    .arg(deleteModelDialog.pendingTitle)
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
                    onClicked: deleteModelDialog.close()
                }
                LLOButton {
                    id: deleteButton
                    text: qsTr("Delete")
                    emphasis: true
                    Keys.onEscapePressed: deleteModelDialog.close()
                    onClicked: {
                        const err = ModelInstaller.removeModelRow(deleteModelDialog.pendingKey,
                                                          root.isVerifyModelRole)
                        deleteModelDialog.close()
                        if (err.length)
                            root.actionError(err)
                    }
                }
            }
        }
    }
}
