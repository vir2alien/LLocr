pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

// The confirmation shown before a model download starts, wherever it was
// started from — the Models tab or the wizard. It carries the same four
// fields in both, so it lives here once.
Dialog {
    id: root

    property string modelTitle: ""
    property string quantId: ""
    property string license: ""
    property string runtimeNote: ""

    // The plan is prepared asynchronously after the click; confirming while
    // it is still fetching would start the previous (or no) plan.
    readonly property bool planReady: ModelInstaller.state === ModelInstaller.ReadyToDownload

    modal: true
    anchors.centerIn: parent
    width: 420
    title: qsTr("Install model")

    function showFor(title, quant, licenseText, note) {
        modelTitle = title
        quantId = quant
        license = licenseText
        runtimeNote = note
        open()
    }

    onOpened: installButton.forceActiveFocus()

    ColumnLayout {
        width: parent.width
        spacing: 6
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.textPrimary
            text: [root.modelTitle, root.quantId].join(" ").trim()
        }
        LLOLabel {
            Layout.fillWidth: true
            visible: ModelInstaller.state === ModelInstaller.Fetching
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.textMuted
            text: qsTr("Preparing the download…")
        }
        LLOLabel {
            Layout.fillWidth: true
            visible: ModelInstaller.state === ModelInstaller.Error
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.error
            text: ModelInstaller.statusMessage
        }
        LLOLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pointSize: Theme.captionSize
            color: Theme.textSecondary
            text: qsTr("Downloading starts after confirmation. The model license "
                       + "applies — review it before installing.")
        }
        RuntimeNoteWarning {
            note: root.runtimeNote
        }
        LLOLabel {
            Layout.fillWidth: true
            font.pointSize: Theme.captionSize
            color: Theme.accent
            visible: root.license.length > 0
            textFormat: Text.RichText
            text: {
                var lic = root.license
                if (/^https?:\/\//.test(lic))
                    return qsTr("License: %1")
                        .arg("<a href=\"" + lic + "\">License</a>")
                return qsTr("License: %1").arg(lic)
            }
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + Theme.spacingLarge

        RowLayout {
            id: buttons
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacingLarge
            anchors.bottomMargin: Theme.spacing
            spacing: Theme.spacing

            Item { Layout.fillWidth: true }
            LLOButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            LLOButton {
                id: installButton
                text: qsTr("Install")
                emphasis: true
                enabled: root.planReady
                Keys.onEscapePressed: root.close()
                onClicked: root.accept()
            }
        }
    }

    onAccepted: {
        if (root.planReady)
            ModelInstaller.installPrepared()
    }
}