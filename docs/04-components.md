# 04. Components

Status legend: ✅ implemented · 🟡 partial · ⬜ not started

## 4.1 LLM provider (OpenAI-compatible only)
- **OpenAiProvider** ✅: sends a request to `/v1/chat/completions` with an image
  (base64 in `image_url`) and parses the response. Transport-only: the connection
  config (`ProviderConfig`) is separate from the model/prompt params
  (`OcrRequest`). Async (`QPromise`/`QFuture`), per-request timeout, and
  **`abort()`** for Stop.
- Configuration ✅: base URL, API key (optional), timeout — in `ProviderConfig`;
  model name, temperature, max tokens, DRY params, parser — in `SettingsStore`
  (`QSettings`), edited in the tabbed **Settings** dialog. (The prompt is
  currently hardcoded in `AppController` — see below.)

## 4.2 Settings
All configuration lives in the **Settings dialog**, grouped into tabs:

| Tab        | Fields                                                              |
| ---------- | ------------------------------------------------------------------- |
| UI         | language (System / English / Русский), theme (System / Light / Dark) |
| Connection | base URL, API key (optional), request timeout                       |
| Model      | model name, temperature, max tokens, DRY multiplier, DRY base,      |
|            | DRY allowed length, DRY penalty last-N                              |
| Output     | output parser (`raw` / `det_tokens`; default `det_tokens`)           |

Persistence is handled by `SettingsStore` (`QSettings`, grouped keys
`provider/*`, `model/*`, `output/*`). UI state (theme, language, window
geometry) is persisted under `ui/*`.

> **Not in the dialog yet:** the model prompt/instruction is hardcoded in
> `AppController::m_prompt` (a `prompt` PROPERTY exists but has no UI); the
> bbox coordinate range is hardcoded in `DetTokensParser`
> (`kBboxCoordinateRange = 1000`). Both remain future work.

## 4.3 Themes
Three options handled by `UiController` (a QML singleton), selected in
**Settings → UI**:
- `System`(follows the system theme)
- `Light`
- `Dark`

## 4.4 Output parsers
- `det_tokens` ✅ — **default**. `DetTokensParser` extracts text + coordinates
  from the model's `<|det|> label [x1,y1,x2,y2] <|/det|> text` stream (content
  is JSON-escaped and unescaped by the parser; model control tokens such as
  `<|end_of_sentence|>` — incl. full-width-pipe variants — are stripped).
  Legacy bare `label [x1,y1,x2,y2] text` lines are still accepted. Input is
  one page per request, so no page splitting happens here. Coordinates are in
  the 0–1000 range and normalized to [0,1] (`kBboxCoordinateRange`).
- `raw` ✅ — text as-is (`RawParser`).
- Created via `ParserFactory` (`registeredIds()` → `raw`, `det_tokens`); the
  active parser is chosen in **Settings → Output**
  (`AppController::parserNames` lists the options). Easy to add new ones —
  extend `ParserFactory` and the `parserNames` list.
- Block styles: `DetTokensParser` maps model labels to `BlockStyle`
  (`BlockStyle.h`) — `title` → heading, `image`/`chart` → image placeholder,
  `image_caption`/`table_caption`/`table_footnote`/`page_number` → italic,
  `equation` → equation, `table` → GFM pipe table, `ref_text` → plain text.
  The free function `rebuildPageText()` regenerates the page Markdown from the
  boxes (used after a box is removed, to keep `image://ocr/crop/<N>` indices
  consistent).

## 4.5 Image and PDF loading
- Images: `QImage` (single or multiple via `loadImages` / `appendImage`). ✅
- PDF: rendered page-by-page via **Qt PDF (`QPdfDocument`)** at ~150 DPI. ✅
- Multi-page documents → page-by-page processing, with a **"Recognize all"**
  batch run and a page-thumbnail strip. ✅
- Pages can be **deleted** (`removePage`) and **drag-reordered** (`movePage`);
  PDF pages and images can be appended to an open document. ✅

## 4.6 Box rendering & image-block editing
- Overlay bboxes on top of the preview via a QML `Repeater` bound to
  `BoxListModel` (normalized rectangles). ✅ (populated when `det_tokens` is used).
- Image/chart blocks (`label` = `image` / `chart`) are **editable**: they can
  be moved, resized (8 resize handles), and deleted directly on the preview.
  Deletion/geometry changes are pushed through `BoxListModel::removeBox` /
  `updateBoxRect` and reflected back into the page Markdown via
  `rebuildPageText`. ✅

## 4.7 Page navigation & thumbnails
- Left strip of page thumbnails (`PageListModel` + `OcrImageProvider`
  `image://ocr/page/N`). ✅
