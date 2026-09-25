# 04. Components

Status legend: ✅ implemented · 🟡 partial · ⬜ not started

## 4.1 OCR models and transport (llama.cpp, OpenAI-compatible API)
- **OcrModel / UnlimitedOcrModel** ✅ (`src/models/`): each OCR LLM is one
  adapter class. The abstract base `OcrModel` encodes the image (base64 data
  URL), builds the OpenAI-style chat-completions body (`buildRequestBody`,
  virtual hook) and parses the response (`parseResponse`, virtual hook);
  async (`QPromise`/`QFuture`), per-request timeout, and **`abort()`** for Stop.
  `UnlimitedOcrModel` supplies its single prompt variant ("document parsing.")
  and the default parser (`det_tokens`); new LLMs are added as new subclasses
  registered in `OcrModelFactory` (ADR 58).
- **LlamaClient** ✅ (`core/LlamaClient.h`): thin transport — POSTs JSON to
  `/v1/chat/completions` with an optional bearer token, per-request timeout and
  `abort()`; surfaces the server's error message. Former provider layer reduced
  to exactly this.
- Configuration ✅: base URL, API key (optional), timeout — in `ConnectionConfig`;
  the OCR model adapter (`model/recipeId`, default `unlimited-ocr`), model name
  and the request-body parameters — one **request profile per OCR model**
  (DRY params, temperature, max tokens; ADR 59). The recognition **prompt**
  comes from the selected model adapter's `promptVariants()` (ADR 58).
- **Text verification (check model)** ✅ (`CheckController` +
  `GeneralPurposeModel`/`QwenGeneralModel`, ADR 68/74/78/79): a small
  general-purpose model verifies a recognized block — the request carries the
  block crop, the recognized text, the type prompt and the protocol contract
  (`OK`/`FIX`/`REVIEW`), with the validate request profile's parameters. It
  uses the same resolved connection (role-aware model name per ADR 73) and the
  single managed instance is re-loaded per task (ADR 74). Checking runs on the
  current page (`checkEnabledBlocksOnPage`), across all recognized pages
  («Проверить всё», `checkAllEnabledBlocks`), or automatically after a
  recognition run when `check/autoCheck` is on (Config: Settings → Проверка →
  Blocks) — the automatic run passes `onlyUnchecked`, so blocks that were
  already verified (`checkStatus != NotChecked`) are skipped and a re-
  recognition only verifies the newly recognized blocks. In Managed mode the
  server switches from the OCR to the check model automatically (ADR 74), in
  External mode the check just starts (ADR 79).
  The serial check queue (task list, progress, stop) lives in
  **`VerificationQueueController`** which owns `CheckController` and publishes
  results to `AppController` via `blockChecked` (ADR 86) — `AppController`'s
  `check*` API to QML is unchanged.
  The queue survives page switches (parity with recognition), so a batch check
  keeps running while the user browses (ADR 79).
  The Blocks tab list is backed by group-filtered proxy models
  (`BlockGroupFilterModel` over the shared `VerificationBlocksModel`, exposed
  as `Verification.blockModelContent/Captions/Service`, ADR 82); the embedded
  list components (`Common/ModelInstalledList`, `ModelPresetList`,
  `RuntimeBuildsList`) expose `implicitHeight` (cap via `maxVisibleRows`,
  `<= 0` = uncapped) so consumers bind heights instead of re-deriving row
  counts (ADR 82). The HF preset install pipeline lives in
  `ModelInstallTransaction` (prepare → download → finalize, ADR 85);
  `ModelInstaller` stays the QML façade over presets, the registry and
  role-filtered views.

## 4.2 Settings
Settings are split into **separate non-modal windows** opened from the Header
menu (ADR 70): **Interface**, **Output**, **Runtime**, **OCR model** and
**Check model**. Interface settings apply immediately; the other windows commit
on **Save** and discard on **Cancel**. **Interface**, **Output** and **Runtime**
windows carry a scoped **«Restore defaults»** (ADR 75) that writes the matching
rows of the table-driven `kDefaults` registry through the property setters; the
model windows have per-tab resets instead (profile restore in Launch/Request).

