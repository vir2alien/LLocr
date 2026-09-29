pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property string role: "ocr"
    property var runtimeSettingsRef: null

    readonly property bool externalMode: Settings.connectionMode === "external"

    readonly property var profiles: LaunchProfiles

    readonly property bool modelProfileMissing: role === "check"
        ? profiles.checkModelProfileMissing
        : profiles.modelProfileMissing

    readonly property string runtimeNote: role === "check"
        ? profiles.checkModelRuntimeNote
        : profiles.modelRuntimeNote

    function loadValues() {
        root.profiles.reloadDraft()
    }

    function saveValues() {
        root.profiles.saveDraft()
    }

    function resetValues() {
        root.profiles.loadDefaultDraft()
    }

    readonly property real nameWidth: 0.24
    readonly property real valueWidth: 0.24
    readonly property real removeWidth: 28

    Component.onCompleted: syncPresetModel()

    function syncPresetModel() {
        presetListModel.clear()
        for (let i = 0; i < root.profiles.presetIds.length; ++i)
            presetListModel.append({ name: root.profiles.presetNames[i] })
        const idx = root.profiles.presetIds.indexOf(root.profiles.draftProfileId)
        profileBox.currentIndex = idx >= 0 ? idx : 0
    }

    Connections {
        target: root.profiles
        function onDraftProfileChanged() { syncPresetModel() }
        function onActiveProfileChanged() { syncPresetModel() }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !root.externalMode
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.modelProfileMissing
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.warning
            text: qsTr("This model is not in the catalog, so its launch parameters come from the fallback set instead of the model's own. The context window may be wrong for it — check the log if a page comes out truncated.")
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.runtimeNote.length > 0
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.warning
            text: qsTr("The managed runtime cannot run this model. %1").arg(root.runtimeNote)
        }

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
                onActivated: root.profiles.selectDraftProfile(
                                 root.profiles.presetIds[currentIndex])
            }
            LLOButton {
                text: qsTr("Restore profile")
                onClicked: root.resetValues()
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            visible: root.profiles.otherPresetNames.length > 0

            LLOLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Theme.captionSize
                color: Theme.helpColor
                text: qsTr("The list follows the build that is installed. These profiles need another one: %1.").arg(root.profiles.otherPresetNames.join(", "))
            }
            LLOButton {
                text: qsTr("Runtime settings…")
                onClicked: {
                    if (root.runtimeSettingsRef)
                        root.runtimeSettingsRef.show()
                }
            }
        }

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
            model: root.profiles.draftModel

            delegate: Item {
                id: paramRow

                required property int index
                required property string name
                required property string valueText
                required property string description
                required property bool editable

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
                    enabled: paramRow.editable
                    placeholderText: qsTr("(flag)")
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
                    anchors.right: removeButton.left
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    font.pointSize: Theme.captionSize
                    color: Theme.textMuted
                    text: paramRow.description
                }

                Button {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.removeWidth
                    height: Theme.controlHeight
                    flat: true
                    visible: paramRow.editable
                    text: "\u2715"
                    font.pointSize: Theme.captionSize
                    onClicked: root.profiles.removeDraftRow(paramRow.index)
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
                placeholderText: qsTr("(flag)")
            }
            LLOButton {
                text: qsTr("Add")
                onClicked: {
                    if (root.profiles.appendDraftParameter(newParamName.text,
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
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("llama-server command-line parameters; --model/--mmproj/--alias/--host/--port come from the other launch settings. The greyed-out rows are the shared server policy and apply to every model on this machine.")
        }
    }

    // External server: launch settings belong to the managed runtime only.
    // spacing 6 matches the LocationTab external pane.
    ColumnLayout {
        anchors.fill: parent
        visible: root.externalMode
        spacing: 6

        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("The model is managed by the external server. Location and download settings are not available in this mode.")
        }

        LLOButton {
            text: qsTr("Configure runtime…")
            onClicked: {
                if (root.runtimeSettingsRef)
                    root.runtimeSettingsRef.show()
            }
        }

        Item { Layout.fillHeight: true }
    }
}
