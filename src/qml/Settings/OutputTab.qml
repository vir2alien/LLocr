pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: root

    spacing: 4

    // {id, label} pairs so the combo shows translated names while
    // Settings.parserId keeps storing the stable id.
    property var parserModel: {
        var ids = Controller.parserNames;
        var labels = Controller.parserLabels;
        var out = [];
        for (var i = 0; i < ids.length; i++)
            out.push({ id: ids[i], label: labels[i] });
        return out;
    }

    // The orientation lives here, not in the Settings: the window edits the
    // controls and writes them on Save.
    property int orientationIndex: 0

    function syncParser() {
        var idx = -1;
        for (var i = 0; i < root.parserModel.length; i++) {
            if (root.parserModel[i].id === Settings.parserId) {
                idx = i;
                break;
            }
        }
        parserBox.currentIndex = idx >= 0 ? idx : 0
    }

    function loadValues() {
        syncParser()
        splitPagesCheck.checked = Settings.splitPages
        pageNumbersCheck.checked = Settings.keepHeadersAndPageNumbers
        tablesAsHtmlCheck.checked = Settings.tablesAsHtml
        orientationCheckBox.checked = Settings.orientationCheckEnabled
        orientationAutoTurnCheck.checked = Settings.orientationAutoTurnEnabled
        root.orientationIndex = Settings.pdfLandscape ? 1 : 0
        marginSpin.value = Settings.pdfMarginMm
    }

    function saveValues() {
        Settings.parserId = root.parserModel[parserBox.currentIndex].id
        Settings.splitPages = splitPagesCheck.checked
        Settings.keepHeadersAndPageNumbers = pageNumbersCheck.checked
        Settings.tablesAsHtml = tablesAsHtmlCheck.checked
        Settings.orientationCheckEnabled = orientationCheckBox.checked
        Settings.orientationAutoTurnEnabled = orientationAutoTurnCheck.checked
        Settings.pdfLandscape = root.orientationIndex === 1
        Settings.pdfMarginMm = marginSpin.value
    }

    Component.onCompleted: syncParser()

    Connections {
        target: Settings
        function onParserIdChanged() { root.syncParser() }
        function onSplitPagesChanged() { splitPagesCheck.checked = Settings.splitPages }
        function onKeepHeadersAndPageNumbersChanged() { pageNumbersCheck.checked = Settings.keepHeadersAndPageNumbers }
        function onTablesAsHtmlChanged() { tablesAsHtmlCheck.checked = Settings.tablesAsHtml }
        function onOrientationCheckEnabledChanged() { orientationCheckBox.checked = Settings.orientationCheckEnabled }
        function onOrientationAutoTurnEnabledChanged() { orientationAutoTurnCheck.checked = Settings.orientationAutoTurnEnabled }
        function onPdfLandscapeChanged() { root.orientationIndex = Settings.pdfLandscape ? 1 : 0 }
        function onPdfMarginMmChanged() { marginSpin.value = Settings.pdfMarginMm }
    }

    LLOLabel {
        text: qsTr("Output parser")
        font.pointSize: Theme.bodySize
        color: Theme.textPrimary
    }
    LLOComboBox {
        id: parserBox
        Layout.fillWidth: true
        model: root.parserModel
        textRole: "label"
    }

    Item { implicitHeight: 6 }

    OptionCheck {
        id: splitPagesCheck
        label: qsTr("Split pages")
    }

    OptionCheck {
        id: pageNumbersCheck
        label: qsTr("Keep headers and page numbers")
    }

    OptionCheck {
        id: tablesAsHtmlCheck
        label: qsTr("Keep tables as HTML")
    }

    OptionCheck {
        id: orientationCheckBox
        label: qsTr("Check the page orientation")
    }

    OptionCheck {
        id: orientationAutoTurnCheck
        visible: orientationCheckBox.checked
        label: qsTr("Turn a sideways page after markup automatically")
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("PDF export")
        font.pointSize: Theme.bodySize
        color: Theme.textPrimary
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            text: qsTr("Orientation")
            font: Theme.bodySmall
        }
        SegmentedToggle {
            Layout.fillWidth: true
            options: [
                { label: qsTr("Portrait") },
                { label: qsTr("Landscape") }
            ]
            currentIndex: root.orientationIndex
            onActivated: (idx) => root.orientationIndex = idx
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        LLOLabel {
            text: qsTr("Margins (mm)")
            font: Theme.bodySmall
        }
        SpinBox {
            id: marginSpin
            Layout.preferredWidth: 110
            implicitHeight: Theme.controlHeight
            font.pointSize: Theme.bodySmallSize
            from: 0
            to: 50
            editable: true
        }
        Item { Layout.fillWidth: true }
    }

    Item { Layout.fillHeight: true }
}
