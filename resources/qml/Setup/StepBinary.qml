pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import LLocr
import "../Common"

Item {
    id: root

    property bool complete: Settings.serverPath.trim().length > 0

    onVisibleChanged: {
        if (visible)
            pathField.text = Settings.serverPath
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("llama-server binary")
            font.pointSize: Theme.bodySize
            color: Theme.textPrimary
            font.bold: true
        }

        LLOLabel {
            Layout.fillWidth: true
            text: qsTr("Point to the llama-server binary you already have. "
                       + "The binary is checked right after selection.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: pathField
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                selectByMouse: true
                placeholderText: qsTr("path to llama-server")
                text: Settings.serverPath
                onEditingFinished: Settings.serverPath = text.trim()
            }
            LLOButton {
                text: qsTr("Browse…")
                onClicked: binaryPicker.open()
            }
        }

        LLOButton {
            text: qsTr("Check")
            enabled: Settings.serverPath.trim().length > 0
            onClicked: Runtime.probeRuntimePath(Settings.serverPath.trim())
        }

        LLOLabel {
            Layout.fillWidth: true
            text: Runtime.statusMessage.length
                  ? Runtime.statusMessage
                  : (Settings.serverPath.length
                     ? qsTr("Not probed yet") : qsTr("No server binary selected"))
            elide: Text.ElideMiddle
            wrapMode: Text.NoWrap
            font.pointSize: Theme.captionSize
            color: Settings.serverPath.length && !Runtime.lockedOut
                   ? Theme.textSecondary : Theme.textMuted
        }

        Item { Layout.fillHeight: true }
    }

    FileDialog {
        id: binaryPicker
        title: qsTr("Select llama-server binary")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Executables (*)")]
        onAccepted: {
            const path = Runtime.localPath(selectedFile)
            Settings.serverPath = path
            Settings.serverPathIsManaged = false
            pathField.text = path
            Runtime.probeRuntimePath(path)
        }
    }
}
