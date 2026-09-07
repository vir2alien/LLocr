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
  (`QSettings`), edited in the tabbed **Settings** dialog. The recognition
  **prompt** is supplied by the chosen **model preset** (see 4.17); a built-in
  default (`AppController::m_prompt`, "document parsing.") applies when no
  preset is in use.

## 4.2 Settings
All configuration lives in the **Settings dialog**, grouped into tabs:

| Tab        | Fields                                                              |
| ---------- | ------------------------------------------------------------------- |
| UI         | language (System / English / Русский), theme (System / Light / Dark) |
| Connection | base URL, API key (optional), request timeout                       |
| Model      | model name, temperature, max tokens, DRY multiplier, DRY base,      |
|            | DRY allowed length, DRY penalty last-N                              |
| Output     | output parser (`raw` / `det_tokens`; default `det_tokens`)           |
| Runtime    | managed `llama-server` path + probe, Start/Stop/Restart, Show log,   |
|            | stage-D installer (release/backend, download+install, updates,      |
|            | cleanup); “Launch setup wizard…”                                    |
| Models     | installed models table (activate/remove), preset catalog, HF search  |
|            | + install, HF token, catalog import/export (see 4.17)               |

Persistence is handled by `SettingsStore` (`QSettings`, grouped keys
`provider/*`, `model/*`, `output/*`, `runtime/*`, `launch/*`, `hf/*`). UI state
(theme, language, window geometry) is persisted under `ui/*`.

> The recognition **mode** (`External` / `Managed`) is chosen by the first-run
> wizard; existing profiles stay `External` (ADR 26/31). In `Managed` mode the
> launch parameters live in the wizard's **Launch** step (port, ctx-size,
> n-gpu-layers, `autoStart`, command preview + self-test), there is no separate
> “Launch” settings tab.

> **Not in the dialog yet:** the bbox coordinate range is hardcoded in
> `DetTokensParser` (`kBboxCoordinateRange = 1000`). The recognition prompt is
> supplied by the selected **model preset** (`ModelPreset.prompt`); without a
> preset a built-in default (`AppController::m_prompt`, "document parsing.")
> applies.

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
Unit tests live under `tests/` (Qt Test) and are all registered in
`tests/CMakeLists.txt`; built when `LLOCR_BUILD_TESTS=ON` (default).

Base suite (stages 1–3):
- `test_det_parser`, `test_pagemodel`, `test_settings_store`, `test_exporter`.

Local-runtime suite (stages A–H):

| Target                     | Covers                                                        |
| -------------------------- | ------------------------------------------------------------- |
| `test_launch_config`       | argv, `extraArgs`, defaults, paths with spaces/cyrillic       |
| `test_runtime_lifetime`    | single `RuntimeController` instance; QML singleton identity   |
| `test_runtime_locator`     | probe, tolerant version parse, non-standard binary name       |
| `test_capabilities`        | build profiles + `--help` parse, flag screening               |
| `test_server_process`      | state machine, health, timeout, stop, restart, ring log       |
| `test_ensure_connection`   | External/Managed resolve, dedup, cancel, timeout, self-test    |
| `test_download_manager`    | `If-Range`/`Content-Range`, ETag change, resume+sha256, cancel |
| `test_release_catalog`     | real-release fixtures, `sha256` from body, asset selection    |
| `test_archive_extractor`   | zip-slip, UNC, symlink, bomb, duplicates, permissions         |
| `test_install_transaction` | failure at every step leaves no garbage / settings intact     |
| `test_model_catalog`       | pagination, revision pinning, mmproj, multi-part, path encoding |
| `test_model_registry`      | recovery, managed vs external, active-model removal guard     |
| `test_model_memory_estimator` | GGUF + KV-cache RAM estimate (H.2)                        |
| `test_install_lock`        | dedicated `.install.lock`: path, refusal while another instance installs, release (H.6) |

Plus the helper `mock_llama_server` binary (not a ctest target): a local
`QTcpServer` stand-in with `--version`/`--help`, `/health`, `/v1/models`,
controlled crash and delayed start. **No test uses the real network** — only
local `QTcpServer` and fixtures in `tests/data/`.

> The four base targets from `AGENTS.md` are still present; the runtime suite
> above is what the local-runtime plan (stages A–H) added.

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

## 4.16 Local runtime (managed llama.cpp)
The managed-runtime layer (`src/runtime/`) lets LLocr start, own and stop a
local **`llama-server`** process (stages A/B, D of `docs/09-local-runtime-plan.md`).

- **Connection mode** (`External` / `Managed`) is chosen by the first-run wizard
  (ADR 31). In `External` nothing is launched; the app talks to an existing
  OpenAI-compatible endpoint. In `Managed` LLocr starts the server itself. ✅
- **`RuntimeController`** (QML singleton `Runtime`, created in `main.cpp`, ADR 36)
  is the facade: `ensureConnectionReady()`, `cancelPendingStart()`,
  Start/Stop/Restart, probe/auto-detect, `estimateModelMemory()`, and QML state
  (`state`, `busyState`, `statusMessage`, `loadProgressPercent`, `configValid`,
  `lockedOut`). ✅
- **`SelfTestController`** (QML singleton `SelfTest`) — wizard “Check”:
  `runSelfTest()`/`runSelfTestQml()`, `selftest*` state; a separate consumer of
  `ensureConnectionReady()` (review 3.4 extraction). ✅
