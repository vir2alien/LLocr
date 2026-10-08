pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool checkRole: false
    readonly property real nameWidth: 0.28
    readonly property real valueWidth: 0.26
    readonly property var profiles: checkRole ? RequestProfilesValidate
                                              : RequestProfilesOcr

    function loadValues() {
        root.profiles.reloadDraft()
    }

    function saveValues() {
        root.profiles.saveDraft()
    }

    function resetValues() {
        root.profiles.loadDefaultDraft()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        ParamsTableEditor {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.profiles.draftModel
            nameWidth: root.nameWidth
            valueWidth: root.valueWidth
            setValue: (row, text) => root.profiles.setDraftValue(row, text)
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: checkRole
                ? qsTr("Sampling parameters sent with every check request. The set comes from the model profile — edit the values, not the list.")
                : qsTr("Sampling parameters sent with every recognition request. The set comes from the model profile — edit the values, not the list.")
        }
    }
}
