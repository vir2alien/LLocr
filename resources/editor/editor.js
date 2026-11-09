"use strict";

// The bridge is the only channel back to QML: page lifecycle, the edited
// text, block clicks and editor focus all travel through it. Everything else
// (markdown in) arrives through runJavaScript calls on the view.
let bridge = null;
let vditor = null;
let reference = "";      // normalized markdown of the last push
let lastPushed = null;   // markdown of the last push, as received
let pushToken = 0;       // identifies the push the page's content derives from
let pullTimer = null;
let highlighted = [];
let suppressPull = false;

function contentElement() {
    return document.querySelector(".vditor-wysiwyg [contenteditable]");
}

function retagBlocks() {
    const content = contentElement();
    if (!content)
        return;
    const children = Array.from(content.children);
    for (let i = 0; i < children.length; ++i)
        children[i].setAttribute("data-ll-block", String(i));
}

function schedulePull() {
    // The timer arms even during a push-settle: a keystroke landing there
    // must not stay uncommitted just because the window was brief.
    if (pullTimer !== null)
        clearTimeout(pullTimer);
    pullTimer = setTimeout(pull, 300);
    if (suppressPull)
        return;
    retagBlocks();
}

function pull() {
    pullTimer = null;
    if (!bridge || !vditor)
        return;
    const value = vditor.getValue();
    if (value !== reference && value !== lastPushed) {
        // The commit becomes the page's own baseline: the follow-up push of
        // the same text (the model's encode of this very pull) must no-op in
        // pushPage instead of setValue-ing over it — a setValue here would
        // clear the undo stack mid-typing.
        reference = value;
        lastPushed = value;
        bridge.notifyTextChanged(value, pushToken);
    }
}

function applyTheme(t) {
    if (!vditor)
        return;
    vditor.setTheme(t.dark ? "dark" : "classic",
                    t.dark ? "dark" : "light",
                    t.dark ? "github-dark" : "github");
    document.body.style.background = t.bg;
    document.body.style.color = t.fg;
    document.documentElement.style.setProperty("--hl", t.hl);
}

function pushPage(markdown, theme) {
    applyTheme(theme);
    retagBlocks();
    if (markdown === lastPushed)
        return;
    lastPushed = markdown;
    pushToken += 1;
    suppressPull = true;
    vditor.setValue(markdown, true);
    setTimeout(() => {
        suppressPull = false;
        reference = vditor.getValue();
        // setValue rebuilds the content DOM asynchronously — the tags that
        // retagBlocks() laid before it are gone by now.
        retagBlocks();
        if (bridge)
            bridge.notifyReferenceChanged(reference, pushToken);
    }, 0);
}

function highlightBlocks(indices) {
    for (const el of highlighted)
        el.classList.remove("ll-hl");
    highlighted = [];
    const content = contentElement();
    if (!content)
        return;
    let first = null;
    for (const i of indices) {
        const el = content.querySelector('[data-ll-block="' + i + '"]');
        if (!el)
            continue;
        el.classList.add("ll-hl");
        highlighted.push(el);
        if (!first)
            first = el;
    }
    if (first)
        first.scrollIntoView({ block: "center" });
}

function initVditor() {
    vditor = new Vditor("editor", {
        cdn: "qrc:/editor/vditor",
        lang: "en_US",
        mode: "wysiwyg",
        height: "100%",
        placeholder: "",
        preview: { math: { inlineMath: true } },
        toolbar: ["undo", "redo", "headings", "bold", "italic", "strike", "|",
                  "list", "ordered-list", "check", "|",
                  "quote", "inline-code", "code", "table"],
        after() {
            const content = contentElement();
            if (content)
                content.addEventListener("click", (event) => {
                    const block = event.target.closest("[data-ll-block]");
                    if (block && bridge)
                        bridge.notifyBlockClicked(parseInt(block.getAttribute("data-ll-block"), 10));
                });
            if (bridge)
                bridge.notifyPageReady();
        },
        input() { schedulePull(); },
        focus() { if (bridge) bridge.notifyEditorFocus(true); },
        blur() { if (bridge) bridge.notifyEditorFocus(false); },
    });
}

new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.editorBridge;
    initVditor();
});
