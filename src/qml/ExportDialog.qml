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

    // When set (0 = all, 1 = current, 2 = range), the dialog opens with that
    // scope preselected instead of the default; consumed on the first show.
    property int presetScope: -1

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
        if (exportOptionsDialog.presetScope === 1)
            scopeCurrent.checked = true
        else if (exportOptionsDialog.presetScope === 2)
            scopeRange.checked = true
        else
            scopeAll.checked = true
        exportOptionsDialog.presetScope = -1
        fromSpin.value = 1
        toSpin.value = Controller.pageCount
    }

    onAccepted: {
        exportOptionsDialog.exportRequested(
            scopeCurrent.checked ? 1 : (scopeRange.checked ? 2 : 0),
            fromSpin.value, toSpin.value)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        ButtonGroup { id: scopeGroup }

        LLORadioButton {
            id: scopeAll
            text: qsTr("All recognized pages")
            font.pixelSize: Theme.captionSize
            checked: true
            ButtonGroup.group: scopeGroup
        }
        LLORadioButton {
            id: scopeCurrent
            text: qsTr("Current page (%1)").arg(Controller.currentPage + 1)
            font.pixelSize: Theme.captionSize
            ButtonGroup.group: scopeGroup
        }
        LLORadioButton {
            id: scopeRange
            text: qsTr("Page range")
            font.pixelSize: Theme.captionSize
            ButtonGroup.group: scopeGroup
        }

        RowLayout {
            Layout.leftMargin: 24
            Layout.topMargin: 2
            spacing: Theme.spacing
            enabled: scopeRange.checked

            LLOLabel {
                text: qsTr("from")
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            LLOSpinBox {
                id: fromSpin
                implicitWidth: 100
                editable: true
                font.pixelSize: Theme.captionSize
                from: 1
                to: Controller.pageCount
            }
            LLOLabel {
                text: qsTr("to")
                color: scopeRange.checked ? Theme.textSecondary : Theme.textMuted
            }
            LLOSpinBox {
                id: toSpin
                implicitWidth: 100
                editable: true
                font.pixelSize: Theme.captionSize
                from: 1
                to: Controller.pageCount
            }
        }

        Item { implicitHeight: 6 }

        LLOLabel {
            Layout.fillWidth: true
            font.pixelSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Only recognized pages inside the selection are exported.")
        }

        Item { Layout.fillHeight: true }
    }
}
