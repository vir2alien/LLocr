pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 40
    property bool managementActions: false

    signal actionError(string message)

    function fmtBytes(bytes) {
        if (bytes <= 0) return ""
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GiB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1024).toFixed(1) + " KiB"
    }

    visible: count > 0
    clip: true
    model: ModelInstaller.installedCount
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: installedRow
        required property int index
        property var info: ModelInstaller.installedInfo(index)
        function refreshInfo() {
            info = ModelInstaller.installedInfo(index)
        }
        Connections {
            target: ModelInstaller
            function onInstalledChanged() { installedRow.refreshInfo() }
        }

        width: root.width
        height: root.rowHeight
        color: info.active ? Theme.surfaceSunken : "transparent"
        border.color: info.active ? Theme.accent : "transparent"
        border.width: info.active ? 1 : 0
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
                text: installedRow.info.title
            }
            LLOLabel {
                visible: root.managementActions
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: root.fmtBytes(installedRow.info.size)
            }
            LLOLabel {
                Layout.preferredWidth: 80
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: installedRow.info.origin === "managed" ? Theme.textSecondary : Theme.textMuted
                text: installedRow.info.origin === "managed" ? qsTr("managed") : qsTr("external")
            }
            LLOButton {
                text: installedRow.info.active ? qsTr("Active") : qsTr("Activate")
                enabled: !installedRow.info.active && !ModelInstaller.busy
                onClicked: ModelInstaller.setActiveModel(installedRow.index)
            }
            LLOButton {
                visible: root.managementActions
                text: qsTr("Remove")
                enabled: installedRow.info.origin === "managed"
                onClicked: {
                    const err = ModelInstaller.removeModel(installedRow.index)
                    if (err.length)
                        root.actionError(err)
                }
            }
            LLOButton {
                visible: root.managementActions
                text: qsTr("Open folder")
                onClicked: {
                    const err = ModelInstaller.openModelFolder(installedRow.index)
                    if (err.length)
                        root.actionError(err)
                }
            }
        }
    }
}
