import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 36
    property bool scrollable: true

    visible: count > 0
    clip: true
    model: RuntimeInstaller.installedBuildCount
    interactive: root.scrollable
    ScrollBar.vertical: ScrollBar { policy: root.scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    delegate: Rectangle {
        id: buildRow
        required property int index
        property var info: RuntimeInstaller.installedBuildInfo(index)
        function refreshInfo() {
            info = RuntimeInstaller.installedBuildInfo(index)
        }
        Connections {
            target: RuntimeInstaller
            function onInstalledBuildsChanged() { buildRow.refreshInfo() }
            function onInstalledChanged() { buildRow.refreshInfo() }
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
                Layout.preferredWidth: 110
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: buildRow.info.build.length ? buildRow.info.build : buildRow.info.tag
            }
            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: buildRow.info.binaryFound
                      ? (buildRow.info.backendDisplay.length
                         ? buildRow.info.backendDisplay
                         : buildRow.info.tag)
                      : qsTr("%1 — binary missing").arg(buildRow.info.tag)
            }
            LLOButton {
                text: buildRow.info.active ? qsTr("Active") : qsTr("Activate")
                enabled: !buildRow.info.active && !RuntimeInstaller.busy
                         && Runtime.state !== Runtime.Starting && buildRow.info.binaryFound
                onClicked: {
                    if (Runtime.state === Runtime.Ready)
                        Runtime.stopServer()
                    RuntimeInstaller.activateBuild(buildRow.index)
                }
            }
        }
    }
}
