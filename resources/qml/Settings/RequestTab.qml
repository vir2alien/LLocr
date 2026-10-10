pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    // Model-profile role id: "ocr", "blockRecognition", "decision" or "layout".
    property string role: "ocr"
    readonly property bool decisionRole: role === "decision"
    readonly property bool hasParams: profiles.draftModel.count > 0
    readonly property real nameWidth: 0.28
    readonly property real valueWidth: 0.26
    readonly property var profiles: role === "blockRecognition" ? RequestProfilesValidate
                                  : role === "decision" ? RequestProfilesDecision
                                  : role === "layout" ? RequestProfilesLayout
                                  : RequestProfilesOcr

    function loadValues() {
        root.profiles.reloadDraft()
        thresholdSpin.value = Math.round(Settings.decisionMatchThreshold * 100)
    }

    function saveValues() {
        root.profiles.saveDraft()
        if (root.decisionRole)
            Settings.decisionMatchThreshold = thresholdSpin.value / 100
    }

    function resetValues() {
        root.profiles.loadDefaultDraft()
        if (root.decisionRole)
            thresholdSpin.value = Math.round(Settings.decisionMatchThreshold * 100)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Item { implicitHeight: 4 }

        RowLayout {
            visible: root.decisionRole
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Match threshold (%)")
            }
            SpinBox {
                id: thresholdSpin
                implicitHeight: Theme.controlHeight
                from: 0
                to: 100
                stepSize: 5
                editable: true
            }
            Item { Layout.fillWidth: true }
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.decisionRole
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("The decision model answers with the probability that the text matches the image. A block counts as correct when the probability reaches this threshold; the rest are re-recognized when automatic re-recognition is on.")
        }

        ParamsTableEditor {
            visible: root.hasParams
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.profiles.draftModel
            nameWidth: root.nameWidth
            valueWidth: root.valueWidth
            setValue: (row, text) => root.profiles.setDraftValue(row, text)
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.decisionRole && !root.hasParams
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("The decision model answers in a single forward pass and generates no text, so it has no sampling parameters — the threshold above is its only setting here.")
        }

        LLOLabel {
            Layout.fillWidth: true
            visible: root.hasParams
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            wrapMode: Text.WordWrap
            text: root.decisionRole
                ? qsTr("Request parameters for the decision endpoint. The set comes from the model profile — edit the values, not the list.")
                : role === "blockRecognition"
                ? qsTr("Sampling parameters sent with every block OCR request. The set comes from the model profile — edit the values, not the list.")
                : role === "layout"
                ? qsTr("Sampling parameters sent with every page markup request. The set comes from the model profile — edit the values, not the list.")
                : qsTr("Sampling parameters sent with every recognition request. The set comes from the model profile — edit the values, not the list.")
        }

        // Without the parameter table there is nothing to absorb the free
        // height, and the layout stretches the remaining rows across the whole
        // column; the spring keeps them packed at the top.
        Item {
            visible: !root.hasParams
            Layout.fillHeight: true
        }
    }
}
