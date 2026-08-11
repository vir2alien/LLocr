# 04. Components

Status legend: ✅ implemented · 🟡 partial · ⬜ not started

## 4.1 LLM provider (OpenAI-compatible only)
- **OpenAiProvider** ✅: sends a request to `/v1/chat/completions` with an image
  (base64 in `image_url`) and parses the response. Async (`QPromise`/`QFuture`),
  per-request timeout, and **`abort()`** for Stop.
- Configuration ✅: base URL, API key (optional), timeout, model name, prompt,
  temperature, max tokens — all in `ProviderConfig`, persisted via
  `SettingsStore`, edited in the tabbed **Settings** dialog.

## 4.2 Settings
All configuration lives in the **Settings dialog**, grouped into tabs:

| Tab        | Fields                                            |
| ---------- | ------------------------------------------------- |
| Connection | base URL, API key (optional), request timeout     |
| Model      | model name, prompt/instruction, temperature, max tokens |
| Output     | output parser, bbox coordinate range              |

Persistence is handled by `SettingsStore` (`QSettings`, grouped keys
`provider/*`, `model/*`, `output/*`).

## 4.3 Themes
Three options handled by `UiController`:
- `System`(follows the system theme)
- `Light`
- `Dark`

## 4.4 Output parsers
- `raw` ✅ — text as-is (`RawParser`). Default.
- `det_tokens` ✅ — `DetTokensParser` extracts text + coordinates
  (`<|det|> label [x1,y1,x2,y2] <|/det|> text`, `<PAGE>` splits) for overlay.
- Created via `ParserFactory`; the active parser is chosen in **Settings →
  Output** (`AppController::parserNames` lists the options). Easy to add new
  ones — extend `ParserFactory` and the `parserNames` list.

## 4.5 Image and PDF loading
- Images: `QImage`. ✅
- PDF: rendered page-by-page via **Qt PDF (`QPdfDocument`)** at ~150 DPI. ✅
- Multi-page documents → page-by-page processing, with a **"Recognize all"**
  batch run and a page-thumbnail strip. ✅

## 4.6 Box rendering
- Overlay bboxes on top of the preview via a QML `Repeater` bound to
  `BoxListModel` (normalized rectangles). ✅ (populated when `det_tokens` is used).

## 4.7 Page navigation & thumbnails
- Left strip of page thumbnails (`PageListModel` + `OcrImageProvider`
  `image://ocr/page/N`). ✅
- Each thumbnail shows a **recognized / not-recognized** marker (green/grey),
  plus an **edited** marker (amber) when the page's text was modified. ✅
- Clicking a thumbnail or using the ‹ › buttons navigates — **allowed while a
  recognition run is in progress**. ✅

## 4.8 Stop / cancellation
- The Stop button calls `AppController::stop()`, which sets a stop flag and
  calls `OpenAiProvider::abort()`; the sequential "recognize all" loop halts
  cleanly and already-recognized pages are preserved. ✅

## 4.9 Text editing
- The right pane is an **editable** `TextArea` (read-only until the current
  page is recognized). ✅
- Edits are stored **per page** in `AppController` (`m_edits`, keyed by page
  index) and override the recognized text for both display and export. ✅
- An edited page is flagged (`PageListModel` `edited` role → amber dot) and
  can be **reverted** to the original recognition (`revertCurrentPageEdits`). ✅
- A fresh recognition of a page **supersedes** any manual edit on it. ✅
- QML syncs safely: programmatic reloads are guarded (`textArea.syncing`) so
  they never look like user input, and an unchanged reload does not reset the
  caret (important during "recognize all" while editing).

## 4.10 Export
Implemented by the **`Exporter`** component. **Markdown is the single internal
source of truth** (ADR #5/#11). Export uses the **effective** text per page —
i.e. the user's edit when present, else the raw recognition.

| Format   | Implementation                                   | Status |
| -------- | ------------------------------------------------ | ------ |
| TXT      | directly (page separators)                       | ✅     |
| Markdown | directly (`## Page N`) — the internal source     | ✅     |
| HTML     | directly (escaped, self-contained `<section>`)   | ✅     |
| DOCX     | via **Pandoc** (Markdown on stdin → .docx)       | ✅     |
| PDF      | **Pandoc** if available, else built-in `QPdfWriter` + `QTextDocument` fallback | ✅ |

- Pandoc is discovered once via `QStandardPaths::findExecutable("pandoc")`.
- The Save dialog advertises **DOCX only when Pandoc is present**
  (`AppController::exportNameFilters` / `pandocAvailable`). PDF is always
  offered because of the built-in fallback writer.

## 4.11 RAG service (later stage) — ⬜ not started
- A separate Python process (FastAPI + Chroma/Qdrant).
- LLocr sends recognized text for indexing over HTTP.
- At an early stage — only an **interface stub** in the backend.
