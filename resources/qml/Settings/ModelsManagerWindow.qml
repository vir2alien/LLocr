pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

ApplicationWindow {
    id: window

    title: qsTr("Models")
    width: 640
    height: 640
    modality: Qt.NonModal

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    background: SettingsSurface {
    }

    footer: SettingsFooter {
        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.helpColor
            text: qsTr("Every model and quantization of the app. Assigning them "
                       + "to tasks lives in Models by task.")
        }
        LLOButton {
            text: qsTr("Close")
            onClicked: window.close()
        }
    }

    onVisibleChanged: {
        if (visible) {
            ModelInstaller.refreshInstalled()
            ModelInstaller.reloadPresets()
            ModelInstaller.rescanRegistry()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: 8

        InstallerStatusLabel {
            Layout.fillWidth: true
            isError: ModelInstaller.state === ModelInstaller.Error
            busy: ModelInstaller.busy
            statusText: ModelInstaller.statusMessage.length
                  ? ModelInstaller.statusMessage
                  : qsTr("Models are stored locally and launched by the managed runtime.")
        }

        InstallerProgressRow {
            // Invisible while idle: an empty nested layout absorbs the spare
            // window height and pushes the list down.
            visible: ModelInstaller.busy
            Layout.fillWidth: true
            busy: ModelInstaller.busy
            progress: ModelInstaller.progress
            cancelVisible: ModelInstaller.state === ModelInstaller.Downloading
            onCancelClicked: ModelInstaller.cancelInstall()
        }

        ModelDownloadList {
            id: downloadList
            Layout.fillWidth: true
            // Take the spare window height (rows stay at the top; the list
            // scrolls internally when the catalog outgrows the window).
            Layout.fillHeight: true
            Layout.preferredHeight: downloadList.implicitHeight
            maxVisibleRows: -1
            task: "all"
            manage: true
            onActionError: (msg) => statusMsg.text = msg
            onDownloadRequested: (title, quantId, license, runtimeNote) =>
                pickDialog.showFor(title, quantId, license, runtimeNote)
        }

        LLOLabel {
            id: statusMsg
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.textSecondary
            font.pointSize: Theme.captionSize
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }
    }//ColumnLayout

    InstallModelDialog {
        id: pickDialog
    }
}