| Window      | Fields                                                              |
| ----------- | ------------------------------------------------------------------- |
| Interface   | language (System / English / Русский), theme (System / Light / Dark); applies immediately, scoped «Restore defaults» |
| Output      | output parser (`raw` / `det_tokens`; default `det_tokens`); **split   |
|             | pages** (`output/splitPages`, default on — `---` rule in MD, dash     |
|             | line in TXT, `<hr>` in HTML, page break in PDF/DOCX — no “Page N”     |
|             | headings, ADR 64); **keep page numbers** (`output/keepPageNumbers`,   |
|             | default on — off drops `page_number` blocks at parse time); **keep    |
|             | tables as HTML** (`output/tablesAsHtml`, default off — on passes the  |
|             | model's `<table>` block through verbatim instead of a pipe table, ADR |
|             | 19); PDF export: orientation (`export/pdfLandscape`), margins in mm   |
|             | (`export/pdfMarginMm`, 0–50, default 15); «Restore defaults»         |
| Runtime     | connection mode (`External` / `Managed`, applies immediately);        |
|             | External: endpoint base URL, **model name (OCR)** and **model name    |
|             | (validator)** for multi-model servers (ADR 73), API key, request      |
|             | timeout; Managed: `llama-server` path + probe, Start/Stop/Restart,    |
|             | Show log, installer (release/backend, download+install, updates,      |
|             | cleanup); «Launch setup wizard…»; «Restore defaults»                  |
| OCR model   | tabs **Location** (model + mmproj paths, installed models, preset     |
|             | catalog, HF search), **Launch** (launch-parameter profile, ADR 57),   |
|             | **Request** (OCR model adapter `model/recipeId` + the request         |
|             | profile, ADR 56/59)                                                   |
| Check model | the same tab set for the verification model (ADR 71): its own         |
|             | `check/modelPath` + `check/mmprojPath`, the validate launch profile   |
|             | and the validate request profile; installs started here activate as   |
|             | the check model without touching OCR settings                         |

The single managed llama-server instance serves both roles one at a time:
`RuntimeController` (re)loads the model per task (ADR 74). Parallel roles are
only possible against external servers, which get per-role model names
(ADR 73). Model presets live in per-role catalogs (`defaultLlmPresetsOcr` /
`defaultLlmPresetsValidate`, ADR 72; see 4.17).

Persistence is handled by `SettingsStore` (`QSettings`, grouped keys
`provider/*`, `model/*`, `parser/id`, `output/*`, `export/*`, `runtime/*`,
`launch/*`, `check/*`, `hf/*`). UI state (theme, language, window geometry) is
persisted under `ui/*`. The resettable keys are registered in the table-driven
`kDefaults` array (ADR 50); `resetToDefaults()` resets the full table, the
per-window scoped resets use the same rows (ADR 75).

> The recognition **mode** (`External` / `Managed`) is chosen by the first-run
> wizard; existing profiles stay `External` (ADR 26/31). The **Launch** tab in
> the model windows edits the llama-server launch-parameter profiles; the
> first-run wizard's **Launch** step (port, ctx-size, n-gpu-layers,
> `autoStart`, command preview + self-test) covers the same profile on a clean
> setup.

> **Not in the settings yet:** the bbox coordinate range is hardcoded in
> `DetTokensParser` (`kBboxCoordinateRange = 1000`). The recognition prompt is
> owned by the selected **OCR model adapter** (`promptVariants()`, ADR 58) and
> is not editable; the `parser`/`prompt` fields of model presets are stored
> metadata only. The check-request parameters live in the validate request
> profile (Settings → Check model → Request).

