pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// Output options apply immediately on change (like the Output settings).
Item {
    id: root

    property bool complete: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Output")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("How recognized text is structured and exported. "
                       + "Everything can be changed later in Settings → Output.")
        }

        LLOCheckBox {
            id: splitPagesCheck
            font.pointSize: Theme.captionSize
            text: qsTr("Split pages")
            checked: Settings.splitPages
            onToggled: Settings.splitPages = checked
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("When off, exported pages are joined without the "
                       + "“Page 1”, “Page 2” … headings.")
        }

        LLOCheckBox {
            id: pageNumbersCheck
            font.pointSize: Theme.captionSize
            text: qsTr("Keep page numbers")
            checked: Settings.keepPageNumbers
            onToggled: Settings.keepPageNumbers = checked
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("When off, page_number blocks from the model are ignored "
                       + "during recognition. Applies to newly recognized pages.")
        }

        LLOCheckBox {
            id: tablesAsHtmlCheck
            font.pointSize: Theme.captionSize
            text: qsTr("Keep tables as HTML")
            checked: Settings.tablesAsHtml
            onToggled: Settings.tablesAsHtml = checked
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("When on, recognized tables are kept as the model's <table> "
                       + "HTML instead of being converted to a Markdown pipe table.")
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
                function sync() { currentIndex = Settings.pdfLandscape ? 1 : 0 }
                onModelChanged: sync()
                Component.onCompleted: sync()
                onActivated: (idx) => Settings.pdfLandscape = (idx === 1)
                Connections {
                    target: Settings
                    function onPdfLandscapeChanged() { orientationBox.sync() }
                }
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
                value: Settings.pdfMarginMm
                onValueModified: Settings.pdfMarginMm = value
                Connections {
                    target: Settings
                    function onPdfMarginMmChanged() { marginSpin.value = Settings.pdfMarginMm }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
