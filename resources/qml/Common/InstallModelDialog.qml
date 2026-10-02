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

    modal: true
    anchors.centerIn: parent
    width: 420
    title: qsTr("Install model")
    standardButtons: Dialog.Ok | Dialog.Cancel

    function showFor(title, quant, licenseText, note) {
        modelTitle = title
        quantId = quant
        license = licenseText
        runtimeNote = note
        open()
    }

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

    onAccepted: ModelInstaller.installPrepared()
}