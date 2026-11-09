"use strict";

// The bridge is the only channel back to QML: page lifecycle, per-block
// edits, block clicks and editor focus all travel through it. Everything
// else (markdown in) arrives through runJavaScript calls on the view.
let bridge = null;
let vditor = null;
let reference = "";      // normalized markdown of the last push
let lastPushed = null;   // markdown of the last push, as received
let blockMd = [];        // the pushed markdown of every tagged block
let blockFlags = [];     // per block: the recognition carries another text
let pushToken = 0;       // identifies the push the page's content derives from
let dirty = false;       // a pull is in flight or just landed: the DOM owns newer text
let pendingSync = null;  // a model push that arrived while dirty; applied on the next settle
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

// The blocks the user layered get a badge: the recognition carries a
// different text, and the badge click releases the layer.
function refreshBadges() {
    const content = contentElement();
    if (!content)
        return;
    const children = Array.from(content.children);
    for (let i = 0; i < children.length; ++i) {
        let badge = children[i].querySelector(":scope > .ll-ai-badge");
        if (blockFlags[i]) {
            if (!badge) {
                badge = document.createElement("button");
                badge.className = "ll-ai-badge";
                badge.textContent = "AI";
                badge.title = "The recognition carries a different text — click to take it";
                badge.addEventListener("click", (event) => {
                    event.preventDefault();
                    event.stopPropagation();
                    if (bridge)
                        bridge.notifyBlockReleased(i, pushToken);
                });
                children[i].appendChild(badge);
            }
        } else if (badge) {
            badge.remove();
        }
    }
}

// The editor edits one block at a time: a structural change (a block added
// or removed by Enter/Backspace across boundaries) is out of scope — the
// page is pushed back over it.
function blockStructureChanged() {
    const content = contentElement();
    if (!content)
        return false;
    return content.children.length !== blockMd.length;
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
    refreshBadges();
    refreshMath();
}

function pull() {
    pullTimer = null;
    if (!bridge || !vditor)
        return;
    if (suppressPull)
        return;
    if (blockStructureChanged()) {
        pushPage(lastPushed, null);
        return;
    }
    retagBlocks();
    refreshBadges();

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
    if (value === reference || value === lastPushed)
        return;
    // The typed markdown of the block: the whole document's serialization
    // shrinks and grows only inside the edited block, so its slice is the
    // old fragment shifted by the document's size delta.
    const oldMd = blockMd[ordinal];
    const at = lastPushed.indexOf(oldMd);
    if (at < 0)
        return;
    const delta = value.length - lastPushed.length;
    const newMd = value.substr(at, oldMd.length + delta);
    if (newMd === oldMd)
        return;
    blockMd[ordinal] = newMd;
    lastPushed = value;
    reference = value;
    // The DOM owns newer text than the model until this lands; a model push
    // arriving meanwhile must wait, or it would setValue over the caret.
    dirty = true;
    bridge.notifyBlockChanged(ordinal, newMd, pushToken);
    // The pull reached the model synchronously; its join comes back as a
    // push, so the dirty window closes on that push — nothing to flush here.
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
    refreshBadges();
    if (markdown === lastPushed)
        return;
    lastPushed = markdown;
    pushToken += 1;
    dirty = false;
    suppressPull = true;
    vditor.setValue(markdown, true);
    setTimeout(() => {
        suppressPull = false;
        reference = vditor.getValue();
        // setValue rebuilds the content DOM asynchronously — the tags that
        // retagBlocks() laid before it are gone by now.
        retagBlocks();
        refreshBadges();
        if (bridge)
            bridge.notifyReferenceChanged(reference, pushToken);
    }, 0);
}

// The model pushes whenever its text changes (a block landing, a release,
// a revert). While the user is typing the DOM owns newer text: the push
// waits as pendingSync and applies at the next settle (a pull commit, a
// blur, the next push attempt) — never over in-flight keystrokes, and
// never waiting for a tab switch.
function pushDocument(markdown, theme, nextBlockMd, nextBlockFlags) {
    if (dirty) {
        pendingSync = { md: markdown, theme: theme, blockMd: nextBlockMd || [], blockFlags: nextBlockFlags || [] };
        return;
    }
    blockMd = nextBlockMd || [];
    blockFlags = nextBlockFlags || [];
    pushPage(markdown, theme);
}

function flushPendingSync() {
    if (!pendingSync)
        return;
    const ps = pendingSync;
    pendingSync = null;
    pushDocument(ps.md, ps.theme, ps.blockMd, ps.blockFlags);
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
                    if (block && bridge && !event.target.classList.contains("ll-ai-badge"))
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
            flushPendingSync();
        },
    });
}

// The push carries what the badges need: the markdown of every block and
// which of them hold a user layer.
function pushDocument(markdown, theme, nextBlockMd, nextBlockFlags) {
    blockMd = nextBlockMd || [];
    blockFlags = nextBlockFlags || [];
    pushPage(markdown, theme);
}

new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.editorBridge;
    initVditor();
});
