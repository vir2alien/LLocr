import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // The table edits the draft copy of the selected preset only. Save commits
    // the draft (selection + rows) to the launch profile store, Cancel/reopen
    // discards it (reloadDraft()).
    function loadValues() {
        LaunchProfiles.reloadDraft()
    }

    function saveValues() {
        LaunchProfiles.saveDraft()
    }

    // Restore defaults: loads the built-in rows of the edited preset into the
    // draft (uncommitted until Save).
    function resetValues() {
        LaunchProfiles.loadDefaultDraft()
    }

    // Column proportions shared by the header and the delegates.
    readonly property real nameWidth: 0.24
    readonly property real valueWidth: 0.24
    readonly property real removeWidth: 28

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
                id: profileBox
                Layout.preferredWidth: root.width * 0.4
                implicitHeight: Theme.controlHeight
                textRole: "name"
                model: ListModel { id: presetListModel }
                onActivated: LaunchProfiles.selectDraftProfile(
                                 LaunchProfiles.presetIds[currentIndex])
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
                anchors.rightMargin: root.removeWidth + Theme.spacing
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
            model: LaunchProfiles.draftModel

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
                    placeholderText: qsTr("(flag)")
                    text: model.valueText

                    onEditingFinished: {
                        if (text === model.valueText)
                            return
                        if (!LaunchProfiles.setDraftValue(index, text))
                            text = model.valueText
                    }
                }

                LLOLabel {
                    id: descriptionLabel
                    anchors.left: valueField.right
                    anchors.leftMargin: Theme.spacing
                    anchors.right: removeButton.left
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: model.description
                }

                Button {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.removeWidth
                    height: Theme.controlHeight
                    flat: true
                    text: "\u2715"
                    font.pointSize: Theme.captionSize
                    onClicked: LaunchProfiles.removeDraftRow(index)
                }
            }
        }

        // Add-parameter row.
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
                placeholderText: qsTr("(flag)")
            }
            LLOButton {
                text: qsTr("Add")
                onClicked: {
                    if (LaunchProfiles.appendDraftParameter(newParamName.text,
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
            color: Theme.textMuted
            text: qsTr("llama-server command-line parameters; --model/--mmproj/--alias/--host/--port come from the other launch settings")
        }
    }

    Component.onCompleted: syncPresetModel()

    Connections {
        target: LaunchProfiles
        function onDraftProfileChanged() { syncPresetModel() }
        function onActiveProfileChanged() { syncPresetModel() }
    }

    // currentIndex is assigned imperatively: a declarative binding would be
    // broken by the user's own combobox interaction.
    function syncPresetModel() {
        presetListModel.clear()
        for (let i = 0; i < LaunchProfiles.presetIds.length; ++i)
            presetListModel.append({ name: LaunchProfiles.presetNames[i] })
        const idx = LaunchProfiles.presetIds.indexOf(LaunchProfiles.draftProfileId)
        profileBox.currentIndex = idx >= 0 ? idx : 0
    }
}
