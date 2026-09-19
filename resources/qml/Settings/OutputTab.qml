pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ColumnLayout {
    id: root

    spacing: 4

    property var parserModel: Controller.parserNames

    function syncParser() {
        var idx = root.parserModel.indexOf(Settings.parserId)
        parserBox.currentIndex = idx >= 0 ? idx : 0
    }

    function loadValues() {
        syncParser()
        splitPagesCheck.checked = Settings.splitPages
        pageNumbersCheck.checked = Settings.keepPageNumbers
        orientationBox.currentIndex = Settings.pdfLandscape ? 1 : 0
        marginSpin.value = Settings.pdfMarginMm
    }

    function saveValues() {
        Settings.parserId = root.parserModel[parserBox.currentIndex]
        Settings.splitPages = splitPagesCheck.checked
        Settings.keepPageNumbers = pageNumbersCheck.checked
        Settings.pdfLandscape = orientationBox.currentIndex === 1
        Settings.pdfMarginMm = marginSpin.value
    }

    Component.onCompleted: syncParser()

    Connections {
        target: Settings
        function onParserIdChanged() { root.syncParser() }
        function onSplitPagesChanged() { splitPagesCheck.checked = Settings.splitPages }
        function onKeepPageNumbersChanged() { pageNumbersCheck.checked = Settings.keepPageNumbers }
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
    }

    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("‘raw’ keeps the model text as-is. ‘det_tokens’ extracts "
                   + "positioned fragments (bounding boxes) for the overlay.")
    }

    Item { implicitHeight: 6 }

    CheckBox {
        id: splitPagesCheck
        font.pointSize: Theme.captionSize
        text: qsTr("Split pages")
    }
    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("When off, exported pages are joined without the "
                   + "“Page 1”, “Page 2” … headings.")
    }

    CheckBox {
        id: pageNumbersCheck
        font.pointSize: Theme.captionSize
        text: qsTr("Keep page numbers")
    }
    LLOLabel {
        Layout.fillWidth: true
        font.pointSize: Theme.captionSize
        color: Theme.textMuted
        text: qsTr("When off, page_number blocks from the model are ignored "
                   + "during recognition. Applies to newly recognized pages.")
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
