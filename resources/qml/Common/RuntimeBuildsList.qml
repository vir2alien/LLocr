pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 36
    property int maxVisibleRows: 3
    property bool scrollable: true

    visible: count > 0
    clip: true
    implicitHeight: count <= 0
                    ? 0
                    : (maxVisibleRows > 0 ? Math.min(count, maxVisibleRows) : count) * rowHeight
    model: RuntimeInstaller.installedBuilds
    interactive: root.scrollable
    ScrollBar.vertical: ScrollBar { policy: root.scrollable ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    delegate: Rectangle {
        id: buildRow
        required property int index
        required property string tag
        required property string build
        required property string backendDisplay
        required property string serverPath
        required property bool binaryFound
        required property bool active

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
                Layout.preferredWidth: 110
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: buildRow.build.length ? buildRow.build : buildRow.tag
            }
            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: buildRow.binaryFound
                      ? (buildRow.backendDisplay.length
                         ? buildRow.backendDisplay
                         : buildRow.tag)
                      : qsTr("%1 — binary missing").arg(buildRow.tag)
            }
            LLOButton {
                text: buildRow.active ? qsTr("Active") : qsTr("Activate")
                enabled: !buildRow.active && !RuntimeInstaller.busy
                         && Runtime.state !== Runtime.Starting && buildRow.binaryFound
                onClicked: {
                    if (Runtime.state === Runtime.Ready)
                        Runtime.stopServer()
                    RuntimeInstaller.activateBuild(buildRow.index)
                }
            }
            LLOButton {
                visible: buildRow.binaryFound
                text: qsTr("Open folder")
                onClicked: RuntimeInstaller.openBuildFolder(buildRow.index)
            }
        }
    }
}
