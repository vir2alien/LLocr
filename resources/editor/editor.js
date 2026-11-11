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

// The zoom of the editor's text: the content scales, the menu keeps the app's
// size — so the band still matches the central panel at any zoom.
const ZOOM_MIN = 0.5;
const ZOOM_MAX = 3;
const ZOOM_STEP = 1.25;
let editorZoom = 1;

// The page speaks the app's language: it travels in the url (ADR 303).
// Vditor has its own locale files; the zoom buttons are ours and their words
// come from the app's translation catalog (setZoomLabels).
const DEFAULT_ZOOM_LABELS = { in: "Zoom in", out: "Zoom out", reset: "Reset the zoom" };

// The locales Vditor ships. A language it does not know falls back to
// English — the editor stays usable in every language of the app.
const VDITOR_LOCALES = {
    de: "de_DE",
    en: "en_US",
    es: "es_ES",
    fr: "fr_FR",
    ja: "ja_JP",
    ko: "ko_KR",
    pt: "pt_BR",
    ru: "ru_RU",
    sv: "sv_SE",
    vi: "vi_VN",
    zh: "zh_CN",
    "zh-hant": "zh_TW",
    "zh-tw": "zh_TW",
};

let zoomLabels = DEFAULT_ZOOM_LABELS;

// The app's own words for the zoom (its translation catalog is the single
// source): a new app language brings its words with it.
function setZoomLabels(labels) {
    zoomLabels = Object.assign({}, DEFAULT_ZOOM_LABELS, labels || {});
    applyZoomLabels();
}

function pageLanguage() {
    const match = /[?&]lang=([a-zA-Z-]+)/.exec(window.location.search);
    return match ? match[1].toLowerCase() : "en";
}

function vditorLocale() {
    return VDITOR_LOCALES[pageLanguage()] || "en_US";
}

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

