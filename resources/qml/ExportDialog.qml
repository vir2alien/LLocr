pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "Common"

Dialog {
    id: exportOptionsDialog
    title: qsTr("Export — pages")
    modal: true
    anchors.centerIn: parent
    width: 360
    implicitHeight: 250

    signal exportRequested(int scope, int fromPage, int toPage)

    background: Rectangle {
        color: Theme.surface
        radius: Theme.dialogRadius
        border.color: Theme.border
        border.width: 1
    }

    header: Item {
        implicitHeight: 38
        LLOLabel {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: exportOptionsDialog.title
            font.bold: true
            color: Theme.textPrimary
        }
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 2 * Theme.spacingLarge
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.paddingWindow
            anchors.rightMargin: Theme.paddingWindow
            anchors.topMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacing

            Item { Layout.fillWidth: true }

            LLOButton {
                text: qsTr("Cancel")
                onClicked: exportOptionsDialog.reject()
            }
            LLOButton {
                emphasis: true
                text: qsTr("Export")
                onClicked: exportOptionsDialog.accept()
            }
        }
    }

    onAboutToShow: {
        scopeAll.checked = true
        fromSpin.value = 1
        toSpin.value = Controller.pageCount
    }

    onAccepted: {
        exportOptionsDialog.exportRequested(
            scopeCurrent.checked ? 1 : (scopeRange.checked ? 2 : 0),
            fromSpin.value, toSpin.value)
    }

    component CompactSpinBox: SpinBox {
        id: sb
        implicitHeight: Theme.controlHeight
        implicitWidth: 100
        editable: true
        font.pointSize: Theme.captionSize

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
                font.pointSize: Theme.iconSize
                color: sb.enabled && sb.value < sb.to ? Theme.textSecondary : Theme.textMuted
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
                font.pointSize: Theme.iconSize
                color: sb.enabled && sb.value > sb.from ? Theme.textSecondary : Theme.textMuted
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
            font.pointSize: Theme.captionSize
            checked: true
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeCurrent
            text: qsTr("Current page (%1)").arg(Controller.currentPage + 1)
            font.pointSize: Theme.captionSize
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeRange
            text: qsTr("Page range")
            font.pointSize: Theme.captionSize
            ButtonGroup.group: scopeGroup
        }

        RowLayout {
            Layout.leftMargin: 24
            Layout.topMargin: 2
            spacing: 8
            enabled: scopeRange.checked

            LLOLabel {
                text: qsTr("from")
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            CompactSpinBox {
                id: fromSpin
                from: 1
                to: Controller.pageCount
            }
            LLOLabel {
                text: qsTr("to")
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            CompactSpinBox {
                id: toSpin
                from: 1
                to: Controller.pageCount
            }
        }

        Item { implicitHeight: 6 }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Only recognized pages inside the selection are exported.")
        }

        Item { Layout.fillHeight: true }
    }
}
