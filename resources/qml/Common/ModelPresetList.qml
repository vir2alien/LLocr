pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

ListView {
    id: root

    property int rowHeight: 36
    // Height of the embedded (non-scrolling) list. Consumers bind
    // Layout.preferredHeight to implicitHeight; maxVisibleRows <= 0 = uncapped.
    property int maxVisibleRows: 3

    property bool isVerifyModelRole: false

    signal installClicked(int index)

    visible: count > 0
    clip: true
    implicitHeight: count <= 0
                    ? 0
                    : (maxVisibleRows > 0 ? Math.min(count, maxVisibleRows) : count) * rowHeight
    model: isVerifyModelRole ? ModelInstaller.checkPresetCount : ModelInstaller.presetCount
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: Rectangle {
        id: presetRoot
        required property int index
        property var pInfo: ModelInstaller.presetInfo(index, root.isVerifyModelRole)
        function refreshPresetInfo() {
            pInfo = ModelInstaller.presetInfo(index, root.isVerifyModelRole)
        }
        Connections {
            target: ModelInstaller
            function onInstalledChanged() { presetRoot.refreshPresetInfo() }
            function onPresetsChanged() { presetRoot.refreshPresetInfo() }
        }

        width: root.width
        height: root.rowHeight
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6
            LLOLabel {
                Layout.preferredWidth: 150
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textPrimary
                text: presetRoot.pInfo.title
            }
            LLOLabel {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                wrapMode: Text.NoWrap
                font.pointSize: Theme.captionSize
                color: Theme.textMuted
                text: presetRoot.pInfo.repo
            }
            LLOButton {
                text: presetRoot.pInfo.active ? qsTr("Active")
                      : presetRoot.pInfo.installed ? qsTr("Activate") : qsTr("Install")
                enabled: !presetRoot.pInfo.active && !ModelInstaller.busy
                onClicked: {
                    if (presetRoot.pInfo.installed) {
                        ModelInstaller.activatePreset(presetRoot.index, root.isVerifyModelRole)
                        return
                    }
                    root.installClicked(presetRoot.index)
                }
            }
        }//RowLayout
    }//delegate
}
