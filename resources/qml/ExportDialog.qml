import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Dialog {
    id: exportOptionsDialog
    title: qsTr("Export — pages")
    modal: true
    anchors.centerIn: parent
    width: 360
    standardButtons: Dialog.Ok | Dialog.Cancel

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ButtonGroup { id: scopeGroup }

        RadioButton {
            id: scopeAll
            text: qsTr("All recognized pages")
            checked: true
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeCurrent
            text: qsTr("Current page (%1)").arg(controller.currentPage + 1)
            ButtonGroup.group: scopeGroup
        }
        RadioButton {
            id: scopeRange
            text: qsTr("Page range")
            ButtonGroup.group: scopeGroup
        }

        RowLayout {
            Layout.leftMargin: 28
            spacing: 6
            enabled: scopeRange.checked

            Label { text: qsTr("from") }
            SpinBox {
                id: fromSpin
                from: 1
                to: controller.pageCount
                editable: true
            }
            Label { text: qsTr("to") }
            SpinBox {
                id: toSpin
                from: 1
                to: controller.pageCount
                editable: true
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: qsTr("Only recognized pages inside the selection are exported.")
        }
    }
}