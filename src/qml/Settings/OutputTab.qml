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
        pageNumbersCheck.checked = Settings.keepPageNumbers
        tablesAsHtmlCheck.checked = Settings.tablesAsHtml
        orientationBox.currentIndex = Settings.pdfLandscape ? 1 : 0
        marginSpin.value = Settings.pdfMarginMm
    }

    function saveValues() {
        Settings.parserId = root.parserModel[parserBox.currentIndex].id
        Settings.splitPages = splitPagesCheck.checked
        Settings.keepPageNumbers = pageNumbersCheck.checked
        Settings.tablesAsHtml = tablesAsHtmlCheck.checked
        Settings.pdfLandscape = orientationBox.currentIndex === 1
        Settings.pdfMarginMm = marginSpin.value
    }

    Component.onCompleted: syncParser()

    Connections {
        target: Settings
        function onParserIdChanged() { root.syncParser() }
        function onSplitPagesChanged() { splitPagesCheck.checked = Settings.splitPages }
        function onKeepPageNumbersChanged() { pageNumbersCheck.checked = Settings.keepPageNumbers }
        function onTablesAsHtmlChanged() { tablesAsHtmlCheck.checked = Settings.tablesAsHtml }
        function onPdfLandscapeChanged() { orientationBox.currentIndex = Settings.pdfLandscape ? 1 : 0 }
        function onPdfMarginMmChanged() { marginSpin.value = Settings.pdfMarginMm }
    }

    LLOLabel {
        text: qsTr("Output parser")
    }
    ComboBox {
        id: parserBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: root.parserModel
        textRole: "label"
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.helpColor
        text: qsTr("“Automatic” uses the parser the selected OCR model expects. "
                   + "“Raw text” keeps the model reply as-is; “Layout tokens” "
                   + "extracts positioned fragments (bounding boxes) for the overlay.")
    }

    Item { implicitHeight: 6 }

    OptionCheck {
        id: splitPagesCheck
        label: qsTr("Split pages")
        help: qsTr("When off, exported pages are joined without the "
                   + "“Page 1”, “Page 2” … headings.")
    }

    OptionCheck {
        id: pageNumbersCheck
        label: qsTr("Keep page numbers")
        help: qsTr("When off, page_number blocks from the model are ignored "
                   + "during recognition. Applies to newly recognized pages.")
    }

    OptionCheck {
        id: tablesAsHtmlCheck
        label: qsTr("Keep tables as HTML")
        help: qsTr("When on, recognized tables are kept as the model's <table> "
                   + "HTML instead of being converted to a Markdown pipe table. "
                   + "Most Markdown editors render this.")
    }

    Item { implicitHeight: 6 }

    LLOLabel {
        text: qsTr("PDF export")
        font.bold: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        LLOLabel {
            text: qsTr("Orientation")
        }
        ComboBox {
            id: orientationBox
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            model: [qsTr("Portrait"), qsTr("Landscape")]
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        LLOLabel {
            text: qsTr("Margins (mm)")
        }
        SpinBox {
            id: marginSpin
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            from: 0
            to: 50
            editable: true
        }
    }

    Item { Layout.fillHeight: true }
}
