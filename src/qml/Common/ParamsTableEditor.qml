pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var model

    property real nameWidth: 0.24
    property real valueWidth: 0.24
    property string valuePlaceholder: qsTr("value")

    property var setValue: null

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

                readonly property var choices: model.choices || []
                readonly property int choiceIndex: model.choiceIndex !== undefined ? model.choiceIndex : -1

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
                    placeholderText: root.valuePlaceholder
                    text: paramRow.model.valueText
                    visible: paramRow.choices.length === 0

                    onEditingFinished: {
                        if (text === paramRow.model.valueText)
                            return
                        if (!root.setValue(paramRow.index, text))
                            text = Qt.binding(() => paramRow.model.valueText)
                    }
                }

                // A parameter with named options is a closed set: the choices
                // come from the request profile, the value from the model.
                ComboBox {
                    id: choiceBox
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * root.valueWidth
                    implicitHeight: Theme.controlHeight
                    visible: paramRow.choices.length > 0
                    model: paramRow.choices
                    textRole: "label"
                    valueRole: "value"

                    Component.onCompleted: currentIndex = paramRow.choiceIndex
                    onActivated: (index) => {
                        const choice = paramRow.choices[index]
                        if (!root.setValue(paramRow.index, choice.value))
                            choiceBox.currentIndex = paramRow.choiceIndex
                    }

                    // The value can change outside the control (a reset of the
                    // profile); the option follows the model.
                    Connections {
                        target: root.model
                        function onDataChanged(topLeft, bottomRight, roles) {
                            if (topLeft.row <= paramRow.index && paramRow.index <= bottomRight.row)
                                choiceBox.currentIndex = paramRow.choiceIndex
                        }
                    }
                }

                LLOLabel {
                    id: descriptionLabel
                    anchors.left: valueField.right
                    anchors.leftMargin: Theme.spacing
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: paramRow.model.description
                }
            }
        }
    }
}
