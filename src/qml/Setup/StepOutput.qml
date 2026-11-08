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

    implicitHeight: contentColumn.implicitHeight + 40

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 20
        spacing: Theme.spacing

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Output")
            font.pixelSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("How recognized text is structured and exported. "
                       + "Everything can be changed later in Settings → Output.")
        }

        OptionCheck {
            label: qsTr("Split pages")
            help: qsTr("When off, exported pages are joined without the "
                       + "“Page 1”, “Page 2” … headings.")
            checked: Settings.splitPages
            onToggled: (value) => Settings.splitPages = value
        }

        OptionCheck {
            label: qsTr("Keep headers and page numbers")
            help: qsTr("When off, the running heads, the running feet and the "
                       + "page numbers from the model are ignored during "
                       + "recognition. Applies to newly recognized pages.")
            checked: Settings.keepHeadersAndPageNumbers
            onToggled: (value) => Settings.keepHeadersAndPageNumbers = value
        }

        OptionCheck {
            label: qsTr("Keep tables as HTML")
            help: qsTr("When on, recognized tables are kept as the model's <table> "
                       + "HTML instead of being converted to a Markdown pipe table.")
            checked: Settings.tablesAsHtml
            onToggled: (value) => Settings.tablesAsHtml = value
        }

        Item { implicitHeight: 6 }

        LLOLabel {
            text: qsTr("PDF export")
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            LLOLabel {
                text: qsTr("Orientation")
                font: Theme.bodySmall
            }
            SegmentedControl {
                options: [
                    { label: qsTr("Portrait") },
                    { label: qsTr("Landscape") }
                ]
                currentIndex: Settings.pdfLandscape ? 1 : 0
                onActivated: (idx) => Settings.pdfLandscape = (idx === 1)
            }
            Item { Layout.fillWidth: true }
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
                font.pixelSize: Theme.bodySmallSize
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
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
