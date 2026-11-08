pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: true

    implicitHeight: contentColumn.implicitHeight + 40

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Welcome to LLM OCR")
            font.pixelSize: Theme.headingSize3
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            font.pixelSize: Theme.subheadingSize
            text: qsTr("This assistant recognizes text from images and PDFs using a "
                       + "local LLM. A few steps will configure the app — you can "
                       + "change everything later in Settings.")
        }

        Item { implicitHeight: 6 }

        InterfaceSettings {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}