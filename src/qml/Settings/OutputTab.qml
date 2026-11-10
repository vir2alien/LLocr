pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: root

    spacing: Theme.spacingSmall

    signal edited()

    // The load fills the controls from the settings: the changes it makes are
    // not the user's, so they must not mark the settings window dirty.
    property bool loading: false

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
        root.loading = true
        syncParser()
        splitPagesCheck.checked = Settings.splitPages
        pageNumbersCheck.checked = Settings.keepHeadersAndPageNumbers
        tablesAsHtmlCheck.checked = Settings.tablesAsHtml
        orientationCheckBox.checked = Settings.orientationCheckEnabled
        orientationAutoTurnCheck.checked = Settings.orientationAutoTurnEnabled
        root.orientationIndex = Settings.pdfLandscape ? 1 : 0
        marginSpin.value = Settings.pdfMarginMm
        root.loading = false
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
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }
    LLOComboBox {
        id: parserBox
        Layout.fillWidth: true
        model: root.parserModel
        textRole: "label"
        onActivated: if (!root.loading) root.edited()
    }

    Item { implicitHeight: 6 }

    OptionCheck {
        id: splitPagesCheck
        label: qsTr("Split pages")
        onToggled: if (!root.loading) root.edited()
    }

    OptionCheck {
        id: pageNumbersCheck
        label: qsTr("Keep headers and page numbers")
        onToggled: if (!root.loading) root.edited()
    }

    OptionCheck {
        id: tablesAsHtmlCheck
        label: qsTr("Keep tables as HTML")
        onToggled: if (!root.loading) root.edited()
    }

    OptionCheck {
        id: orientationCheckBox
        label: qsTr("Check the page orientation")
        onToggled: if (!root.loading) root.edited()
    }

    OptionCheck {
        id: orientationAutoTurnCheck
        visible: orientationCheckBox.checked
        label: qsTr("Turn a sideways page after markup automatically")
        onToggled: if (!root.loading) root.edited()
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("PDF export")
        font.pixelSize: Theme.bodySize
        color: Theme.textPrimary
    }

    GridLayout {
        columns: 3
        columnSpacing: Theme.spacing
        rowSpacing: root.spacing

        LLOLabel {
            text: qsTr("Orientation")
            font: Theme.body
        }
        SegmentedControl {
            options: [
                { label: qsTr("Portrait") },
                { label: qsTr("Landscape") }
            ]
            currentIndex: root.orientationIndex
            onActivated: (idx) => {
                root.orientationIndex = idx
                if (!root.loading) root.edited()
            }
        }
        Item { Layout.fillWidth: true }

        LLOLabel {
            text: qsTr("Margins (mm)")
            font: Theme.body
        }
        SpinBox {
            id: marginSpin
            Layout.preferredWidth: 110
            implicitHeight: Theme.controlHeight
            font.pixelSize: Theme.bodySize
            from: 0
            to: 50
            editable: true
            onValueChanged: if (!root.loading) root.edited()
        }
        Item { Layout.fillWidth: true }
    }

    Item { Layout.fillHeight: true }
}