## 4.3 Themes
Three options handled by `UiController` (a QML singleton), selected in
**Settings → Interface**:
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
  `equation` → equation, `table` → GFM pipe table (`output/tablesAsHtml`
  keeps the model's `<table>` HTML instead, ADR 19), `ref_text` → plain text.
  The free function `rebuildPageText()` regenerates the page Markdown from the
  boxes (used after a box is removed, to keep `image://ocr/crop/<N>` indices
  consistent).

## 4.5 Image, PDF and DjVu loading
- Images: `QImage` (single or multiple via `loadImages` / `appendImage`). ✅
- PDF: rendered page-by-page via **Qt PDF (`QPdfDocument`)** at ~150 DPI. ✅
- DjVu dependency: **DjVuLibre** via its public C decoding API (`ddjvuapi`),
  wrapped by `src/app/DjVuDocument.h/.cpp` and used by `DocumentModel`.
  The decoder is a **required link-time dependency**, not an external `ddjvu`
  process or a Qt image plugin. QML does not access the library directly.
  CMake links `DjVuLibre::DjVuLibre` into every target compiling
  `DocumentModel.cpp`: `llocr`, `test_document_model`, and
  `test_djvu_document`, plus the `test_app_import` controller integration test.
  See ADR 65 and [dependency setup](06-dev-setup.md#djvulibre-required).
  `.djvu` and `.djv` files share the image/PDF opening and drag-and-drop path.
  Native scan resolution is retained up to 40 megapixels and 16384 pixels per
  side. Orientation is preserved; full-size images are rendered lazily through
  the existing four-image cache. Decoder waits are bounded to 30 seconds per
  operation. DjVu import prepares metadata and thumbnails on a `QtConcurrent`
  worker with an independent decoder; a GUI-owned `QFutureWatcher` commits
  the prepared pages under a short `m_documentLock` write lock. No worker
  accesses the controller or live document. Files are processed sequentially
  to preserve mixed-selection order. `importing` drives the footer spinner
  and a filename/file-count status, and prevents overlapping import, OCR,
  export, page removal and reordering. Unreadable pages become white replacements
  preserving source indices; metadata failures use an 800×1000 fallback size.
  `sourceError` retains the decoder error; import reports a warning count and
  the first error, and selecting a replacement shows its warning in the footer.
  OCR skips replacements; they remain unrecognized and are not included in
  recognized-text export. Failure to open the document itself still rejects
  the file. Closing the controller disconnects delivery
  without waiting on the worker (the thread pool can still wait at app exit).
  Import cancellation and per-page progress are not implemented. PDF/raster
  loading and lazy full-size rendering retain their existing synchronous paths.
- Multi-page documents → page-by-page processing, with a **"Recognize all"**
  batch run and a page-thumbnail strip. ✅
- Pages can be **deleted** (`removePage`) and **drag-reordered** (`movePage`);
  PDF/DjVu pages and images can be appended to an open document. ✅

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
  `OcrModel::abort()` → `LlamaClient::abort()`); the sequential "recognize all"
  loop halts cleanly and already-recognized pages are preserved. ✅

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
Implemented by the **`Exporter`** component, orchestrated by
**`ExportController`** (owns `Exporter` + `ExportRenderer` and the whole
export flow; `AppController::exportPages` is a forwarder, ADR 86).
**Markdown is the single internal
source of truth** (ADR #5/#10). Export uses the **effective** text per page —
i.e. the user's edit when present, else the raw recognition.

| Format   | Implementation                                   | Status |
| -------- | ------------------------------------------------ | ------ |
| TXT      | directly (page separators)                       | ✅     |
| Markdown | directly (`## Page N`) — the internal source     | ✅     |
| HTML     | **preview-pipeline render** (`ExportRenderer`): marked + DOMPurify + KaTeX in a headless `QWebEnginePage`, self-contained file (styles + KaTeX fonts inlined); pages separated by a horizontal rule; fallback — escaped-text writer | ✅ |
| DOCX     | via **Pandoc** (Markdown on stdin → .docx, native Word equations); each source page starts on a new page (raw OpenXML page break) | ✅     |
| PDF      | **preview-pipeline render** (`ExportRenderer` + `QWebEnginePage::printToPdf`); each source page starts on a new page; fallback — built-in `QPdfWriter` + `QTextDocument` | ✅ |

- The HTML/PDF render path reuses the preview bundle (`qrc:/preview/export.html`:
  marked + DOMPurify + KaTeX), so the export matches the Markdown preview 1:1
  (real headings, tables, code blocks, math). Pandoc is **not** needed for PDF
  anymore (the old Pandoc→LaTeX path is gone); it stays for DOCX only (ADR 63).
- **Split pages** (`output/splitPages`, default on): pages are separated
  without any "Page N" labels — a horizontal rule (`---`) between pages in
  Markdown, a dash line in TXT, `<hr>` in HTML; PDF and DOCX start each source
  page on a new physical page (print CSS `break-before:page` / raw OpenXML
  page break). Split off → pages flow continuously without separators (ADR 64).
- The render runs on the **UI thread** (`ExportRenderer`, off-screen
  `QWebEnginePage`); the heavy crop→PNG/base64 encoding and file writing stay
  on worker threads. Progress is surfaced in the status line
  ("Exporting… (n/N)").
- Pandoc is discovered once via `QStandardPaths::findExecutable("pandoc")`
  (`Exporter::isPandocAvailable()` / `pandocExecutable()`); when the PATH
  lookup fails, the installers' well-known locations are probed too — Windows:
  `%LOCALAPPDATA%\Pandoc`, `Program Files\Pandoc`, `Program Files (x86)\Pandoc`;
  macOS: `/usr/local/bin`, `/opt/homebrew/bin`, `/opt/local/bin` — a GUI launch
  often sees a stale or shortened PATH even with Pandoc installed.
- The Save dialog advertises **DOCX only when Pandoc is present**
  (`AppController::exportNameFilters`). PDF is always offered because of the
  built-in fallback writer.
- Image blocks are emitted as `![alt](image://ocr/crop/<boxIndex>)`. Since those
  references have no meaning outside the app, every export path resolves them
  first: the render path embeds them as `data:` URLs
  (`Exporter::embedImagesAsDataUrls`), the direct writers save real image files
  next to the output (`Exporter::resolveImageReferences`, using the cropped
  pixels from `AppController::croppedImage`).
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
- Language is selected in **Settings → Interface** (System / English / Русский),
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
Model management lives in the **OCR model / Check model settings windows**
(via the role-aware `ModelInstaller` singleton, stage E): the installed-models
table, the preset catalog and the HF search are part of the **Location** tab
(see 4.2).

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

  Clicking the badge opens the shared **`ServerLogWindow`**. (The former
  "opens Settings → Runtime" empty-state shortcut was removed with the settings
  rework — ADR 70; the wizard and Settings cover the unconfigured case.) ✅
- **`Footer.qml` Start/Stop toggle** — next to the indicator, visible only in
  Managed mode. When the config is valid (`configValid`: binary + model exist)
  it starts the managed llama-server with the selected model — the same
  `Runtime.startServer()` as the Start button in Settings → Runtime — so the
  server can be warmed up right after app launch without opening Settings.
  While the server is `Starting`/`Ready` the button turns into **Stop**
  (`stopServer()`); stopping is confirmed when a recognition job is in flight
  (§H.1.4 courtesy, same as Restart). Disabled when the runtime is owned by
  another instance (`lockedOut`) or the config is incomplete (tooltip hints
  at Settings → Runtime). ✅
- **Loading status** — while `StartingRuntime`, the footer shows the busy
  spinner (the same one used for recognition, enlarged to 28 px) plus the
  stderr-derived status text (“Loading model… N%” from
  `loadProgressPercent()`, H.7). The dedicated footer progress bar was
  removed as uninformative — llama.cpp's stderr percent updates too coarsely
  to be useful, spinner + status text read better. ✅
- **Restart banner** — editing `launch/*` while the server is `Ready` shows
  «Launch settings changed — restart required» with a **Restart** button. ✅
- **Error surfacing** — `Failed` messages follow the §7.5 matrix. ✅
- **Settings → Runtime** carries Start/Stop/Restart, probe status, Show log,
  and the stage-D installer; the model windows (OCR/Check) carry the model
  table/presets/HF (Location tab). ✅
