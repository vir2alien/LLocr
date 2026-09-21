pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property int currentIndex: -1

    function selectIndex(index) {
        if (index === root.currentIndex)
            return
        if (root.currentIndex >= 0)
            savePrompt()
        root.currentIndex = index
        typeList.currentIndex = index
        loadPrompt()
    }

    function savePrompt() {
        if (root.currentIndex < 0)
            return
        Verification.blockModel.setPrompt(root.currentIndex, promptArea.text)
    }

    function loadPrompt() {
        if (root.currentIndex < 0) {
            promptArea.text = ""
            return
        }
        promptArea.text = Verification.blockModel.promptAt(root.currentIndex)
    }

    Connections {
        target: Verification
        function onModelChanged() {
            root.currentIndex = -1
            typeList.currentIndex = -1
            promptArea.text = ""
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        ListView {
            id: typeList
            Layout.preferredWidth: parent.width * 0.30
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: Verification.blockModel

            delegate: Item {
                id: typeRow

                required property int index
                required property string type
                required property string name
                required property bool enabled

                width: typeList.width
                implicitHeight: Theme.controlHeight

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.controlRadius
                    color: typeList.currentIndex === typeRow.index
                           ? Theme.selected : "transparent"
                }

                LLOLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    color: typeList.currentIndex === typeRow.index
                           ? Theme.textPrimary : Theme.textSecondary
                    text: typeRow.name.length > 0 ? typeRow.name : typeRow.type
                }

                TapHandler {
                    onTapped: root.selectIndex(typeRow.index)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSmall

            LLOLabel {
                text: root.currentIndex >= 0
                    ? qsTr("Prompt for “%1” blocks").arg(
                          Verification.blockModel.nameAt(root.currentIndex)
                              .length > 0
                              ? Verification.blockModel.nameAt(root.currentIndex)
                              : Verification.blockModel.typeAt(root.currentIndex))
                    : qsTr("Select a block type")
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: root.currentIndex >= 0
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                text: qsTr("Ask the verifier to check this kind of block against the "
                           + "image and answer with OK, FIX followed by the corrected "
                           + "block, or REVIEW.")
            }

            TextArea {
                id: promptArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: root.currentIndex >= 0
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                placeholderText: qsTr("Prompt for the selected block type")
                onEditingFinished: root.savePrompt()
            }
        }
    }
}