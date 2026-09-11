import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    function loadValues() {
        RequestProfiles.reloadDraft()
    }

    function saveValues() {
        RequestProfiles.saveDraft()
    }

    function resetValues() {
        RequestProfiles.loadDefaultDraft()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { Layout.columnSpan: 2; implicitHeight: 4 }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOLabel {
                Layout.preferredWidth: root.width * 0.45
                font.bold: true
                text: qsTr("Parameter")
            }
            LLOLabel {
                Layout.fillWidth: true
                font.bold: true
                text: qsTr("Value")
            }
        }

        ListView {
            id: paramsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: RequestProfiles.draftModel

            delegate: Item {
                width: paramsList.width
                implicitHeight: Theme.controlHeight

                LLOLabel {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.3
                    elide: Text.ElideRight
                    text: model.name
                }

                TextField {
                    id: valueField
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.7
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    text: model.valueText

                    onEditingFinished: {
                        if (text === model.valueText)
                            return
                        if (!RequestProfiles.setDraftValue(index, text))
                            text = model.valueText
                    }
                }
            }
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Advanced request parameters sent to llama.cpp alongside the OCR prompt; see the llama.cpp server documentation")
        }
    }
}
