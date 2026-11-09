pragma ComponentBehavior: Bound

import QtQuick
import QtWebChannel
import QtWebEngine

import LLocr
import "../Common"

Item {
    id: root

    property string markdown: ""
    // The box owning every top-level markdown block, in page order; the
    // editor's DOM blocks are tagged with the same ordinals.
    property var blockMap: []
    property bool dark: false
    property color bgColor: "white"
    property color fgColor: "black"
    property string hl: "#308cc6"
    property bool editorFocused: false

    // The reference is the normalization of the last push: a pull matching
    // it is the editor's own echo of the pushed text, not a user edit.
    signal editorTextChanged(string markdown)
    signal blockClicked(int blockIndex)

    property bool _pageLoaded: false
    property bool _channelReady: false
    property string _reference: ""
    property int _referenceToken: -1
    property bool _pendingPush: false
    property string _loadError: ""

    function _pushPage() {
        if (!_pageLoaded || !_channelReady)
            return
        // A push while the user types rebuilds the DOM under the caret and
        // clears the editor's undo stack; the refresh waits for blur — the
        // same deferral the raw field's pendingReload implements.
        if (root.editorFocused) {
            root._pendingPush = true
            return
        }
        root._pendingPush = false
        var js = "pushPage(" + JSON.stringify(root.markdown) + ", "
               + JSON.stringify({ dark: root.dark, bg: String(root.bgColor), fg: String(root.fgColor), hl: root.hl }) + ");"
        web.runJavaScript(js)
    }

    function highlightBox(boxIndex) {
        if (!_pageLoaded || !_channelReady)
            return
        var indices = []
        for (var i = 0; i < root.blockMap.length; ++i)
            if (root.blockMap[i] === boxIndex)
                indices.push(i)
        web.runJavaScript("highlightBlocks(" + JSON.stringify(indices) + ");")
    }

    Timer {
        id: pushDebounce
        interval: 250
        onTriggered: root._pushPage()
    }

    onMarkdownChanged: pushDebounce.restart()
    onDarkChanged: _pushPage()
    onBgColorChanged: _pushPage()
    onFgColorChanged: _pushPage()
    onHlChanged: _pushPage()

    // A failed editor.html load used to leave a permanently blank pane; the
    // banner is cleared by the next successful load.
    LLOLabel {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingWindow
        visible: root._loadError !== ""
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.bodySmallSize
        color: Theme.textSecondary
        text: root._loadError
    }

    // Instantiated here, not referenced as a type: a bare EditorBridge inside
    // registeredObjects resolves to null. The page looks the object up as
    // channel.objects.editorBridge, which the attached WebChannel.id names.
    // The type is registered from main.cpp at runtime — the module's qmldir
    // does not know it, so the two lint checks below are muted per line.
    EditorBridge { // qmllint disable import
        id: bridge
        WebChannel.id: "editorBridge"
    }

    WebChannel {
        id: channel
        registeredObjects: [bridge] // qmllint disable incompatible-type
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: root.bgColor
        url: "qrc:/editor/editor.html"
        webChannel: channel

        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false

        onContextMenuRequested: (request) => request.accepted = true
        onNavigationRequested: (request) => {
            if (!request.url.toString().startsWith("qrc:/editor/"))
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                root._pageLoaded = true
                root._loadError = ""
            } else if (info.status === WebEngineView.LoadFailedStatus) {
                root._pageLoaded = false
                root._channelReady = false
                root._loadError = qsTr("Failed to load the editor page (%1).").arg(info.errorString)
            }
        }
    }

    Connections {
        target: bridge // qmllint disable incompatible-type

        function onPageReady() {
            root._channelReady = true
            root._pushPage()
        }
        function onEditorReferenceChanged(markdown, pushToken) {
            root._reference = markdown
            root._referenceToken = pushToken
        }
        function onEditorTextChanged(markdown, pushToken) {
            // A pull whose base push is no longer the current one (a newer
            // push replaced the document) edits text that is already gone.
            if (pushToken !== root._referenceToken)
                return
            if (markdown !== root._reference)
                root.editorTextChanged(markdown)
        }
        function onEditorBlockClicked(blockIndex) {
            root.blockClicked(blockIndex)
        }
        function onEditorFocusChanged(focused) {
            root.editorFocused = focused
            if (!focused && root._pendingPush)
                root._pushPage()
        }
    }
}
