// MarkdownPreview.qml
import QtQuick
import QtWebEngine

Item {
    id: root

    property string markdown: ""
    property bool dark: false
    property color bgColor: "white"
    property color fgColor: "black"

    property bool _pageReady: false

    function _push() {
        if (!_pageReady) return
        var js = "render(" + JSON.stringify(root.markdown) + ", "
               + JSON.stringify({ dark: root.dark, bg: root.bgColor, fg: root.fgColor }) + ");"
        web.runJavaScript(js)
    }

    Timer {
        id: debounce
        interval: 250
        onTriggered: root._push()
    }

    onMarkdownChanged: debounce.restart()
    onDarkChanged: _push()
    onBgColorChanged: _push()
    onFgColorChanged: _push()

    WebEngineView {
        id: web
        anchors.fill: parent
        backgroundColor: root.bgColor
        url: "qrc:/preview/preview.html"

        settings.localContentCanAccessFileUrls: true
        settings.localContentCanAccessRemoteUrls: false

        onContextMenuRequested: (request) => request.accepted = true
        onNavigationRequested: (request) => {
            if (request.navigationType !== WebEngineNavigationRequest.TypedNavigation
                && !request.url.toString().startsWith("qrc:/preview/"))
                request.action = WebEngineNavigationRequest.IgnoreRequest
        }

        onLoadingChanged: (info) => {
            if (info.status === WebEngineView.LoadSucceededStatus) {
                root._pageReady = true
                root._push()
            }
        }
    }
}