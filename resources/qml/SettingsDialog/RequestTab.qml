import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    function loadValues() {
        var midx = modelBox.model.indexOf(Controller.modelIdToName(Settings.modelRecipeId))
        modelBox.currentIndex = midx >= 0 ? midx : 0
        RequestProfiles.reloadDraft()
    }

    function saveValues() {
        RequestProfiles.saveDraft()
    }

    // Restore defaults: loads the default profile into the draft (uncommitted
    // until Save).
    function resetValues() {
        RequestProfiles.loadDefaultDraft()
    }

    // Column proportions shared by the header and the delegates.
    readonly property real nameWidth: 0.28
    readonly property real valueWidth: 0.26

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        LLOLabel {
            text: qsTr("OCR model")
        }

        ComboBox {
            id: modelBox
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            model: Controller.modelNames
            onActivated: {
                RequestProfiles.selectDraftProfile(
                            Controller.modelNameToId(modelBox.currentText))
            }
        }

        Item { implicitHeight: 6 }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOButton {
                text: qsTr("Restore profile")
                onClicked: root.resetValues()
            }

            Item { Layout.fillWidth: true }
        }

        // Header, anchored like the delegate rows so the columns line up.
        Item {
            Layout.fillWidth: true
            implicitHeight: headerValue.implicitHeight

            LLOLabel {
                id: headerName
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
            model: RequestProfiles.draftModel

            delegate: Item {
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
                    text: model.name
                }

                TextField {
                    id: valueField
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * root.valueWidth
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    text: model.valueText

                    onEditingFinished: {
                        if (text === model.valueText)
                            return
                        if (!RequestProfiles.setDraftValue(index, text))
                            text = Qt.binding(() => model.valueText)
                    }
                }

                LLOLabel {
                    id: descriptionLabel
                    anchors.left: valueField.right
                    anchors.leftMargin: Theme.spacing
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: model.description
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