- **`RuntimeLog`** (QML singleton `RuntimeLog`) — live server-log view
  (`serverLog` + copy/clear/open), fed by the facade on each (re)spawn. ✅
- **Process**: `LlamaServerProcess` (QProcess, argv-only, ring buffer + rotating
  file log, health polling, crash-restart ≤3×/5 min, terminate→kill) with a
  `ProcessGuard` (Job Object on Windows, `PDEATHSIG` on Linux, best-effort
  `owner.json` on macOS — ADR 30). ✅
- **Connection resolution**: Managed resolve = start → GET `/health` (fallback
  `/v1/models`) → verify `--alias` → `ResolvedConnection`; concurrent callers
  share one future; Stop cancels a pending start (§G-core). ✅
- Install (stage D) in **Settings → Runtime** via the `RuntimeInstaller`
  singleton: `ReleaseCatalog` (GitHub Releases + `sha256`), backend picker,
  `DownloadTask`/`DownloadManager`, hardened `ArchiveExtractor` (ZIP +
  `.tar.gz` via zlib), transactional `InstallTransaction`, «Check for updates»,
  «Clean up unused builds». ✅
- **No-orphan guarantees** and the error→message **matrix §7.5** are described in
  `docs/09-local-runtime-plan.md`. ✅

## 4.17 Models and catalog (Hugging Face)
Model management lives in **Settings → Models** via the `ModelInstaller`
singleton (stage E).

- **`ModelCatalog`** — Hugging Face tree API with `Link: rel=next` pagination,
  **commit-SHA pinning** (the tree and every download use the pinned `sha`, so
  files can't change between browse and download), `mmproj`/multi-part GGUF
  detection, search (`/api/models?search=…&filter=gguf`); gated repos →
  `401/403` handling with a license link (ADR 29). ✅
- **`ModelPreset` / `ModelPresetCatalog`** — pre-verified `model+mmproj+parser+
  prompt+ctx` pairs. Built-in `:/models/default-presets.json` (read-only) +
  user `models/catalog.json` (read/write), merged by `id` (user wins),
  import/export/reset (ADR 42/43). ✅
- **`ModelRegistry`** — persists installed models to `<modelsDir>/index.json`
  (`QSaveFile`), recovers by rescanning on corruption, distinguishes
  **managed** (inside `modelsDir`, removable) vs **external** (user-provided,
  never deleted, only “remove from list”), and forbids removing the active
  model while the server is `Ready`. ✅
- **Download & verify** — each GGUF is downloaded with resume (`sha256` from
  `lfs.oid` or the preset) and magic-checked (`GGUF`); multi-part models are
  fetched as one unit. ✅
- **HF token** is optional (for gated repos); `401/403` → “gated repository” +
  a link to the license/accept page (ADR 29/44/45). ✅
- Memory: the wizard's **Launch** step shows a **model + KV-cache RAM estimate**
  and warns when the total looks high vs system RAM (`ModelMemoryEstimator`, H.2). ✅

## 4.18 First-run wizard (Stage F)
On a clean profile (`setupVersion == 0 && !setupDismissed`) `Main.qml` triggers
`SetupWizard.qml` — **no network probes** (ADR 31). Steps:

1. **Welcome** — “Local server (recommended)” vs “I already have a server/api”;
   choosing External finishes the wizard (`setupVersion = 1`), opens Connection.
2. **Runtime** — download llama.cpp (auto platform/backend + manual backend) or
   point at an existing binary; progress/errors/retry; SmartScreen/AV hint.
3. **Model** — preset from the catalog / HF search / local GGUF; size, approx
   VRAM, license link.
4. **Launch** — port, ctx-size, n-gpu-layers, `autoStart` (off by default,
   ADR 44), command preview, memory estimate warning (H.2), **Check** →
   `runSelfTestQml()` (start → health → `/v1/models` → one OCR request).
5. **Done** — summary; `setupVersion = 1`.

Navigation: Back / Next / Skip; a step can't be left forward until its
condition holds; the ✕ sets `setupDismissed = true`. All strings are `qsTr`
and translated in `llocr_ru.ts`. ✅

## 4.19 UI sketch (G-UI)
Once the runtime is wired, the main window surfaces its state:

- **`Footer.qml` managed-runtime indicator** — a colored dot + text:

  | State      | Dot   | Meaning                                    |
  | ---------- | ----- | ------------------------------------------ |
  | `Stopped`  | grey  | no server / not configured                 |
  | `Starting` | yellow | server starting, model loading (progress)  |
  | `Ready`    | green  | server up, model loaded, recognition ok    |
  | `Failed`   | red    | startup/probe failed (§7.5 message)         |
  | External   | grey   | mode is External (no managed server)       |

  Clicking the badge opens the shared **`ServerLogWindow`**; if runtime isn't
  configured it opens Settings → Runtime instead (H.1 empty state). ✅
- **Loading progress** — while `StartingRuntime`, the footer shows an
  indeterminate progress bar plus the stderr text; once the model-load
  percentage is classified (`loadProgressPercent()`, H.7) it becomes
  deterministic (“Loading model… N%”). ✅
- **Restart banner** — editing `launch/*` while the server is `Ready` shows
  «Launch settings changed — restart required» with a **Restart** button. ✅
- **Error surfacing** — `Failed` messages follow the §7.5 matrix. ✅
- **Settings → Runtime** carries Start/Stop/Restart, probe status, Show log,
  and the stage-D installer; **Models** carries the model table/presets/HF. ✅
