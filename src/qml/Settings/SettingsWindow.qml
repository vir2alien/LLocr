pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// One window for every setting (ADR 297): the sections on the left, the
// content of the picked section on the right, one Save and one Cancel for
// all of them.
ApplicationWindow {
    id: window

    title: qsTr("Settings")
    width: 1040
    height: 700
    minimumWidth: 880
    minimumHeight: 560
    modality: Qt.NonModal

    // The user changed something in one of the sections.
    property bool dirty: false
    property bool forceClose: false

    // The wizard replaces the editing session: the window closes and the
    // wizard runs alone in the main window.
    signal wizardRequested()

    readonly property var sections: [
        { id: "interface", title: qsTr("Interface"), icon: "monitor" },
        { id: "output", title: qsTr("Output"), icon: "file-text" },
        { id: "verification", title: qsTr("Verification"), icon: "circle-check" },
        { id: "runtime", title: qsTr("Runtime"), icon: "server" },
        { id: "models", title: qsTr("Models"), icon: "box" }
    ]

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    function showSection(sectionId) {
        for (let i = 0; i < window.sections.length; ++i) {
            if (window.sections[i].id === sectionId) {
                nav.currentIndex = i
                return
            }
        }
    }

    function loadAll() {
        interfaceSection.loadValues()
        outputSection.loadValues()
        verificationSection.loadValues()
        runtimeSection.loadValues()
        modelsSection.loadValues()
        window.dirty = false
    }

    // The models section answers false when a model is not on disk: the window
    // stays open and the download dialog is up.
    function saveAll() {
        if (modelsSection.saveValues() === false)
            return false
        outputSection.saveValues()
        verificationSection.saveValues()
        runtimeSection.saveValues()
        Settings.forceSave()
        window.dirty = false
        return true
    }

    function cancelAll() {
        interfaceSection.cancelValues()
        outputSection.cancelValues()
        verificationSection.cancelValues()
        runtimeSection.cancelValues()
        modelsSection.cancelValues()
        window.dirty = false
    }

    // The undo of the live sections is armed once per session: a hide and
    // show inside the session must not re-arm it — otherwise Cancel would
    // have nothing to put back.
    property bool sessionStarted: false

    // The live pages take their undo snapshot here: the language and the
    // theme, the block types of the verification, the connection mode and
    // the backend.
    function armSession() {
        interfaceSection.beginSession()
        verificationSection.beginSession()
        runtimeSection.beginSession()
    }

    background: SettingsSurface {
    }

    onVisibleChanged: {
        if (!visible)
            return
        // The one-shot pass spent on a confirmed discard must not leak into
        // the next session with the window.
        window.forceClose = false
        if (window.sessionStarted)
            return
        window.armSession()
        window.loadAll()
        window.sessionStarted = true
    }

    // The wizard replaces the editing session: the flag carries the request
    // through the close, and the wizard starts once the window is gone.
    property bool pendingWizard: false

    function requestWizard() {
        window.pendingWizard = true
        window.close()
    }

    onClosing: (close) => {
        if (window.dirty && !window.forceClose) {
            close.accepted = false
            closeConfirmDialog.open()
            return
        }
        window.sessionStarted = false
        if (window.pendingWizard) {
            window.pendingWizard = false
            // The window is still visible while the closing signal runs: the
            // wizard must not start under it.
            Qt.callLater(function() { window.wizardRequested() })
        }
    }

    footer: SettingsFooter {
        LLOButton {
            subtle: true
            text: qsTr("Setup wizard")
            onClicked: window.requestWizard()
        }
        Item { Layout.fillWidth: true }
        LLOButton {
            text: qsTr("Cancel")
            onClicked: {
                window.cancelAll()
                window.close()
            }
        }
        LLOButton {
            emphasis: true
            text: qsTr("Save")
            onClicked: {
                if (window.saveAll())
                    window.close()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: Theme.spacingLarge

        SettingsSectionNav {
            id: nav

            Layout.preferredWidth: 200
            Layout.fillHeight: true
            sections: window.sections
            onSectionPicked: (sectionId) => window.showSection(sectionId)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceAlt
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius + 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    text: nav.currentSectionTitle
                    font.pixelSize: Theme.subheadingSize
                    font.bold: true
                    color: Theme.textPrimary
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    InterfaceSection {
                        id: interfaceSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 0
                        onEdited: window.dirty = true
                    }
                    OutputSection {
                        id: outputSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 1
                        onEdited: window.dirty = true
                    }
                    VerificationSection {
                        id: verificationSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 2
                        onEdited: window.dirty = true
                    }
                    RuntimeSection {
                        id: runtimeSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 3
                        onEdited: window.dirty = true
                    }
                    ModelsSection {
                        id: modelsSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 4
                        onEdited: window.dirty = true
                        onRuntimeSectionRequested: window.showSection("runtime")
                    }
                }
            }
        }
    }

    Dialog {
        id: closeConfirmDialog

        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Discard changes?")
        standardButtons: Dialog.Yes | Dialog.No

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: window.pendingWizard
                  ? qsTr("There are unsaved changes. They will be lost, and the setup wizard will start.")
                  : qsTr("There are unsaved changes. Close without saving?")
        }

        onAccepted: {
            window.cancelAll()
            window.dirty = false
            window.forceClose = true
            window.close()
        }

        onRejected: {
            window.pendingWizard = false
        }
    }
}
