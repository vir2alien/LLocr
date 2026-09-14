marked.setOptions({ gfm: true, breaks: false });

// --- math protection: stash formulas before marked, restore after ---
function protectMath(md) {
  var store = [];
  var stash = function(s) { return "\uE000MATH" + (store.push(s) - 1) + "\uE001"; };
  md = md.replace(/\$\$([\s\S]+?)\$\$/g, function(_, m) { return stash("$$" + m + "$$"); });
  md = md.replace(/\$([^$\n]+?)\$/g,   function(_, m) { return stash("$" + m + "$"); });
  return { md: md, store: store };
}

function restoreMath(html, store) {
  return html.replace(/\uE000MATH(\d+)\uE001/g, function(_, i) { return store[+i]; });
}

window.render = function (md, opts) {
  opts = opts || {};
  var r = document.documentElement.style;
  if (opts.dark) {
    document.body.style.background = opts.bg || "#1e1e1e";
    document.body.style.color = opts.fg || "#e0e0e0";
    r.setProperty("--border", "#444");
    r.setProperty("--th-bg", "rgba(255,255,255,0.08)");
    r.setProperty("--code-bg", "rgba(255,255,255,0.08)");
    r.setProperty("--muted", "#999");
  } else {
    document.body.style.background = opts.bg || "#ffffff";
    document.body.style.color = opts.fg || "#1a1a1a";
    r.setProperty("--border", "#ccc");
    r.setProperty("--th-bg", "rgba(0,0,0,0.05)");
    r.setProperty("--code-bg", "rgba(0,0,0,0.05)");
    r.setProperty("--muted", "#888");
  }

  var el = document.getElementById("content");

  // protect → render MD → sanitize → restore → sanitize → render KaTeX
  var p = protectMath(md || "");
  var html = marked.parse(p.md);
  html = DOMPurify.sanitize(html, { USE_PROFILES: { html: true, svg: true, mathml: true } });
  html = restoreMath(html, p.store);
  html = DOMPurify.sanitize(html, { USE_PROFILES: { html: true, svg: true, mathml: true } });
  el.innerHTML = html;

  renderMathInElement(el, {
    delimiters: [
      { left: "$$", right: "$$", display: true },
      { left: "\\[", right: "\\]", display: true },
      { left: "$",  right: "$",  display: false },
      { left: "\\(", right: "\\)", display: false }
    ],
    throwOnError: false
  });
};
