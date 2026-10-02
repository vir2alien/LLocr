pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var model

    property real nameWidth: 0.24
    property real valueWidth: 0.24
    property bool removable: false
    property string valuePlaceholder: qsTr("value")

    property var setValue: null
    property var addRow: null
    property var removeRow: null

    readonly property real removeWidth: 28

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item {
            Layout.fillWidth: true
            implicitHeight: headerValue.implicitHeight

            LLOLabel {
                anchors.left: parent.left
                width: parent.width * root.nameWidth
                font.bold: true
                text: qsTr("Parameter")
            }
            LLOLabel {
                id: headerValue
                anchors.left: parent.left
                anchors.leftMargin: parent.width * root.nameWidth + Theme.spacing
                width: parent.width * root.valueWidth
                font.bold: true
                text: qsTr("Value")
            }
            LLOLabel {
                anchors.left: headerValue.right
                anchors.leftMargin: Theme.spacing
                anchors.right: parent.right
                anchors.rightMargin: root.removable ? root.removeWidth + Theme.spacing : 0
                font.bold: true
                text: qsTr("Description")
            }
        }

        ListView {
            id: paramsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: root.model

            delegate: Item {
                id: paramRow

                required property int index
                required property var model

                // The Launch store marks rows a layer owns as read-only; the
                // Request store has no such role, and an absent one means the
                // row is editable.
                readonly property bool editable: paramRow.model.editable !== false

                width: paramsList.width
                implicitHeight: Math.max(Theme.controlHeight,
                                         descriptionLabel.implicitHeight)

                LLOLabel {
                    id: nameLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * root.nameWidth
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    text: paramRow.model.name
                }

                TextField {
                    id: valueField
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * root.valueWidth
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    enabled: paramRow.editable
                    placeholderText: root.valuePlaceholder
                    text: paramRow.model.valueText

                    onEditingFinished: {
                        if (text === paramRow.model.valueText)
                            return
                        if (!root.setValue(paramRow.index, text))
                            text = Qt.binding(() => paramRow.model.valueText)
                    }
                }

                LLOLabel {
                    id: descriptionLabel
                    anchors.left: valueField.right
                    anchors.leftMargin: Theme.spacing
                    anchors.right: root.removable ? removeButton.left : parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: paramRow.model.description
                }

                Button {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.removeWidth
                    height: Theme.controlHeight
                    flat: true
                    visible: root.removable && paramRow.editable
                    text: "✕"
                    font.pointSize: Theme.captionSize
                    onClicked: root.removeRow(paramRow.index)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            TextField {
                id: newParamName
                Layout.preferredWidth: root.width * root.nameWidth
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                placeholderText: qsTr("New parameter name")
            }
            TextField {
                id: newParamValue
                Layout.preferredWidth: root.width * root.valueWidth
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                placeholderText: root.valuePlaceholder
            }
            LLOButton {
                text: qsTr("Add")
                onClicked: {
                    if (root.addRow(newParamName.text, newParamValue.text)) {
                        newParamName.text = ""
                        newParamValue.text = ""
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}