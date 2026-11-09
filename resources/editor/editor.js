"use strict";

// The bridge is the only channel back to QML: page lifecycle, per-block
// edits, block clicks and editor focus all travel through it. Everything
// else (markdown in) arrives through runJavaScript calls on the view.
let bridge = null;
let vditor = null;
let reference = "";      // the editor's serialization of the last applied push
let rawLast = null;      // the markdown of the last model push, as received
let lastEditor = "";     // the editor's serialization the pulls slice against
let domBlocks = 0;       // the DOM block count of the synced state
let blockMd = [];        // the pushed markdown of every tagged block
let pushToken = 0;       // identifies the push the page's content derives from
let dirty = false;       // the DOM holds text the model has not seen yet
let stale = false;       // a model push was skipped while dirty; fetched at the next settle
let settleTimer = null;  // the beat of quiet typing that closes the dirty window
let pullTimer = null;
let highlighted = [];
let suppressPull = false;

function contentElement() {
    return document.querySelector(".vditor-wysiwyg [contenteditable]");
}

// Undo/redo restores DOM snapshots whose math previews come back empty (or
// carrying the raw source instead of the render), while Vditor's own pass
// skips elements that already carry data-math — so the formulas vanish until
// the next full push. Re-render the stale previews ourselves.
function refreshMath() {
    if (typeof katex === "undefined")
        return;
    document.querySelectorAll(".language-math[data-math]").forEach((el) => {
        const raw = el.getAttribute("data-math");
        const rendered = el.childElementCount > 0;
        if (rendered && el.textContent.trim() !== raw)
            return;
        try {
            el.innerHTML = katex.renderToString(raw, { displayMode: el.tagName === "DIV", output: "html" });
        } catch (e) {
            el.textContent = raw;
        }
    });
}

function watchMath() {
    const content = contentElement();
    if (!content || typeof MutationObserver === "undefined")
        return;
    let timer = null;
    const observer = new MutationObserver(() => {
        if (timer !== null)
            clearTimeout(timer);
        timer = setTimeout(() => {
            timer = null;
            refreshMath();
        }, 100);
    });
    observer.observe(content, { childList: true, subtree: true });
}

function retagBlocks() {
    const content = contentElement();
    if (!content)
        return;
    const children = Array.from(content.children);
    for (let i = 0; i < children.length; ++i)
        children[i].setAttribute("data-ll-block", String(i));
}

// The editor edits one block at a time: a structural change (a block added
// or removed by Enter/Backspace across boundaries) is out of scope — the
// page is pushed back over it. The baseline is the DOM block count of the
// synced state, not the model fragment count: a fragment may span several
// DOM blocks (an inner blank line).
function blockStructureChanged() {
    const content = contentElement();
    if (!content)
        return false;
    return content.children.length !== domBlocks;
}

function schedulePull() {
    dirty = true;
    // The timer arms even during a push-settle: a keystroke landing there
    // must not stay uncommitted just because the window was brief.
    if (pullTimer !== null)
        clearTimeout(pullTimer);
    pullTimer = setTimeout(pull, 300);
    if (suppressPull)
        return;
    retagBlocks();
    refreshMath();
}

function pull() {
    pullTimer = null;
    if (!bridge || !vditor)
        return;
    if (suppressPull)
        return;
    if (blockStructureChanged()) {
        pushPage(rawLast, null);
        return;
    }
    retagBlocks();

    const selection = window.getSelection();
    const anchor = selection && selection.anchorNode
        ? (selection.anchorNode.nodeType === 1 ? selection.anchorNode : selection.anchorNode.parentElement)
        : null;
    const tag = anchor ? anchor.closest("[data-ll-block]") : null;
    if (!tag)
        return;
    const ordinal = parseInt(tag.getAttribute("data-ll-block"), 10);
    if (!(ordinal >= 0) || ordinal >= blockMd.length)
        return;

    const value = vditor.getValue();
    if (value === reference || value === lastEditor)
        return;
    // The typed markdown of the block: the whole document's serialization
    // shrinks and grows only inside the edited block, so its slice is the
    // old fragment shifted by the document's size delta. Both sides are the
    // editor's serialization — a raw push and getValue() disagree in
    // normalization details, and a delta across them steals a tail.
    const oldMd = blockMd[ordinal];
    const at = lastEditor.indexOf(oldMd);
    if (at < 0)
        return;
    const delta = value.length - lastEditor.length;
    const newMd = value.substr(at, oldMd.length + delta);
    if (newMd === oldMd)
        return;
    blockMd[ordinal] = newMd;
    reference = value;
    lastEditor = value;
    bridge.notifyBlockChanged(ordinal, newMd, pushToken);
    // The model's join answers as a push. While the typing continues it
    // parks — a setValue now would clobber the caret — and the settle that
    // follows the last keystroke fetches the model's current state.
}

