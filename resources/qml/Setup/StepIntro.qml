pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Welcome to LLM OCR")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("This assistant recognizes text from images and PDFs using a "
                       + "local LLM. A few steps will configure the app — you can "
                       + "change everything later in Settings.")
        }

        Item { implicitHeight: 6 }

        InterfaceSettings {
            Layout.fillHeight: true
        }
    }
}