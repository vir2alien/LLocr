// export.js — headless export rendering (HTML/PDF) driven from
// ExportRenderer.cpp via runJavaScript. Uses the same render pipeline as the
// preview: protectMath → marked → DOMPurify → restoreMath → KaTeX auto-render.

var EXPORT_DELIMITERS = [
  { left: "$$", right: "$$", display: true },
  { left: "\\[", right: "\\]", display: true },
  { left: "$",  right: "$",  display: false },
  { left: "\\(", right: "\\)", display: false }
];

function renderMarkdownInto(md, el) {
  var p = protectMath(md || "");
  var html = marked.parse(p.md);
  html = DOMPurify.sanitize(html, { USE_PROFILES: { html: true, svg: true, mathml: true } });
  html = restoreMath(html, p.store);
  html = DOMPurify.sanitize(html, { USE_PROFILES: { html: true, svg: true, mathml: true } });
  el.innerHTML = html;
  renderMathInElement(el, {
    delimiters: EXPORT_DELIMITERS,
    throwOnError: false
  });
}

window.beginExport = function (css, includeHeadings) {
  var root = document.getElementById("export-root");
  root.innerHTML = "";
  var st = document.getElementById("export-style");
  if (!st) {
    st = document.createElement("style");
    st.id = "export-style";
    document.head.appendChild(st);
  }
  // The stylesheet comes from the C++ side (Exporter::exportStyleSheet) so the
  // in-page rendering (PDF) and the standalone HTML file share one source.
  st.textContent = css || "";
  window.__includePageHeadings = includeHeadings !== false;
  window.__fontsSettled = false;
  return true;
};

window.appendExportPage = function (pageNumber, md) {
  var root = document.getElementById("export-root");
  var section = document.createElement("section");
  section.className = "export-page";
  if (window.__includePageHeadings !== false) {
    var h = document.createElement("h2");
    h.textContent = "Page " + pageNumber;
    section.appendChild(h);
  }
  var body = document.createElement("div");
  body.className = "export-page-body";
  section.appendChild(body);
  renderMarkdownInto(md, body);
  root.appendChild(section);
  return true;
};

window.finishExport = function () {
  var settle = function () { window.__fontsSettled = true; };
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(settle);
    // Safety net: never block the export on a stuck font load.
    setTimeout(settle, 3000);
  } else {
    settle();
  }
  return true;
};