function applyTheme(t) {
    if (!vditor || !t)
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
    rawLast = markdown;
    if (markdown === lastEditor) {
        // The model's raw join already equals the editor's serialization:
        // the DOM — and with it the caret and the undo stack — stays
        // untouched.
        dirty = false;
        stale = false;
        return;
    }
    pushToken += 1;
    dirty = false;
    stale = false;
    suppressPull = true;
    vditor.setValue(markdown, true);
    setTimeout(() => {
        suppressPull = false;
        reference = vditor.getValue();
        lastEditor = reference;
        const content = contentElement();
        domBlocks = content ? content.children.length : 0;
        // setValue rebuilds the content DOM asynchronously — the tags that
        // retagBlocks() laid before it are gone by now.
        retagBlocks();
        if (bridge)
            bridge.notifyReferenceChanged(reference, pushToken);
    }, 0);
}

// The model pushes whenever its text changes (a block landing, a revert).
// While the user is typing the DOM owns newer text: applying would setValue
// over the caret and the un-pulled keystrokes — the push parks and the state
// it skipped is fetched from QML — which always holds the current join — at
// the next settle.
function pushDocument(markdown, theme, nextBlockMd) {
    if (dirty) {
        stale = true;
        armSettle();
        return;
    }
    blockMd = nextBlockMd || [];
    pushPage(markdown, theme);
}

function armSettle() {
    if (settleTimer !== null)
        clearTimeout(settleTimer);
    settleTimer = setTimeout(settleIfQuiet, 400);
}

function settleIfQuiet() {
    settleTimer = null;
    if (!dirty)
        return;
    if (vditor.getValue() !== lastEditor) {
        armSettle();
        return;
    }
    dirty = false;
    requestMissingSync();
}

function requestMissingSync() {
    if (!stale)
        return;
    stale = false;
    if (bridge)
        bridge.notifyResyncRequested();
}

// A model-side release of a layer («Взять текст из блока», the badge): the
// changed document comes in through the next push — nothing to do here.
function highlightBlocks(indices) {
    for (const marked of highlighted)
        marked.classList.remove("ll-hl");
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
        toolbar: ["headings", "bold", "italic", "strike", "|",
                  "list", "ordered-list", "check", "|",
                  "quote", "inline-code", "code", "table"],
        after() {
            const content = contentElement();
            if (content) {
                content.addEventListener("click", (event) => {
                    const block = event.target.closest("[data-ll-block]");
                    if (block && bridge)
                        bridge.notifyBlockClicked(parseInt(block.getAttribute("data-ll-block"), 10));
                });
                watchMath();
            }
            if (bridge)
                bridge.notifyPageReady();
        },
        input() { schedulePull(); },
        focus() { if (bridge) bridge.notifyEditorFocus(true); },
        blur() {
            if (bridge)
                bridge.notifyEditorFocus(false);
            // Leaving the field settles everything: the un-pulled keystrokes
            // commit (the caret may already be gone — the accepted ≤300 ms
            // loss), and a state the page skipped applies with no caret to
            // clobber.
            if (pullTimer !== null)
                pull();
            dirty = false;
            requestMissingSync();
        },
    });
}

new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.editorBridge;
    initVditor();
});
