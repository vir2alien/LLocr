pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            text: qsTr("Blocks whose type is checked below are verified automatically "
                       + "when verification runs. Unchecked types are skipped.")
        }

        ListView {
            id: blocksList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: Verification.blockModel

            delegate: Item {
                id: blockRow

                required property int index
                required property string type
                required property string name
                required property bool enabled

                width: blocksList.width
                implicitHeight: Math.max(Theme.controlHeight, 28)

                CheckBox {
                    id: enabledCheck
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    checked: blockRow.enabled
                    onToggled: {
                        Verification.blockModel.setEnabled(blockRow.index, checked)
                    }
                }

                LLOLabel {
                    id: nameLabel
                    anchors.left: enabledCheck.right
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: blockRow.name.length > 0 ? blockRow.name : blockRow.type
                    font.bold: false
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }

                LLOLabel {
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.spacing
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    text: blockRow.type
                }
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            text: qsTr("Blocks without OCR text (image, chart) are always skipped.")
        }
    }
}