- Each thumbnail shows a **recognized / not-recognized** marker, an **edited**
  marker, and a **duplicate** marker (red) when a page had duplicate boxes. ✅
- Pages can be **deleted** (hover → ✕) and **drag-reordered** (drag grip). ✅
- Clicking a thumbnail or using the ‹ › buttons navigates — **allowed while a
  recognition run is in progress**. ✅

## 4.8 Stop / cancellation
- The Stop button calls `AppController::stop()`, which forwards to
  `RecognitionController::stop()` (it sets a stop flag and calls
  `OpenAiProvider::abort()`); the sequential "recognize all" loop halts
  cleanly and already-recognized pages are preserved. ✅

## 4.9 Text editing
- The right pane is an **editable** `TextArea` (read-only until the current
  page is recognized). ✅
- Edits are stored **per page** in the dedicated `PageEditStore` class (owned
  by `AppController`, keyed by page index) and override the recognized text
  for both display and export. ✅
- An edited page is flagged (`PageListModel` `edited` role) and can be
  **reverted** to the original recognition (`revertCurrentPageEdits`). ✅
- A fresh recognition of a page **supersedes** any manual edit on it. ✅
- `PageEditStore` also remaps edit indices after a page is removed or reordered
  (`remapAfterRemove` / `remapAfterMove`). ✅
- QML syncs safely: programmatic reloads are guarded (`textArea.syncing`) so
  they never look like user input, and an unchanged reload does not reset the
  caret (important during "recognize all" while editing).

## 4.10 Export
Implemented by the **`Exporter`** component. **Markdown is the single internal
source of truth** (ADR #5/#10). Export uses the **effective** text per page —
i.e. the user's edit when present, else the raw recognition.

| Format   | Implementation                                   | Status |
| -------- | ------------------------------------------------ | ------ |
| TXT      | directly (page separators)                       | ✅     |
| Markdown | directly (`## Page N`) — the internal source     | ✅     |
| HTML     | directly (escaped, self-contained `<section>`)   | ✅     |
| DOCX     | via **Pandoc** (Markdown on stdin → .docx)       | ✅     |
| PDF      | **Pandoc** if available, else built-in `QPdfWriter` + `QTextDocument` fallback | ✅ |

- Pandoc is discovered once via `QStandardPaths::findExecutable("pandoc")`
  (`Exporter::isPandocAvailable()` / `pandocExecutable()`).
- The Save dialog advertises **DOCX only when Pandoc is present**
  (`AppController::exportNameFilters`). PDF is always offered because of the
  built-in fallback writer.
- Image blocks are emitted as `![alt](image://ocr/crop/<boxIndex>)`. Since those
  references have no meaning outside the app, every export path resolves them
  to real image files first (`Exporter::resolveImageReferences`, using the
  cropped pixels from `AppController::croppedImage`).
- Export scope is selectable for multi-page documents: **All recognized
  pages**, **Current page**, or a **page range** (only recognized pages in the
  selection are exported).

## 4.11 Window settings
Window position / size / visibility are persisted via `WindowSettings.qml`
(writing to `SettingsStore` `ui/windowX|Y|Width|Height|State`). ✅

## 4.12 Tests
Unit tests exist under `tests/` (Qt Test) and are all registered in
`tests/CMakeLists.txt`: `test_det_parser`, `test_pagemodel`,
`test_settings_store`, and `test_exporter`. Built when `LLOCR_BUILD_TESTS=ON`
(default).

## 4.13 RAG service (later stage) — ⬜ not started
- A separate Python process (FastAPI + Chroma/Qdrant).
- LLocr sends recognized text for indexing over HTTP.
- At an early stage — only an **interface stub** in the backend.

## 4.14 Internationalization (i18n)
- UI strings use `qsTr`/`tr`; a Russian translation lives in
  `resources/i18n/llocr_ru.ts` and is compiled/embedded via
  `qt_add_translations` (Qt LinguistTools). ✅
- Language is selected in **Settings → UI** (System / English / Русский),
  persisted via `SettingsStore` (`ui/language`), and applied at runtime by the
  `I18n` class — it installs app/Qt translators and emits `languageApplied`,
  which `main.cpp` connects to `QQmlApplicationEngine::retranslate()`. ✅

## 4.15 Markdown preview
- The right text pane has a **Preview** switch that renders the current page's
  Markdown in a `QtWebEngine` view (`MarkdownPreview.qml`). ✅
- Rendering is done client-side by bundled `marked` + `KaTeX` assets
  (`resources/preview/`), so LaTeX formulas, tables, and images render
  locally with no network access. ✅
- `image://ocr/crop/<N>` references are converted to `data:` URIs before
  rendering (`AppController::resolveImagesForPreview`). ✅
