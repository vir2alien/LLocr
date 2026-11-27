pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

// One layer of a block: the recognized text or the correction. The card
// names its layer, carries the note of the pipeline, the score the layer
// earned and the approve action of the layer. The chip at the right says
// whether the document carries this layer (ADR 339).
Rectangle {
    id: root

    property string title: ""
    property string noteText: ""
    property string scoreText: ""
    property color scoreColor: Theme.nothing
    property bool inDocument: false
    property string html: ""
    property bool mono: false
    // The approve action belongs to the layer it acts on (ADR 339).
    property bool approveVisible: false
    property bool approveEnabled: true
    property string approveText: qsTr("Approve")
    property string approveTip: qsTr("Ignore the check result")

    signal approveRequested()

    // The room the words of the header take to the right of the name of the
    // layer: only the words that are there take it.
    readonly property real rightRoom: (scorePrefix.visible ? scorePrefix.implicitWidth : 0)
                                  + (scoreLabel.visible ? scoreLabel.implicitWidth : 0)
                                  + (approveButton.visible ? approveButton.implicitWidth : 0)
                                  + (documentChipBox.visible ? documentChipBox.implicitWidth : 0)
                                  + Theme.spacingSmall * 4
    readonly property real noteRoom: noteLabel.visible
                                      ? Math.min(noteLabel.implicitWidth, root.noteLimit)
                                      : 0
    readonly property int noteLimit: 200

    implicitHeight: layout.implicitHeight
    color: Theme.cardSurface
    border.color: Theme.cardBorder
    border.width: 1
    radius: Theme.radius
    clip: true

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: headerRow.implicitHeight + 2 * Theme.spacingSmall
            color: Theme.panelHeaderBg

            RowLayout {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                LLOLabel {
                    id: titleLabel
                    text: root.title
                    font.bold: true
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    // The name of the layer keeps its room: the note and the
                    // score give way to it, not the other way round.
                    Layout.maximumWidth: Math.max(0, root.width - root.rightRoom - root.noteRoom)
                }

                LLOLabel {
                    // What the pipeline did with this layer: the note stands
                    // next to the layer it speaks about.
                    id: noteLabel
                    visible: root.noteText.length > 0
                    text: "· " + root.noteText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.maximumWidth: root.noteLimit
                }

                LLOLabel {
                    id: scorePrefix
                    visible: root.scoreText.length > 0
                    text: qsTr("· Score:")
                    color: Theme.textMuted
                }

                LLOLabel {
                    id: scoreLabel
                    visible: root.scoreText.length > 0
                    text: root.scoreText
                    color: root.scoreColor
                    font.bold: true
                }

                LLOButton {
                    id: approveButton
                    visible: root.approveVisible
                    enabled: root.approveEnabled
                    text: root.approveText
                    topPadding: 0
                    bottomPadding: 0
                    onClicked: root.approveRequested()
                    ToolTip.visible: hovered
                    ToolTip.text: root.approveTip
                }

                Item {
                    Layout.fillWidth: true
                }

                Rectangle {
                    id: documentChipBox
                    visible: root.inDocument
                    Layout.preferredWidth: documentChip.implicitWidth + 12
                    Layout.preferredHeight: 18
                    radius: 9
                    color: Theme.selected
                    border.color: Theme.border
                    border.width: 1

                    LLOLabel {
                        id: documentChip
                        anchors.centerIn: parent
                        text: qsTr("In the document")
                        color: Theme.textSecondary
                        font: Theme.caption
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // The text as it is: the tinted fragments of the diff, or the plain
        // text of the layer. The field stays selectable — the operator copies
        // a formula from here.
        ScrollView {
            id: textScroll
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSmall
            Layout.rightMargin: Theme.spacingSmall
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
            Layout.preferredHeight: Math.min(Math.max(textView.contentHeight, 18) + 2 * Theme.spacingSmall, 120)
            contentWidth: availableWidth

            TextArea {
                id: textView
                width: parent.width
                readOnly: true
                textFormat: TextArea.RichText
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: Theme.textPrimary
                font: root.mono ? Theme.mono : Theme.bodySmall
                background: null
                topPadding: 0
                bottomPadding: 0
                text: root.html
            }
        }
    }
}
