pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 40
    // Height of the embedded (non-scrolling) list. Consumers bind
    // Layout.preferredHeight to implicitHeight; maxVisibleRows <= 0 = uncapped.
    property int maxVisibleRows: 3
    property bool managementActions: false
    property bool isVerifyModelRole: false

    signal actionError(string message)

    function fmtBytes(bytes) {
        if (bytes <= 0) return ""
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    visible: count > 0
    clip: true
    implicitHeight: count <= 0
                    ? 0
                    : (maxVisibleRows > 0 ? Math.min(count, maxVisibleRows) : count) * rowHeight
    model: root.isVerifyModelRole ? ModelInstaller.checkInstalledModels
                                  : ModelInstaller.installedModels
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: installedRow
        required property int index
        required property string title
        required property string path
        required property string mmprojPath
        required property var size
        required property string quantization
        required property string origin
        required property string license
        required property string repo
        required property bool active
        required property int parts

        width: root.width
        height: root.rowHeight
        color: active ? Theme.surfaceSunken : "transparent"
        border.color: active ? Theme.accent : "transparent"
        border.width: active ? 1 : 0
        radius: Theme.radius

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6

            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: installedRow.title
            }
            LLOLabel {
                visible: root.managementActions
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: root.fmtBytes(installedRow.size)
            }
            LLOLabel {
                Layout.preferredWidth: 80
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: installedRow.origin === "managed" ? Theme.textSecondary : Theme.textMuted
                text: installedRow.origin === "managed" ? qsTr("managed") : qsTr("external")
            }
            LLOButton {
                text: installedRow.active ? qsTr("Active") : qsTr("Activate")
                enabled: !installedRow.active && !ModelInstaller.busy
                onClicked: {
                    const i = model.sourceIndex(installedRow.index)
                    if (i !== undefined && i >= 0)
                        ModelInstaller.setActiveModel(i, root.isVerifyModelRole)
                }
            }
            LLOButton {
                visible: root.managementActions
                text: qsTr("Remove")
                enabled: installedRow.origin === "managed"
                onClicked: {
                    const i = model.sourceIndex(installedRow.index)
                    if (i === undefined || i < 0)
                        return
                    const err = ModelInstaller.removeModel(i)
                    if (err.length)
                        root.actionError(err)
                }
            }
            LLOButton {
                visible: root.managementActions
                text: qsTr("Open folder")
                onClicked: {
                    const i = model.sourceIndex(installedRow.index)
                    if (i === undefined || i < 0)
                        return
                    const err = ModelInstaller.openModelFolder(i)
                    if (err.length)
                        root.actionError(err)
                }
            }
        }//RowLayout
    }//delegate
}
