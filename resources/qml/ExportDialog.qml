import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

Dialog {
    id: exportOptionsDialog
    title: qsTr("Export — pages")
    modal: true
    anchors.centerIn: parent
    width: 360
    implicitHeight: 250
    standardButtons: Dialog.Ok | Dialog.Cancel

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: Item {
        implicitHeight: 38
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: exportOptionsDialog.title
            font.bold: true
            font.pixelSize: Theme.fontNormal
            color: Theme.textPrimary
        }
    }

    footer: DialogButtonBox {
        background: Rectangle {
            color: "transparent"
        }
        alignment: Qt.AlignRight
        standardButtons: exportOptionsDialog.standardButtons
        spacing: 6
        padding: 10
    }

    onAboutToShow: {
        scopeAll.checked = true
        fromSpin.value = 1
        toSpin.value = controller.pageCount
    }

    onAccepted: {
        exportDialog.scope = scopeCurrent.checked ? 1
                            : (scopeRange.checked ? 2 : 0)
        exportDialog.fromPage = fromSpin.value
        exportDialog.toPage = toSpin.value
        exportDialog.open()
    }

    component CompactSpinBox: SpinBox {
        id: sb
        implicitHeight: Theme.controlHeight
        implicitWidth: 100
        editable: true
        font.pixelSize: Theme.fontCaption

        contentItem: TextInput {
            z: 2
            text: sb.textFromValue(sb.value, sb.locale)
            font: sb.font
            color: sb.enabled ? Theme.textPrimary : Theme.textMuted
            selectionColor: Theme.selected
            selectedTextColor: Theme.textPrimary
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !sb.editable
            validator: sb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
        }

        up.indicator: Rectangle {
            x: sb.mirrored ? 0 : sb.width - width
            y: 0
            implicitWidth: 20
            implicitHeight: sb.height / 2
            color: sb.up.pressed ? Theme.selected : Theme.surfaceSunken
            border.color: Theme.divider
            border.width: 1

            Text {
                text: "▲"
                font.pixelSize: 7
                color: sb.up.enabled ? Theme.textSecondary : Theme.textMuted
                anchors.centerIn: parent
            }
        }

        down.indicator: Rectangle {
            x: sb.mirrored ? 0 : sb.width - width
            y: sb.height / 2
            implicitWidth: 20
            implicitHeight: sb.height / 2
            color: sb.down.pressed ? Theme.selected : Theme.surfaceSunken
            border.color: Theme.divider
            border.width: 1

            Text {
                text: "▼"
                font.pixelSize: 7
                color: sb.down.enabled ? Theme.textSecondary : Theme.textMuted
                anchors.centerIn: parent
            }
        }

        background: Rectangle {
            implicitWidth: 100
            border.color: Theme.border
            border.width: 1
            color: Theme.surfaceAlt
            radius: Theme.controlRadius
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        ButtonGroup { id: scopeGroup }

        RadioButton {
            id: scopeAll
            text: qsTr("All recognized pages")
            font.pixelSize: Theme.fontCaption
            checked: true
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeCurrent
            text: qsTr("Current page (%1)").arg(controller.currentPage + 1)
            font.pixelSize: Theme.fontCaption
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeRange
            text: qsTr("Page range")
            font.pixelSize: Theme.fontCaption
            ButtonGroup.group: scopeGroup
        }

        RowLayout {
            Layout.leftMargin: 24
            Layout.topMargin: 2
            spacing: 8
            enabled: scopeRange.checked

            Label {
                text: qsTr("from")
                font.pixelSize: Theme.fontCaption
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            CompactSpinBox {
                id: fromSpin
                from: 1
                to: controller.pageCount
            }
            Label {
                text: qsTr("to")
                font.pixelSize: Theme.fontCaption
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            CompactSpinBox {
                id: toSpin
                from: 1
                to: controller.pageCount
            }
        }

        Item { implicitHeight: 6 }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: qsTr("Only recognized pages inside the selection are exported.")
        }

        Item { Layout.fillHeight: true }
    }
}
