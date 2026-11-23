pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

// One layer of a block: the recognized text or the correction. The panel
// names its layer, carries the verdict of the checking model and the text
// itself. The chip at the right says whether the layer is the one the
// document shows.
Rectangle {
    id: root

    property string title: ""
    property string noteText: ""
    property string verdictText: ""
    property string confidenceText: ""
    property color verdictColor: Theme.textMuted
    property bool inDocument: false
    property string html: ""
    property bool mono: false
    property bool empty: false
    property string emptyText: ""

    // The room the words of the header take to the right of the name of the
    // layer: only the words that are there take it.
    readonly property real rightRoom: (verdictPrefix.visible ? verdictPrefix.implicitWidth : 0)
                                  + (verdictLabel.visible ? verdictLabel.implicitWidth : 0)
                                  + (confidenceLabel.visible ? confidenceLabel.implicitWidth : 0)
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
                    // verdict give way to it, not the other way round.
                    Layout.maximumWidth: Math.max(0, root.width - root.rightRoom - root.noteRoom)
                }

                LLOLabel {
                    // What the pipeline did with this layer: the note stands
                    // next to the layer it speaks about.
                    id: noteLabel
                    visible: !root.empty && root.noteText.length > 0
                    text: "· " + root.noteText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.maximumWidth: root.noteLimit
                }

                LLOLabel {
                    id: verdictPrefix
                    visible: !root.empty && root.verdictText.length > 0
                    text: qsTr("· Check:")
                    color: Theme.textMuted
                }

                LLOLabel {
                    id: verdictLabel
                    visible: !root.empty && root.verdictText.length > 0
                    text: root.verdictText
                    color: root.verdictColor
                    font.bold: true
                }

                LLOLabel {
                    id: confidenceLabel
                    visible: !root.empty && root.confidenceText.length > 0
                    text: root.confidenceText
                    color: root.verdictColor
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

        LLOLabel {
            visible: root.empty
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSmall
            Layout.rightMargin: Theme.spacingSmall
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
            text: root.emptyText
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        // The text as it is: the tinted fragments of the diff, or the plain
        // text of the layer. The field stays selectable — the operator copies
        // a formula from here.
        ScrollView {
            id: textScroll
            visible: !root.empty
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
