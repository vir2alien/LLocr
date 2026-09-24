pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // false = the recognition (OCR) request profile, true = the verification
    // (check) request profile.
    property bool checkRole: false

    readonly property var profiles: checkRole ? RequestProfilesValidate
                                              : RequestProfilesOcr

    function loadValues() {
        if (!checkRole) {
            var midx = modelBox.model.indexOf(Controller.modelIdToName(Settings.modelRecipeId))
            modelBox.currentIndex = midx >= 0 ? midx : 0
        }
        root.profiles.reloadDraft()
    }

    function saveValues() {
        root.profiles.saveDraft()
    }

    // Restore defaults: loads the default profile into the draft (uncommitted
    // until Save).
    function resetValues() {
        root.profiles.loadDefaultDraft()
    }

    // Column proportions shared by the header and the delegates.
    readonly property real nameWidth: 0.28
    readonly property real valueWidth: 0.26

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Profile")
            }

            ComboBox {
                id: modelBox
                visible: !root.checkRole
                Layout.preferredWidth: root.width * 0.4
                implicitHeight: Theme.controlHeight
                model: Controller.modelNames
                onActivated: {
                    root.profiles.selectDraftProfile(
                                Controller.modelNameToId(modelBox.currentText))
                }
            }

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
            model: root.profiles.draftModel

            delegate: Item {
                id: paramRow

                required property int index
                required property string name
                required property string valueText
                required property string description

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
                    text: paramRow.name
                }

                TextField {
                    id: valueField
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * root.valueWidth
                    implicitHeight: Theme.controlHeight
                    selectByMouse: true
                    text: paramRow.valueText

                    onEditingFinished: {
                        if (text === paramRow.valueText)
                            return
                        if (!root.profiles.setDraftValue(paramRow.index, text))
                            text = Qt.binding(() => paramRow.valueText)
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
                    text: paramRow.description
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
                placeholderText: qsTr("value")
            }
            LLOButton {
                text: qsTr("Add")
                onClicked: {
                    if (root.profiles.appendDraftRow(newParamName.text,
                                                     newParamValue.text)) {
                        newParamName.text = ""
                        newParamValue.text = ""
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: checkRole
                ? qsTr("Advanced request parameters sent to llama.cpp alongside the check prompt; see the llama.cpp server documentation")
                : qsTr("Advanced request parameters sent to llama.cpp alongside the OCR prompt; see the llama.cpp server documentation")
        }
    }
}