// "Keep tables as HTML" pushes the tables through as html-blocks: Vditor
// stores the source in a hidden <pre> and shows the raw HTML in the preview,
// where $…$ stays literal — lute never touches an html-block's content. The
// preview is visual only (the serialization reads the hidden source), so the
// dollar spans can be rendered with KaTeX right in it. KaTeX itself is loaded
// statically (editor.html): Vditor fetches it lazily only when it meets a
// formula of its own, and a page of html-table formulas never triggers that.
function refreshHtmlTableMath() {
    if (typeof katex === "undefined")
        return;
    document.querySelectorAll(".vditor-wysiwyg__preview").forEach((preview) => {
        if (!preview.querySelector("table"))
            return;
        const walker = document.createTreeWalker(preview, NodeFilter.SHOW_TEXT, null);
        const targets = [];
        while (walker.nextNode()) {
            const node = walker.currentNode;
            if (node.parentElement.closest(".ll-math"))
                continue;
            if (/\$[^$]+\$/.test(node.nodeValue))
                targets.push(node);
        }
        for (const node of targets) {
            const value = node.nodeValue;
            const frag = document.createDocumentFragment();
            const re = /\$[^$]+\$/g;
            let last = 0;
            let match;
            while ((match = re.exec(value)) !== null) {
                if (match.index > last)
                    frag.appendChild(document.createTextNode(value.slice(last, match.index)));
                const raw = match[0].slice(1, -1);
                const span = document.createElement("span");
                span.className = "ll-math";
                try {
                    span.innerHTML = katex.renderToString(raw, { displayMode: false, output: "html" });
                } catch (e) {
                    span.textContent = match[0];
                }
                frag.appendChild(span);
                last = match.index + match[0].length;
            }
            if (last < value.length)
                frag.appendChild(document.createTextNode(value.slice(last)));
            node.parentNode.replaceChild(frag, node);
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
            refreshHtmlTableMath();
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
    refreshHtmlTableMath();
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
    // The anchor walks the preceding blocks first: a fragment the document
    // repeats (a running header, a copy of a table) must not pull the search
    // to an earlier twin and slice its tail into this block's markdown.
    let cursor = 0;
    for (let i = 0; i < ordinal && i < blockMd.length; ++i) {
        const found = lastEditor.indexOf(blockMd[i], cursor);
        if (found >= 0)
            cursor = found + blockMd[i].length;
    }
    const at = lastEditor.indexOf(oldMd, cursor);
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
        refreshHtmlTableMath();
        // setValue rebuilt the content — the zoom lived on the old element.
        renderZoom();
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

// The hover toolbar Vditor builds for the block under the caret carries
// move-up/move-down/remove. Left alone it mutates the DOM and the settle
// throws the change away; the model owns the structure, so the click is
// intercepted before the button's own handler and turned into a request.
// The caret sits in the block the toolbar was built for — the popover takes
// no focus and the selection survives the click.
function interceptToolbarActions() {
    document.addEventListener("click", (event) => {
        const button = event.target.closest('[data-type="up"],[data-type="down"],[data-type="remove"]');
        if (!button || !bridge)
            return;
        if (!button.closest(".vditor-panel"))
            return;
        const selection = window.getSelection();
        const anchor = selection && selection.anchorNode
            ? (selection.anchorNode.nodeType === 1 ? selection.anchorNode : selection.anchorNode.parentElement)
            : null;
        const tag = anchor ? anchor.closest("[data-ll-block]") : null;
        if (!tag)
            return;
        event.preventDefault();
        event.stopPropagation();
        bridge.notifyBlockStructureAction(parseInt(tag.getAttribute("data-ll-block"), 10),
                                          button.getAttribute("data-type"));
    }, true);
}

// The zoom of the editor's text: the content scales, the menu keeps the app's
// size, so the band still matches the central panel at any zoom.
function renderZoom() {
    const content = contentElement();
    if (content)
        content.style.zoom = String(editorZoom);
    const label = document.querySelector(".ll-zoom__label span");
    if (label)
        label.textContent = Math.round(editorZoom * 100) + "%";
}

function applyZoom(factor) {
    editorZoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, editorZoom * factor));
    renderZoom();
    return editorZoom;
}

function zoomButton(action, label, paths) {
    return '<button type="button" class="vditor-tooltipped vditor-tooltipped__s" data-ll-zoom="' + action +
           '" aria-label="' + label + '"><svg viewBox="0 0 24 24">' + paths + "</svg></button>";
}

// The words of the zoom buttons: the app's own (ADR 303).
function applyZoomLabels() {
    const group = document.querySelector(".ll-zoom");
    if (!group)
        return;
    const labels = { in: zoomLabels.in, out: zoomLabels.out, reset: zoomLabels.reset };
    Object.keys(labels).forEach((action) => {
        const button = group.querySelector('[data-ll-zoom="' + action + '"]');
        if (button)
            button.setAttribute("aria-label", labels[action]);
    });
    alignTooltips(group.closest(".vditor-toolbar"));
}

// A tooltip must stay inside the pane: a long word in a narrow menu would
// hang over the edge and be cut. The width of a tip is the width of its own
// label, so it is measured and pinned to the edge it would cross.
function alignTooltips(toolbar) {
    if (!toolbar)
        return;
    const bounds = toolbar.getBoundingClientRect();
    toolbar.querySelectorAll(".vditor-tooltipped").forEach((button) => {
        button.classList.remove("ll-edge-left", "ll-edge-right");
        if (!button.getAttribute("aria-label"))
            return;
        button.classList.add("ll-measure");
        const style = getComputedStyle(button, "::after");
        const width = parseFloat(style.width) + parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
        button.classList.remove("ll-measure");
        if (!width || isNaN(width))
            return;
        const rect = button.getBoundingClientRect();
        if (rect.left - bounds.left < width / 2)
            button.classList.add("ll-edge-left");
        else if (bounds.right - rect.right < width / 2)
            button.classList.add("ll-edge-right");
    });
}

// The zoom buttons at the right end of the menu: the text grows and shrinks,
// the percentage returns it to 100 %. The menu stands at the top of the page,
// so its tooltips open below it — Vditor points them up, where the page cuts
// them off.
function buildZoomControls() {
    const toolbar = document.querySelector(".vditor-toolbar");
    if (!toolbar || toolbar.querySelector(".ll-zoom"))
        return;
    toolbar.querySelectorAll(".vditor-tooltipped__n, .vditor-tooltipped__ne").forEach((button) => {
        button.classList.remove("vditor-tooltipped__n", "vditor-tooltipped__ne");
        button.classList.add("vditor-tooltipped__s");
    });
    const group = document.createElement("div");
    group.className = "vditor-toolbar__item ll-zoom";
    group.innerHTML =
        zoomButton("out", zoomLabels.out, '<path d="M5 12h14"></path>') +
        '<button type="button" class="vditor-tooltipped vditor-tooltipped__s ll-zoom__label" data-ll-zoom="reset" aria-label="' + zoomLabels.reset + '"><span>100%</span></button>' +
        zoomButton("in", zoomLabels.in, '<path d="M5 12h14"></path><path d="M12 5v14"></path>');
    toolbar.insertBefore(group, toolbar.firstChild);
    group.addEventListener("click", (event) => {
        const button = event.target.closest("[data-ll-zoom]");
        if (!button)
            return;
        const action = button.getAttribute("data-ll-zoom");
        if (action === "in")
            applyZoom(ZOOM_STEP);
        else if (action === "out")
            applyZoom(1 / ZOOM_STEP);
        else
            applyZoom(1 / editorZoom);
    });
}

function initVditor() {
    vditor = new Vditor("editor", {
        cdn: "qrc:/editor/vditor",
        lang: vditorLocale(),
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
            buildZoomControls();
            alignTooltips(document.querySelector(".vditor-toolbar"));
            // A narrower pane moves the buttons against the edges: the tips
            // are pinned again.
            window.addEventListener("resize", () => alignTooltips(document.querySelector(".vditor-toolbar")));
            interceptToolbarActions();
            if (bridge)
                bridge.notifyPageReady();
        },
        input() { schedulePull(); },
        blur() {
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
