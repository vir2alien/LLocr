Full Code Review Report — LLocr (C++ + QML)

**Scope**: whole project — 132 C++ files (`src/` 100, `tests/` 25, `tools/rasterize_icon` 1) + 30 QML files (`resources/qml/`)
**Method**: both review skills' Phase 1 deterministic linting (C++ linter: 229 findings; QML linter: 373), Phase 1b system `qmllint` (Qt 6.10.3, 1,051 warnings), and 12 parallel deep-analysis agents (6 C++ missions + 6 QML missions), deduplicated and confidence-scored. Framework mode: not applicable (no Qt-module signals found).

**Issues found**: 229 + 373 mechanical (mostly style; ~15 verified false-positive clusters, see §4), 46 confirmed deep findings, 15 investigation targets.
**Status**: 40 of 46 deep findings fixed (all Tier-1 hangs, all user-visible breakage, security XSS, crash-risk D-C-08, the Tier-2 performance block, fix rounds 2, 4–6 — see FIXED marks in §1–§2 and the remaining-work plan at the end of §5). D-Q-11 is partially fixed with a small optional remnant.

---

## 0. Executive summary — the 10 issues that matter most

| #   | Location                                             | Issue                                                                                                                                                                                                       | Confidence | Status |
| --- | ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ------ |
| 1   | `src/runtime/ModelInstaller.cpp:608-631`             | Model install **hangs forever in "Downloading"** when a download fails synchronously (e.g. "not enough free space" on a multi-GB GGUF) — `downloadFinished(false)` is emitted before the installer connects | 95         | FIXED  |
| 2   | `src/runtime/RuntimeController.cpp:549-575`          | **"Restart" never restarts** a Ready/Starting server — the `Qt::SingleShotConnection` handler is consumed by the transient `Stopping` emission                                                              | 92         | FIXED  |
| 3   | `resources/qml/SettingsDialog/ModelsTab.qml:292-294` | HF search rows go stale when a new search returns the same row count → **Install installs the wrong model**                                                                                                 | 88         | FIXED  |
| 4   | `resources/preview/preview.html:88-90`               | Markdown preview renders **unsanitized OCR/LLM HTML** via `marked.parse()` → `innerHTML` — script execution inside WebEngineView (XSS on arbitrary scanned documents)                                       | 90         | FIXED  |
| 5   | `src/runtime/RuntimeController.cpp:301-319, 537-547` | Pressing **Stop (footer) during a managed resolve leaves `busy` stuck forever** — spinner spins, recognition never fails                                                                                    | 85         | FIXED  |
| 6   | `resources/qml/Setup/StepModel.qml:14-15`            | Wizard memory estimate stored in **32-bit `property int`** → warning silently hidden/garbage for every real (≥2 GiB) model — the H.2 feature doesn't fire when it matters                                   | 92         | FIXED  |
| 7   | `resources/qml/ServerLogWindow.qml:118-146`          | Live-indicator is **dead code**: `root.liveDot`/`root.liveFlash` are ids of nested objects, not root properties — the dot never turns green                                                                 | 100        | FIXED  |
| 8   | `src/runtime/DownloadTask.cpp:273`                   | GGUF/runtime **download has no timeout of any kind** — a stalled CDN connection hangs the task indefinitely (every other network path is bounded)                                                           | 90         | FIXED  |
| 9   | `src/app/OcrImageProvider.cpp:15-58`                 | Async thumbnail requests **read `DocumentModel` on the loader thread with no synchronization** — data race when pages are opened/deleted/reordered                                                          | 82         | FIXED  |
| 10  | `resources/qml/SettingsDialog.qml:187`               | Browse→pick a server binary throws `ReferenceError: serverPathField is not defined` — **capability probe silently never runs**                                                                              | 92         | FIXED  |

---

## 1. C++ findings (deep analysis, deduplicated across 6 agents)

### Runtime / process lifecycle

FIXED **[D-C-01] `restartServer()` never restarts a Ready/Starting server** — `src/runtime/RuntimeController.cpp:549-575` — Confidence 92
The handler is connected with `Qt::SingleShotConnection`, so it fires on the *first* `stateChanged` emission. For a Ready/Starting server, `m_server->stop()` synchronously emits `Stopping` first; the lambda sees `state != Stopped`, returns, and the connection is auto-released — the later `Stopped` emission never reaches it. Also `restartConn` is captured by value *before* `connect()` returns, so the in-lambda copy is an invalid connection (the `disconnect` is dead code either way). Only the Failed path works.
*Fix*: drop `SingleShotConnection`; keep a persistent connection that disconnects itself when `Stopped` is observed (captured by reference/member, not by value).

FIXED **[D-C-02] Footer "Stop server" during a Managed resolve leaves `busy` stuck forever** — `src/runtime/RuntimeController.cpp:301-319, 537-547` — Confidence 85
`onServerStateForResolve()` handles only Ready→fetch and Failed→fail; the `Stopping`/`Stopped` states fall through both branches. The UI explicitly allows stopping while a resolve is in flight (Footer.qml:184-198 with confirmation → `Runtime.stopServer()`), but `stopServer()` never touches the resolve state. Result: `m_resolveCallbacks` keeps the recognition callback, `controller.busy` stays true, the spinner spins until the user presses the *work-panel* Stop (which routes through `cancelPendingStart()`).
*Fix*: in the `stateChanged` handler, call `failResolve(tr("Server stopped"))` when `m_resolveInProgress` and the state enters Stopping/Stopped (or make `stopServer()` call `cancelPendingStart()` first).

FIXED **[D-C-03] `startServer()` overwrites a synchronously-emitted `Failed` state with `Starting`** — `src/runtime/RuntimeController.cpp:525-533` + `src/runtime/LlamaServerProcess.cpp:116-124, 143-146, 184-189` — Confidence 80
`LlamaServerProcess::start()` returns an empty error string even when `spawn()` failed synchronously (port allocation failure, `waitForStarted` failure with auto-restart off) and already emitted `stateChanged(Failed)` *inside the call*. Back in `startServer()`, `err` is empty → `setState(Starting)` runs anyway. Footer shows "starting…" indefinitely; a later recognition sees `Starting` as server-live, queues its resolve callback, and hangs.
*Fix*: make `start()` report the synchronous outcome, or have `startServer()` check `m_server->state()` after the call and skip the Starting overwrite.

FIXED **[D-C-04] Download-completion signal lost when a task fails synchronously → install hangs in "Downloading"** — `src/runtime/ModelInstaller.cpp:608-631`; root cause `src/runtime/DownloadManager.cpp:60` + `src/runtime/DownloadTask.cpp:203-230` — Confidence 95 (found independently by 3 agents)
`DownloadManager::enqueue()` starts the task synchronously; `DownloadTask::start()` has three synchronous `fail(); emit downloadFinished(false);` paths (mkpath failure, free-space check on a resumable `.part`, URL-scheme rejection). `ModelInstaller::enqueueFile` connects its counting handler only **after** `enqueue()` returns, so the emission is lost → `m_downloadDone` can never reach `m_downloadCount` → `maybeFinishDownloads()` never runs → busy spinner forever, no error. `RuntimeInstaller::enqueueDownload` (`RuntimeInstaller.cpp:395-415`) guards this exact race with a post-connect `task->state()` check and an explicit comment — ModelInstaller was never given the same fix. Realistic trigger: "Not enough free space on the target volume".
*Fix*: mirror RuntimeInstaller's post-enqueue state check; better, consolidate the completion arithmetic into `DownloadManager` (see D-C-15).

FIXED **[D-C-05] GGUF/runtime download has no timeout of any kind** — `src/runtime/DownloadTask.cpp:273` — Confidence 90 (2 agents)
The body-streaming request sets no `setTransferTimeout()` and has no watchdog; a stalled TCP peer (zero-window, half-open after sleep/resume) fires no further signals — the download hangs forever with no failure and no resume. Every other network path in the codebase bounds itself (LlamaClient manual QTimer, ModelCatalog `waitForReply`, ReleaseCatalog `setTransferTimeout`, RuntimeController health poll).
*Fix*: `setTransferTimeout()` (inactivity timeout — safe for multi-GB transfers) or an inactivity watchdog → `fail()` so the pipeline can resume.

FIXED **[D-C-06] `DownloadManager` never removes finished tasks — unbounded growth for the app session** — `src/runtime/DownloadManager.cpp:39-62` — Confidence 95
Tasks are parented to the manager and appended to `m_tasks` but never removed/deleted after Completed/Failed/Canceled; every install retry re-enqueues all parts as brand-new tasks. Bounded-per-session leak (small objects), but genuine monotonic accumulation in app-lifetime singletons.
*Fix*: defer-delete terminal tasks (`beginRemoveRows`/`endRemoveRows` + `deleteLater`), or cap as a session log; make retries resume existing rows.
*Done*: terminal tasks beyond a cap of 10 are evicted via a queued `evictFinishedTasks()` pass (runs from `onTaskFinished` and after `enqueue`), `beginRemoveRows`/`endRemoveRows` + `deleteLater`; the cap keeps `taskAt()` polling in tests and installers valid. Resume-existing-rows retry stays out of scope (D-C-15).

FIXED **[D-C-07] `m_resolveCallbacks` stores raw `std::function`s capturing raw `this` of consumers — no lifetime guard** — `src/runtime/RuntimeController.h:185, .cpp:203-292`; capture sites `RecognitionController.cpp:78`, `SelfTestController.cpp:43` — Confidence 80
If a consumer is destroyed while a resolve is in flight (a Managed start can legitimately stay `Starting` up to the 180 s startup timeout), `completeResolve()` invokes the queued lambda into a destroyed object → use-after-free. Today only the accident of stack-object construction order in `main.cpp` prevents it.
*Fix*: store `{QPointer<QObject> ctx, std::function}` pairs and skip dead contexts, or move to per-caller QFuture/signal connections.
*Done*: callbacks are queued as `{QPointer<QObject> context, guarded, fn}` (`PendingResolve`); a new `ensureConnectionReady(QObject *context, fn)` overload is used by `RecognitionController` and `SelfTestController` (passing `this`), `completeResolve()` skips dead guarded contexts. The 1-arg overload (used by tests) stays unguarded.

FIXED **[D-C-08] Async image requests read `DocumentModel` on the loader thread with no synchronization** — `src/app/OcrImageProvider.cpp:15-58` (+ `ThumbDelegate.qml:40`, `AppController::pageImage`) — Confidence 82
`ThumbDelegate` sets `asynchronous: true`, so `requestImage` runs on the engine loader thread and reads `m_document` (a plain member, no mutex anywhere in the project) while the GUI thread can open/delete/reorder pages — exactly the moments when `docRevision` bumps re-issue requests → data race on the page vector. (The synchronous preview path is safe.)
*Fix*: guard `pageImage`/`croppedImage` with a QMutex, or hand the provider immutable snapshots.

### Models

**[D-C-09] `dataChanged` after remove/move broadcasts an empty roles vector** — `src/app/PageListModel.cpp:139, 166` — Confidence 85
Only `PageIndexRole` is row-derived and needs refetch, but the empty roles list means "all roles changed" — every delegate re-fetches `recognized/edited/hasDuplicates/current` and re-evaluates the thumbnail `Image.source` on every page delete and drag-reorder.
*Fix*: pass `{ PageIndexRole }` explicitly (the emission itself is intentional and covered by `renumberNotifiesAllRows`).

FIXED **[D-C-10] Leftover `qDebug()` dumps of full OCR text** — `src/parsers/DetTokensParser.cpp:300`, `src/app/RecognitionController.cpp:149` — Confidence 88
Every recognized page's entire raw model output is written to stderr in release builds; costs formatting of large strings, pollutes logs, leaks document content.
*Fix*: remove; gate any logging behind an off-by-default `qCDebug` category.
*Done*: both `qDebug()` lines removed (the error already reaches the UI via `statusRequested`); no category introduced.

FIXED **[D-C-11] Non-const Q_PROPERTY READ accessors `pageModel`/`boxModel`** — `src/app/AppController.h:76-77` — Confidence 85
Only non-const property getters in the project; `QMetaProperty::read()` is specified against `const QObject*`. Trivially constable.
*Done*: both getters are `const` now (member address taken through `const_cast`, the standard CONSTANT-property idiom).

**[D-C-12] Const methods return mutable raw pointers** — `src/app/LaunchProfileStore.h:48`, `src/app/RequestProfileStore.h:41`, `src/runtime/DownloadManager.h:45` — Confidence 80
`draftModel() const` (×2), `taskAt(int) const` hand out mutable interiors; `const` gives no protection at the class boundary. Add const overloads or pick one convention.

FIXED **[D-C-13] `setWindowState()` is the only SettingsStore setter without the change guard** — `src/app/SettingsStore.cpp:287-291` — Confidence 88
Unconditional QSettings write + NOTIFY on every assignment, defeating the dedup invariant the other ~35 setters share.
*Done*: same early-return guard as the other setters.

FIXED **[D-C-14] Duplicated numeric validation; `detectPlatform()` called twice per `activeProfileId()`** — `src/app/LaunchParametersModel.cpp:9-14` vs `src/core/RequestProfile.cpp:117-123`; `LaunchProfileStore.cpp:176-177` — Confidence 82
Same strict finite-number validator implemented twice (drift risk); the deterministic `detectPlatform()` runs twice on every recognition request and draft reload.
*Fix*: hoist one shared helper; call `detectPlatform()` once and take both fields.
*Done*: shared `llocr::toFiniteNumber()` in the new `src/core/ValueParsing.h` (also removes the double parse in the Flag→Number paths); `activeProfileId()` calls `detectPlatform()` once.

FIXED **[D-C-15] Substantial copy-paste between `RuntimeInstaller` and `ModelInstaller`** — `RuntimeInstaller.cpp:114-157, 357-447` vs `ModelInstaller.cpp:224-259, 570-711` — Confidence 85
State enums + guarded setters, progress wiring, `emitDownloadProgress`/`onOneDownloadFinished`/`maybeFinishDownloads`, cancel/shutdown pairs, QtConcurrent+`QPair`+`.then()` plumbing are near line-for-line duplicates — and the divergence already produced D-C-04.
*Fix*: extract a shared download-aggregation helper and an async-step wrapper.
*Done (fix round 5)*: the download-completion arithmetic (counters, sync-fail post-enqueue re-check, byte-progress aggregation, single `allFinished` per group) extracted into the new `runtime/DownloadGroup` (h+cpp, in `src/CMakeLists.txt` + `test_model_installer`/`test_install_lock` sources); both installers now hold a `m_group` and keep only their own state transitions in `maybeFinishDownloads()`. The async-step wrapper was deliberately NOT extracted: the two flows differ in guard strategy (prepare-generation counter vs chained `.then` continuations with different payloads), so a generic wrapper would be a forced abstraction; the diverged state-guard classes (D-C-04) are now structurally impossible to re-diverge.

FIXED **[D-C-16] `selectedRelease` clamped without NOTIFY; `installUpdate()` over-emits** — `src/runtime/RuntimeInstaller.cpp:265-266, 315-316` — Confidence 84
Two sites bypass `setSelectedRelease()` in opposite directions (one silent clamp without signal → stale QML binding; one unconditional emit without change).
*Done*: both sites go through `setSelectedRelease()`, whose change-dedup supplies the missing signal in one direction and removes the unconditional emit in the other.

### Performance

FIXED **[D-C-17] Markdown preview re-encodes every image crop to PNG base64 on the GUI thread per text change** — `src/app/AppController.cpp:555-584` (binding at `WorkPanel.qml:113`) — Confidence 92
A live QML binding calls the Q_INVOKABLE on every text change; each run regex-scans the text and does `QImage::copy` + full-res `save("PNG")` + base64 expansion per crop. The 250 ms MarkdownPreview debounce only covers the WebEngine push, not this.
*Fix*: cache the resolved string keyed on (docRevision, imageRevision, text), debounce, and/or scale crops to display size; long-term serve crops via a scheme handler.

FIXED **[D-C-18] Whole export pipeline runs synchronously on the GUI thread** — `src/app/Exporter.cpp:328` (`waitForFinished(120000)`), entry `AppController::exportPages` — Confidence 90
DOCX/PDF via Pandoc blocks the event loop up to 2 minutes; the built-in PDF fallback holds all crops of all pages in RAM. No busy indication.
*Fix*: run `exportToFile` on `QtConcurrent`; disable the dialog meanwhile; cap PDF memory via temp files.

FIXED **[D-C-19] Each recognition request PNG-encodes the full-resolution page on the GUI thread** — `src/models/OcrModel.cpp:89, 17-28` — Confidence 88
~1–3 MB PNG + base64 per page, between HTTP round-trips in batch mode.
*Fix*: move encode+body-build into the existing QtConcurrent stage; consider JPEG ~90 (5–10× smaller/faster, llama-server accepts it).
*Regression note (fix round 2)*: the JPEG ~90 variant of this fix (commit c748ffb) broke recognition against the real server — llama.cpp **b10964** mtmd fails to decode Qt-encoded JPEG (`mtmd_helper_bitmap_init_from_buf: failed to decode buffer as either image/audio/video` → HTTP 400 "Failed to load image or audio file"), while the same server+mmproj processed PNG fine. Reverted to PNG; the QtConcurrent off-GUI-thread encode is kept. Also hardened: a null image or failed encode now fails the request locally with a clear error instead of posting an empty data URL (covered by `test_ocr_models::recognizeSendsDecodableJpegDataUrl` — now PNG — and `recognizeFailsCleanlyOnNullImage`). *Follow-up*: the local guard fired in the app — the image reaching the encoder was empty. **Root cause (not in this review):** D-C-20's lazy `renderFull()` passed a fresh `DocumentPage` with an **empty `sourcePath`** to `decodeSource()`, so every full-image decode failed with "file not found" while thumbnails (decoded at open from the real path) worked — exactly the empty payload the server rejected. Fixed by copying `sourcePath` into the decode page; error reason now propagates `QImageReader::errorString()`/PDF-open failures through `DocumentModel::fullImage` → `AppController::pageImage` → `RecognitionController` (fail-fast per page); `decodeSource()` keeps `setAllocationLimit(0)` as hardening. Regression coverage: new `test_document_model` target (raster full-decode, missing-file error, cached re-fetch).

FIXED **[D-C-20] `DocumentModel` holds every page's full-resolution image in RAM for the whole session** — `src/app/DocumentModel.cpp:61-93` — Confidence 85
100-page PDF ≈ 830 MB before any results; no lazy render/eviction.
*Fix*: store path+index+size and render on demand (full DPI for the current page, reduced for the strip).

FIXED **[D-C-21] cudart archive extraction runs in the `.then()` continuation on the main thread** — `src/runtime/RuntimeInstaller.cpp:490-502` — Confidence 90
The main archive is extracted on a worker, but the companion CUDA-runtime zip (hundreds of MB) is extracted inside the main-thread continuation → multi-second UI freeze.
*Fix*: chain it as a second `QtConcurrent::run` stage.

FIXED **[D-C-22] `roleNames()` rebuilds the QHash on every call in all five list models** — `BoxListModel.cpp:36`, `PageListModel.cpp:40`, `LaunchParametersModel.cpp:52`, `RequestParametersModel.cpp:37`, `DownloadManager.cpp:146` — Confidence 80
Pure per-call allocation; cache as a member/static const.

---

## 2. QML findings (deep analysis, deduplicated across 6 agents)

### Functional bugs

FIXED **[D-Q-01] Server-log live indicator is dead code** — `resources/qml/ServerLogWindow.qml:118-146` (ids at :42, :141) — Confidence 100 (4 agents independently)
`root.liveDot`/`root.liveFlash` read non-existent root properties (they are ids of a header Rectangle and a file-scope Timer) → the guard is always false: the dot never turns green, `liveFlash` never restarts. Had it been written without `root.`, the ids would resolve.
*Fix*: reference the bare ids, or hoist real root properties and bind the visuals declaratively.

FIXED **[D-Q-02] Export SpinBox arrows permanently muted: `sb.up.enabled`/`sb.down.enabled` don't exist** — `resources/qml/ExportDialog.qml:94, 111` — Confidence 95 (5 agents)
`QQuickIndicatorButton` (SpinBox up/down) exposes only `pressed/hovered/indicator/…` — verified against the Qt private header. The ternary always takes the muted branch.
*Fix*: derive per-direction dimming from `sb.value` vs `sb.from`/`sb.to`, or use `sb.up.pressed` for feedback.

FIXED **[D-Q-03] Browse→pick server binary: `ReferenceError: serverPathField is not defined`, probe silently skipped** — `resources/qml/SettingsDialog.qml:187` — Confidence 92
`serverPathField` is an id in a *different document* (`RuntimeTabInternal.qml:64`); cross-document id lookup is impossible. The handler aborts after saving the setting, so `Runtime.probeRuntimePath(path)` never runs (status stays "Not probed yet"; the field still updates via its binding).
*Fix*: delete the redundant assignment (the binding already syncs), or move the FileDialog into RuntimeTabInternal / expose a function on the tab.

FIXED **[D-Q-04] HF search rows stale on equal-count re-search → Install targets the wrong model** — `resources/qml/SettingsDialog/ModelsTab.qml:292-294`, `resources/qml/Setup/StepModel.qml:296-300`; backing `src/runtime/ModelInstaller.cpp:813-858` — Confidence 88
`model: ModelInstaller.searchCount` with non-reactive `searchResult(index)` delegates and no `Connections` refresh (the sibling installed/builds lists all have one). `startSearch` never clears `m_searchResults`; when two searches return the same count, nothing resets → rows show search A's titles while `installRemote(index)` indexes search B's list. Equal counts are the common case (fixed HF page size).
*Fix*: clear `m_searchResults` at search start (count N→0→M forces resets), add a revision counter, or expose a proper QAbstractListModel.

FIXED **[D-Q-05] Wizard memory estimate broken for ≥2 GiB models: 32-bit `property int`** — `resources/qml/Setup/StepModel.qml:14-15` (usage 142-145) — Confidence 92
`estTotal`/`estRam` take qint64 byte counts; values ≥ 2³¹ wrap → the warning label `visible: estTotal > 0` hides exactly for the large models it exists to warn about, or shows garbage. `StepLaunch.qml:16-19` uses `property real` for the identical data — the correct form is already in the codebase.
*Fix*: change the two properties to `real`.

FIXED **[D-Q-06] ModelsTab error label `statusMsg` is outside the ColumnLayout** — `resources/qml/SettingsDialog/ModelsTab.qml:442-448` — Confidence 92
Sibling of the layout (not inside it): `Layout.fillWidth` is a no-op, no anchors → renders at (0,0) overlapping the top status label; implicit text width means a long localized error overflows the 520 px dialog.
*Done*: moved into the ColumnLayout (bottom, after the catalog buttons) with `Layout.fillWidth` + elide — fixed together with D-Q-22.

FIXED **[D-Q-07] Redundant imperative `field.text = path` assignments permanently destroy `Settings.*` bindings** — `RuntimeTabInternal.qml:85`, `Setup/StepRuntime.qml:263`, `Setup/StepModel.qml:439` — Confidence 85
In all three the preceding `Settings.*` write already updates the bound field, and the imperative write breaks the binding for good — e.g. after Browse-picking a GGUF, a later `ModelInstaller::setActiveModel` (`ModelInstaller.cpp:311`) changes `Settings.launchModelPath` but the "Local file" field keeps showing the old path.
*Fix*: delete the imperative assignments; fields bound to `Settings.*` should never be assigned imperatively.

FIXED **[D-Q-08] Delegate revert `text = model.valueText` destroys the row's model binding** — `SettingsDialog/LaunchTab.qml:139`, `RequestTab.qml:132` — Confidence 80
After a rejected `setDraftValue`, the binding is gone; once rows shift after removal, the field shows a previous row's value and the `if (text === model.valueText) return` guard silently swallows edits.
*Done*: revert is `text = Qt.binding(() => model.valueText)` in both tabs.

FIXED **[D-Q-09] C++ singleton status strings are not retranslated on language switch** — `Footer.qml:74-80, 171-177` (+ ModelsTab.qml:42, RuntimeTabInternal.qml:367, StepModel.qml:65, StepLaunch.qml:231-234) — Confidence 85
`engine.retranslate()` re-runs QML `qsTr()` bindings, but `Runtime/ModelInstaller/RuntimeInstaller/SelfTest` `statusMessage` strings are `tr()`-built C++ values with **no `LanguageChange` handler** (grep: none) — after a switch the UI shows a mix of old-language status text and new-language labels until the next state change.
*Fix*: handle `QEvent::LanguageChange` in the singletons and regenerate + re-emit `statusMessage`; or store state codes in C++ and translate in QML.

### Security

FIXED **[D-Q-10] Markdown preview renders unsanitized OCR/LLM HTML — script execution inside WebEngineView** — `resources/preview/preview.html:88-90` (also :50, :61-63; `MarkdownPreview.qml:36-46`) — Confidence 90
Vendored **marked v15.0.12** passes raw inline HTML through by design; `el.innerHTML = marked.parse(p.md)` on untrusted OCR/LLM text executes injected JS (inline handlers are not "navigations", so the `onNavigationRequested` blocker doesn't help). The page runs with `localContentCanAccessFileUrls: true`, no CSP, default persistent profile. `restoreMath()` re-inserts stashed `$…$` content post-parse — same class.
*Fix*: bundle DOMPurify locally and sanitize before `innerHTML` (and after math restore); set `localContentCanAccessFileUrls: false`; add a CSP meta; consider an off-the-record profile. If link-opening is wired later, restrict to http(s) + user confirmation.

### Performance & memory

FIXED **[D-Q-11] Thumbnail strip decodes and caches full-resolution pages for ~150 px thumbs** — `ThumbDelegate.qml:36-44`, aggravated by `ThumbPanel.qml:25` (`cacheBuffer: 10000`) and `AppController.cpp:52-57` (`docRevision` bump on every document change re-keys *all* thumbnail URLs) — Confidence 95 (3 agents)
No `sourceSize` → `requestedSize` invalid → the provider's scaling branch never runs → each thumb is a full-res ~8.7 MB (150 dpi PDF) to ~35 MB (300 dpi scan) texture; `cacheBuffer: 10000` keeps ~45 extra delegates per side alive; `cache: true` retains them; every open/delete/move re-decodes the whole strip.
*Fix*: set `sourceSize` to display size × devicePixelRatio (provider then scales, ~10-40× less memory); reduce `cacheBuffer` to ~800-1500 px; key URLs on per-page revisions.
*Done (fix 18)*: the dominant issue is resolved architecturally — `page/N` now serves pre-rendered small thumbnails (~220×300, `DocumentModel::thumbnail`) instead of full-res pages, so thumb memory dropped from ~8–35 MB to ~0.3 MB per page regardless of `sourceSize`.
*Done (fix round 2)*: `cacheBuffer: 1000`. *Remaining (optional)*: per-page URL keying so an open/delete/move no longer re-decodes the whole strip.

**[D-Q-12] Main preview decodes full-resolution pages synchronously on the GUI thread** — `resources/qml/MainWindow/ImagePreview.qml:7-13` — Confidence 90
No `sourceSize`, no `asynchronous`, `cache: false` — every page switch copies and uploads a full-res texture (up to ~134 MB for a 600 dpi scan) in binding evaluation on the GUI thread; minification aliasing as a bonus.
*Fix*: `sourceSize` bound to viewport × devicePixelRatio (provider honors it with Smooth+KeepAspectRatio) + `asynchronous: true`.

FIXED **[D-Q-13] Server log rebuilt in full and re-set on every appended line, even while the window is hidden** — `ServerLogWindow.qml:102` + `src/runtime/RuntimeLog.cpp:29-44` — Confidence 95
Per-line `serverLogChanged()` → `ringBuffer(2000).join("\n")` → wholesale `TextArea` re-layout of up to 2000 lines, during exactly the phases when llama-server bursts hundreds of lines per second. The window is instantiated at startup (`Main.qml:144-146`), so the churn runs even when closed.
*Fix*: coalesce in C++ (dirty flag + 150-250 ms flush) or an append-only model; QML stopgap: gate the binding on `root.visible`.

**[D-Q-14] Preview Loader destroys/recreates the WebEngineView on every toggle** — `resources/qml/MainWindow/WorkPanel.qml:103-106` — Confidence 82
Toggling Preview off/on re-initializes Chromium + marked + KaTeX each time (hundreds of ms, visible blank, scroll lost); also re-pushes large base64 payloads if a recognition is running.
*Fix*: latch the Loader active after first activation and toggle `visible` instead (accept a resident renderer), or show an explicit "rendering…" placeholder.

FIXED **[D-Q-15] Wizard steps' `Component.onCompleted` side effects run at every application startup** — `Setup/StepRuntime.qml:14`, `Setup/StepModel.qml:31-38` (also `Setup/StepLaunch.qml:60-63`) — Confidence 85
`SetupWizard` is declared eagerly in Main.qml; its steps are plain StackLayout children, so `rescanInstalledBuilds()`, `estimateModelMemory()`, `refreshInstalled()`, `reloadPresets()` all run at startup even for users who never see the wizard — then run again from `startWizard()`/`onAboutToShow`.
*Done*: the step `Component.onCompleted` hooks became `onVisibleChanged` guards (StackLayout toggles child visibility, so the scans now run when the step is actually shown); `ModelInstaller.refreshInstalled()` moved to the Models-tab toggle in `SDTabBar.qml` so the ADR-62 reset recovery still covers the Settings tab without the wizard.

### Structure & maintainability

FIXED **[D-Q-16] Installer UI duplicated across Settings tabs and wizard steps** — `RuntimeTabInternal.qml:176-409` ≈ `StepRuntime.qml:93-204`; `ModelsTab.qml:37-241` ≈ `StepModel.qml:59-346` — Confidence 90
13 duplicated blocks (backend combo, status label formula, builds/models ListView + ~60-line delegate, progress+cancel gating, update check, license dialogs); the fragile `Connections { onXChanged → var = Qt.binding(...) }` re-bind workaround appears **9 times**; the copies have already diverged (backendDisplay fallback exists in only one).
*Fix*: extract `Common/` components (status row, builds list, models list) or drive rows from real QAbstractListModels; delete the re-bind glue.
*Done (fix round 5)*: five new `Common/` components — `RuntimeBuildsList` (delegate + refresh glue + unified backendDisplay fallback + activate logic; `scrollable`/`rowHeight` props), `ModelInstalledList` (glue + unified busy-gate on Activate; `managementActions` toggles size/Remove/Open-folder; `actionError` signal), `ModelPresetList` (glue + Activate inside; `installClicked` signal), `HfSearchList` (`installClicked`), `InstallerProgressRow` (progress + Cancel gating) and `InstallerStatusLabel` (the shared error/busy/muted color formula). All 10 re-bind glue blocks deleted; license/update dialogs stay in the consumers (they genuinely differ).

FIXED **[D-Q-17] 28+ raw integer state comparisons in QML against three C++ enums** — `Footer.qml` (6), `RuntimeTabInternal.qml` (14), `ModelsTab.qml` (3), `StepRuntime.qml` (7), `StepModel.qml` (2) — Confidence 92
`Runtime.state === 0..5`, `RuntimeInstaller.state === 0/1/3/4/6`, `ModelInstaller.state === 2/3/4`, `busyState === 1`, plus tab indices (`selectTab(1)`, `openSettingsRequested(4)`) and the wizard's magic `4`. Any C++ enum reordering silently flips dot colors, disables wrong buttons. (Also: `RequesetTabNum` typo in SettingsDialog.qml:24, and `savaValues()` typo in RuntimeTabExternal.qml:18 / UITab.qml:19 / callers.)
*Fix*: expose a QML singleton with named enums (or int constants on the C++ singletons); fix typos.
*Done (fix round 2)*: `RequesetTabNum` → `RequestTabNum`, `savaValues()` → `saveValues()` (both definitions and all callers), and the wizard's magic `4` (D-Q-19). *Done (fix round 4)*: `RuntimeState`/`AppBusyState` moved into `RuntimeController` with `Q_ENUM` (QML: `Runtime.Ready`, `Runtime.StartingRuntime`, …; `RuntimeState.h` keeps type aliases for C++); `RuntimeInstaller`/`ModelInstaller` enums already had `Q_ENUM` (QML: `RuntimeInstaller.Downloading`, `ModelInstaller.Error`, …); all 32 raw comparisons migrated to named values; the Footer not-configured badge now opens `SettingsDialog.TabsEnum.RuntimeTabNum` (was the raw `4` = LaunchTab).

FIXED **[D-Q-18] No `pragma ComponentBehavior: Bound`; delegates + 3 cross-document id reaches** — project-wide (zero matches); concrete reaches: `ThumbDelegate.qml:93,150,153-161` → `thumbList.*`, `Header.qml:31,85-88,97` → Main.qml ids, `RuntimeTabInternal.qml:134` → `dialog.*` — Confidence 85
*Done (fix round 6)*: pragma added to all 35 QML files (Theme.qml singleton excepted); the cross-document id reaches replaced by explicit contracts — Header emits `openFileRequested`/`exportRequested(multiPage)`/`settingsRequested` (handled in Main.qml, the Footer-signal pattern), ExportDialog emits `exportRequested(scope, fromPage, toPage)` (handled in Main.qml), ThumbDelegate receives `listView` from ThumbPanel, RuntimeTab gets `setupWizardRef`/`logWindowRef` from SettingsDialog and forwards `logWindowRef` to RuntimeTabInternal (a fourth reach, `ExportDialog.qml:55-58` → `exportDialog.*`, was found and fixed en route). Delegate migrations: ThumbDelegate (required `index`/`pageIndex`/`current`/`recognized`/`edited`/`hasDuplicates`), LaunchTab/RequestTab parameter rows (required `index`/`name`/`valueText`/`description`), StepWelcome choice cards (required `index`); the other delegates were already Bound-ready.
The unqualified context chain is exactly what Bound mode removes; D-Q-03 is this bug class in the wild. Delegates in ThumbDelegate/LaunchTab/RequestTab/StepWelcome read `model.<role>` without required properties (ImagePreview.qml:46-54 already demonstrates the correct fully-required pattern; roles verified to match C++ `roleNames()` everywhere).
*Fix*: migrate file-by-file (start with Setup/*, SettingsDialog/*): add required properties, pass cross-file state via explicit properties/signals.

FIXED **[D-Q-19] Wizard step machine couples to `StackLayout.children[]` and magic index 4** — `SetupWizard.qml:39-85` — Confidence 85
`stackLayout.children[current]` breaks (Next permanently disabled, no diagnostic) if any extra Item is ever added to the StackLayout; `4` hardcoded in three gating expressions.
*Done*: `property list<Item> steps` + derived `lastStep` and a declarative `readonly currentStep: steps[current]`; all three `4` comparisons now use `lastStep`.

FIXED **[D-Q-20] Layout-managed items sized with explicit `width`/`height` (documented UB)** — `ThumbDelegate.qml:51-52` (9×9 status dot), `Setup/StepWelcome.qml:50-52` (`width: root.width - 40`, hardcoded 2× the parent's 20px margins) — Confidence 85
Qt Layouts docs: explicit width/height on layout children is "undefined behavior"; works today via the fallback, collapses under compression or future behavior changes. The `- 40` also duplicates the parent's margins.
*Done*: the dot uses `Layout.preferredWidth/Height`; the StepWelcome cards use `Layout.fillWidth: true` + `Layout.preferredHeight`.

FIXED **[D-Q-21] Warning-plaque height ignores its 8px margins — 8px vertical deficit** — `Setup/StepLaunch.qml:145-158`, `RuntimeTabInternal.qml:256-268` — Confidence 82
`implicitHeight: inner.implicitHeight + Theme.spacing` (+8) vs `anchors.margins: 8` (16px chrome) → the bottom row draws ~8px past the plaque border.
*Done*: `inner.implicitHeight + 2 * 8` in both plaques.

FIXED **[D-Q-22] ModelsTab is the only tab without internal scrolling — bottom controls get clipped** — `ModelsTab.qml:33-378` (dialog `SettingsDialog.qml:27-28, 116-129`) — Confidence 80
~17 rows in a fixed 520×600 dialog; with 3+ installed models + presets + one search result the content exceeds the ~476 px budget and `clip: true` silently swallows the HF-token field and Import/Export/Restore row; worse with Russian labels. RuntimeTabInternal already wraps itself in a ScrollView — precedent exists.
*Done*: same ScrollView treatment (`contentWidth: availableWidth`, `contentHeight: layout.implicitHeight`); dialogs stay outside the ScrollView; the now-dead `Item { Layout.fillHeight }` filler removed.

FIXED **[D-Q-23] `controller`/`uiController` exposed as context properties instead of registered singletons** — `src/main.cpp:79-82` — Confidence 95
Nine singletons are properly registered; these two aren't — untyped, unlintable (a large share of the 957 `[unqualified]` warnings), string lookup per binding evaluation, root-context lifetime coupling (today safe by declaration order).
*Done*: `qmlRegisterSingletonInstance` for both (`Controller`, `UiController`); the `UiController` uncreatable-type registration was replaced by the instance registration (QML enum access `UiController.System` keeps working through the type metaobject); all QML uses renamed `controller.` → `Controller.`, `uiController.` → `UiController.` (sed sweep, includes `Theme.qml` which gained `import LLocr` — its URL registration makes the self-import safe); `<QQmlContext>` include dropped.

---

## 3. Investigation targets (human verification needed, confidence 60–79)

| ID   | Location                                                 | Confidence | Suspicion                                                                                                                                                                                                                                          | How to verify                                                                                                |
| ---- | -------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| I-01 | `src/app/PageListModel.cpp:107-122`                      | 70         | FIXED (round 7) — `setCurrent()` now rejects indices outside `[-1, rowCount)` (‑1 stays the legal "no current" sentinel); regression test `setCurrentGuardsRange` in test_pagemodel covers -5/99/-1/re-select                                                              | —                                                                                                            |
| I-02 | `tests/`                                                 | 75         | FIXED (round 7) — `QAbstractItemModelTester` (Fatal) added around `LaunchParametersModel`/`RequestParametersModel` (draft models in test_launch_profile/test_request_profile) and `DownloadManager` (all 10 test managers); new `test_box_model` target adds BoxListModel tester coverage (4 tests: roles, updateBoxRect, removeBox, setFromResult) | —                                                                                                            |
| I-03 | `src/runtime/ModelInstaller.cpp:539-542, 892-898`        | 72         | FIXED (round 2) — `cancelInstall()` now bumps a prepare-generation counter; the in-flight `.then()` continuation drops stale results instead of resurrecting ReadyToDownload                                   | —                                                                                                            |
| I-04 | `src/runtime/LlamaServerProcess.cpp:451-483`             | 65         | FIXED (round 8) — on a crash with auto-restart eligible, the wrapper now sets `Starting` + "Server crashed — restarting…" synchronously with scheduling the respawn, so the ~500 ms window no longer exposes a stale `Ready` with a dead child (a resolve started there waits for the respawn instead of hitting a dead port). Regression test `crashWindowReportsRestarting` (mock `--crash-after`: status flip → `Starting` → stop-in-window lands Stopped)                                                              | —                                                                                                            |
| I-05 | `src/models/OcrModel.cpp:92-105`                         | 68         | FIXED (round 8) — `recognize()` chain made self-contained: `buildRequestBody`/`parseResponse` (which had no overrides) are now `static`, a per-request `shared_ptr<LlamaClient>` is shared between the chain and `abort()` (`m_activeClient`), and **no handler captures `this`** — the model can be destroyed mid-flight safely. Chain-hardening en route: watchers connect *before* `setFuture()` (the Qt "likely race" warning was real) and handler 2 captures the client, so `~QNetworkAccessManager` cannot kill the in-flight reply after handler 1 exits. Regression test `futureCompletesAfterModelDestruction` (model deleted right after recognize; future must deliver "ok") | —                                                                                                            |
| I-06 | `src/app/OcrImageProvider.h:17` + `src/main.cpp:139-148` | 65         | FIXED (round 7) — `appController`/`uiController` moved above the `QQmlApplicationEngine` declaration in main(): the engine (and its owned image provider) is now destroyed first, while the controllers are still alive; the dangling shutdown window is gone                                                     | —                                                                                                            |
| I-07 | `src/runtime/DownloadManager.cpp:20-31`                  | 60         | FIXED (round 7) — defensive `~DownloadTask()` added: disconnects the in-flight reply, aborts it and `deleteLater()`s it, so a directly-destroyed running task can neither leak the reply nor receive callbacks into a half-destroyed object (normal teardown still goes through `abortDownload()`)            | —                                                                                                            |
| I-08 | `src/runtime/RuntimeController.cpp:516-523, 570-584`     | 72         | FIXED (round 2) — `launchCommandPreview()` is cache-only (`RuntimeLocator::cachedProbe()`), never spawns the binary; before the first real probe it renders with default (conservative) capabilities            | —                                                                                                            |
| I-09 | `resources/qml/Main.qml:161`                             | 70         | FIXED (round 2) — wizard "external" path now opens `SettingsDialog.TabsEnum.RuntimeTabNum` (3) where the connection fields live, not Request (1)                                                                | —                                                                                                            |
| I-10 | `resources/qml/MainWindow/Footer.qml:90-99`              | 68         | FIXED (round 3) — the dirty-watch now also tracks `Settings.serverPath` (`onServerPathChanged` → `markLaunchDirty()`), so swapping the managed binary with a Ready server shows the restart banner                                            | —                                                                                                            |
| I-11 | `ModelsTab.qml:186-193`, `StepModel.qml:207-214`         | 65         | FIXED (round 3) — preset delegates refresh on both `installedChanged` and `presetsChanged` via a shared `refreshPresetInfo()`; same-count catalog import/reset no longer leaves stale rows                                                            | —                                                                                                            |
| I-12 | `RuntimeTab.qml:54-60`, `UITab.qml:31,43`                | 65         | FIXED (round 3) — all three ComboBoxes re-sync `currentIndex` in `onModelChanged` from Settings (`syncFromSettings()`/`syncMode()`), so a retranslate-driven model replacement no longer resets the selection (RuntimeTab kept the Managed panel visible)                                                             | —                                                                                                            |
| I-13 | `ServerLogWindow.qml:117-119`                            | 62         | FIXED (round 3) — replaced ScrollView+ScrollBar-position autoscroll with an explicit `Flickable` + `TextArea.flickable` writing `contentY` (gated on `atYEnd`); no dependence on the programmatic ScrollBar→Flickable sync anymore                                                                             | —                                                                                                            |
| I-14 | `ModelsTab.qml:283-330`                                  | 65         | Integer-model staleness may extend beyond the equal-count case to every count change (depends on QQuickAdaptorModel reset vs incremental behavior)                                                                                                 | Search broad→narrow, compare displayed ids vs installed model                                                |
| I-15 | `SetupWizard.qml:13-15` + steps                          | 72         | Fixed 660×560 wizard: StepRuntime/StepLaunch sit within measurement error of clipping bottom rows with Russian locale + builds list + memory plaque                                                                                                | Russian locale + installed runtime + big model; log step implicit heights                                    |

---

## 4. Phase 1 lint results & verified false positives

### C++ linter — 229 findings across 132 files (grouped)

| Rule                                                          | Count | Verdict after agent verification                                                                                                                                                                           |
| ------------------------------------------------------------- | ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ERR-9 NAM without sslErrors handler                           | ~42   | **False positive** — no `ignoreSslErrors`/`setPeerVerifyMode` anywhere; localhost traffic is http; GitHub/HF https keeps Qt's secure default (abort on error). Adding a handler would only weaken security |
| TMO-1 int timeout params                                      | 60    | False positive — all flow into `QTimer::start`/`setTransferTimeout`/`waitFor*`; idiomatic. (One real timeout gap exists but it's the *missing* one: D-C-05)                                                |
| ERR-1 unchecked `QFile::open`                                 | ~30   | Mostly test-helper code (acceptable); one real path: `src/models/OcrModel.cpp:23`                                                                                                                          |
| VAR-3 brace-init style                                        | 20    | Accepted project style                                                                                                                                                                                     |
| ENM-2 unscoped enums                                          | 8     | Verified safe (in-process role/state enums, no persistence as raw ints); adding `: int` is harmless hardening                                                                                              |
| DEP-* (QPair/qMin/QChar/count/currentDateTime/QScopedPointer) | ~35   | Style only; all verified no-behavior-impact                                                                                                                                                                |
| ERR-3 read-before-error-check                                 | 8     | Verified benign at every site (non-2xx branches discard payload / parse errors surfaced)                                                                                                                   |
| ERR-5 missing setTransferTimeout                              | 3     | LlamaClient:29 & ModelCatalog:53 **FP** (manual QTimer abort); **DownloadTask:273 REAL** → D-C-05                                                                                                          |
| MDL-7 `data()` has `default:`                                 | 5     | False positive — every declared role handled; `default:` required for foreign roles (DisplayRole etc.)                                                                                                     |
| ERR-6 arg() mismatch                                          | 5     | False positive — single two-arg `.arg(a, b)` overload calls                                                                                                                                                |
| LCY-5 unbounded growth                                        | 6     | DownloadManager `m_tasks` **REAL** → D-C-06; LlamaServerProcess log **FP** (5 MB rotation + 2000-line ring); tests negligible                                                                              |
| LCY-1 reply without deleteLater                               | 2     | False positive — both handlers deleteLater correctly                                                                                                                                                       |
| PAT-11 regex in loop                                          | 1     | False positive — `static const` inside loop body                                                                                                                                                           |
| HDR-3 / INC-2 / ERR-2 / ERR-4 / DEP-10                        | ~15   | Style/benign; ERR-4 http:// only in test mock URLs (intentional)                                                                                                                                           |

### QML linter — 373 findings across 30 files (grouped)

| Rule                                               | Count | Verdict                                                                                                                                   |
| -------------------------------------------------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| ORD-1 attribute ordering                           | ~180  | Style churn; zero runtime impact — deprioritize wholesale                                                                                 |
| DEL-1 model roles without required property        | ~55   | **No role mismatches anywhere** (all verified against C++ `roleNames()`); pure typing migration, folded into D-Q-18                       |
| STY-3 anchors dot notation / STY-1 missing id:root | ~45   | Style; anchor trios all verified internally consistent                                                                                    |
| PRF-1 transparent Rectangles                       | 18    | Mostly required (border-only rects; Item can't paint borders); 2 could be `background: null` (ExportDialog.qml:38, SettingsDialog.qml:51) |
| BND-2 imperative assignments                       | 17    | Mostly intentional (draft-load/reset patterns, verified per-site); **4 real cases** → D-Q-07, D-Q-08                                      |
| JS-1 `var`                                         | ~35   | Style                                                                                                                                     |
| JS-2 loose equality                                | 7     | **All false positives** — every flagged site is `!==`/`===`; the linter regex doesn't exclude them (project has zero loose equality)      |
| IMP-3 Controls without style qualifier             | 15    | False positive — Fusion set in C++ before engine creation; nothing overrides it                                                           |
| PRF-3 clip                                         | 13    | All justified (scroll containers / masking overflow — note SettingsDialog.qml:117 *masks* D-Q-22)                                         |
| STA-2 transition without from/to                   | 1     | False positive — it's the `displaced` transition for drag-reorder; catch-all is the correct idiom                                         |
| IMG-1 Image without sourceSize                     | 2     | ThumbDelegate **REAL** → D-Q-11; ImagePreview acceptable (main preview is the page)                                                       |
| LAY-2 / BND-1 / BND-3 / IMP-1                      | ~8    | ThumbDelegate.qml:51-52 real → D-Q-20; BND-1 at ModelsTab.qml:294/StepModel.qml:300 understates D-Q-04; rest style                        |

### qmllint (Phase 1b, Qt 6.10.3)

1,051 warnings: 957 `[unqualified]` + 82 `[missing-property]` + 12 `[import]`. The bulk are environment artifacts (module `LLocr` and `controller`/`uiController` registered from C++, invisible to qmllint; the `Theme.*` "member not found" flood is a cascade from the unresolved `uiController` inside Theme.qml — all properties verified to exist). The two genuine catches were **both real bugs**: `root.liveDot`/`liveFlash` (→ D-Q-01) and `sb.up.enabled` (→ D-Q-02). *Note*: a `qmlRegisterSingletonInstance`-based setup can't produce qmltypes for qmllint without a build-time module; registering `controller`/`uiController` as singletons (D-Q-23) would make future qmllint runs meaningful.

### Verified clean (checked explicitly, no findings)

Model protocol: all five `data()` switches cover their roles; begin/end pairs balanced; `movePage` does the QTBUG-classical `beginMoveRows` destination shift correctly; marker roles match delegates. Naming/API: no `get` prefixes, no `return std::move`, no `QList<QString>`, every WRITE property has a firing NOTIFY, no `Q_ASSERT` anywhere, no slicing hazards in the value types, `tr()` contexts consistent. QML: no role-name typos vs C++; drag-reorder math consistent end-to-end; model mutations correctly refused while busy (C++ and UI); no `createObject` leaks; all `Connections` use modern syntax with app-lifetime targets; `ImagePreview` box-overlay coordinate mapping is exact; license links only become anchors for `^https?://` (no scheme injection into `Qt.openUrlExternally`).

---

## 5. Fix status & remaining work plan

**Fixed (fixes 1–21, verified by build + ctest 23/23):**

1. **Data-loss/hang class**: D-C-04 (install hang), D-C-02 (footer stop), D-C-05 (download timeout), D-C-01 (restart), D-C-03 (Failed→Starting) — ✅ all fixed.
2. **User-visible breakage**: D-Q-04 (wrong model installed), D-Q-05 (memory warning), D-Q-03 (probe skipped), D-Q-01 (live dot), D-Q-02 (arrows), D-Q-09 (language switch) — ✅ all fixed.
3. **Security**: D-Q-10 (DOMPurify + CSP + `localContentCanAccessFileUrls: false`) — ✅ fixed.
4. **Crash risk**: D-C-08 (document lock) — ✅ fixed; D-C-07 — ✅ fixed in round 2.
5. **Performance**: D-C-17, D-C-18, D-C-19, D-C-20, D-C-21, D-C-22, D-Q-13 — ✅ all fixed; D-Q-11 — ✅ partially fixed (thumbnails are small now; cacheBuffer + URL keying remain). D-C-19 follow-up (fix round 2): JPEG payload reverted to PNG — b10964's mtmd decoder rejected Qt JPEG (see the regression note in §1); async encode kept, empty-payload guard added.

**Fixed (fix round 2, verified by clean rebuild + ctest 23/23 + qmllint):**

1. **Quick correctness wins**: D-C-10 (qDebug OCR dumps removed), D-C-16 (`selectedRelease` via setter), D-C-13 (`setWindowState` guard), D-C-11 (const `pageModel`/`boxModel`), D-Q-11 remainder (`cacheBuffer: 1000`) — ✅ all fixed.
2. **User-visible QML fixes**: D-Q-06 (`statusMsg` inside the ColumnLayout), D-Q-08 (revert via `Qt.binding`), D-Q-22 (ModelsTab ScrollView), D-Q-20 (`Layout.preferred*` for the 9×9 dot and StepWelcome cards), D-Q-21 (plaque `+ 2 * 8`) — ✅ all fixed.
3. **Robustness**: D-C-07 (QPointer-guarded `m_resolveCallbacks`, new `ensureConnectionReady(context, fn)` overload), D-C-06 (terminal DownloadTask eviction, cap 10, queued remove+deleteLater) — ✅ fixed. Also fixed en route: `cancelPendingStart()` resolved with the cancellation error *before* stopping the server — the synchronous `Stopping` emission used to overwrite the cancellation message with "Server stopped" (caught by `cancelPendingStartInterrupts`).
4. **Structure / investigation (fix round 2)**: D-Q-15 (step scans moved to `onVisibleChanged`; ADR-62 `refreshInstalled()` kept alive via the Models-tab toggle), D-C-14 (shared `toFiniteNumber` in the new `core/ValueParsing.h` + single `detectPlatform()`), D-Q-19 (wizard `steps` list + derived `lastStep`), D-Q-23 (`Controller`/`UiController` registered as singletons; QML renamed), D-Q-17 typos (`RequestTabNum`, `saveValues`) — ✅ fixed. **I-03, I-08, I-09 — ✅ fixed** (prepare-generation invalidation on cancel; cache-only preview probe via `RuntimeLocator::cachedProbe()`; wizard external path opens the Runtime tab).

**Fixed (fix round 3, QML-only, verified by qmllint):**

1. **Investigation fixes**: I-10 (restart banner dirty-watch now tracks `Settings.serverPath`), I-11 (preset delegates refresh on `presetsChanged` too), I-12 (ComboBox `currentIndex` re-synced on retranslate-driven model replacement — UITab ×2, RuntimeTab ×1), I-13 (log autoscroll rewritten as explicit `Flickable` + `TextArea.flickable` writing `contentY`, gated on `atYEnd`).

**Fixed (fix round 4, verified by clean rebuild + ctest 24/24 + qmllint):**

1. **D-Q-17 named enums**: `RuntimeState`/`AppBusyState` moved into `RuntimeController` as `Q_ENUM`s (QML: `Runtime.Ready`, `Runtime.StartingRuntime`, …); `RuntimeState.h` now holds only type aliases so the C++ spelling is unchanged. All 32 raw `=== N` state comparisons in QML migrated to named enum values across Footer, RuntimeTabInternal, ModelsTab, StepModel, StepRuntime. `ModelInstaller`/`RuntimeInstaller` state enums already had `Q_ENUM` and are now used by name in QML. En route: `RuntimeController.h` dropped the unused `<QQmlEngine>` include and inert `QML_ELEMENT`/`QML_SINGLETON` macros (registration is manual) — the alias header would otherwise drag QtQml include paths into non-QML test targets; `test_server_process.cpp` compares these enums via `int(...)` casts so QTest's enum formatting (`QMetaEnum::fromType`) does not demand the moc symbol in a target that doesn't compile `RuntimeController.cpp`. Behavior change (intentional, I-09 precedent): the Footer badge click in Managed mode with a not-configured runtime now opens the **Runtime** tab (`RuntimeTabNum`), not Launch (`4`).

**Fixed (fix round 8, I-04/I-05 without ASan; verified by build + ctest 25/25):**

1. **I-04 restart-window honesty**: a crashed Ready server now flips to `Starting` + "Server crashed — restarting…" synchronously with scheduling the respawn — the 500 ms window no longer offers a stale `Ready` to `ensureConnectionReady` (which would hit a dead port). Regression-tested via the mock `--crash-after` harness.
2. **I-05 self-contained recognize chain**: `buildRequestBody`/`parseResponse` made static (no overrides existed), per-request `shared_ptr<LlamaClient>` shared between the chain and `abort()`, zero `this` captures — `OcrModel` may die mid-request safely. Chain hardening en route: watcher `connect()` moved *before* `setFuture()` (the "likely race" QWARN was real for the fast-encode path) and the client captured through the whole network phase (otherwise `~QNetworkAccessManager` — triggered by the last shared_ptr dying with handler 1 — killed the in-flight reply and surfaced as an empty `errorMessage`). New regression test `futureCompletesAfterModelDestruction` deletes the model right after `recognize()` and requires the future to still deliver the parsed result.

**Fixed (fix round 7, hardening; verified by clean rebuild + ctest 25/25 + qmllint):**

1. **I-01**: `PageListModel::setCurrent()` range guard (`[-1, rowCount)`, -1 stays the "no current" sentinel) + `setCurrentGuardsRange` regression test.
2. **I-02**: `QAbstractItemModelTester` (Fatal) around the remaining list models — Launch/Request draft models (5+5 tests), `DownloadManager` (10 tests), and a new `tests/test_box_model.cpp` target covering `BoxListModel` (roles/reset, updateBoxRect dataChanged, removeBox + boxRemoved, setFromResult).
3. **I-06**: main.cpp declaration order — `AppController`/`UiController` moved above `QQmlApplicationEngine`, so the engine-owned image provider never outlives the controller it points to.
4. **I-07**: defensive `~DownloadTask()` (disconnect + abort + `deleteLater` on an in-flight reply) — a directly-destroyed running task can no longer leak the reply or receive signals into a half-destroyed object.

**Fixed (fix round 6, verified by build + ctest 24/24 + qmllint):**

1. **D-Q-18 `pragma ComponentBehavior: Bound`**: added to all 35 QML files (Theme.qml singleton excepted — no components inside). Delegate migrations: `ThumbDelegate` (required `index`/`pageIndex`/`current`/`recognized`/`edited`/`hasDuplicates` matching PageListModel roleNames, all `model.*` accesses qualified), LaunchTab/RequestTab parameter rows (required `index`/`name`/`valueText`/`description` + delegate id), StepWelcome choice cards (required `index`). The other delegates were already Bound-ready (required properties in ImagePreview box/handle delegates and the four Common lists). All cross-document id reaches replaced with explicit contracts: Header → signals handled in Main.qml; ExportDialog → `exportRequested(scope, from, to)` signal handled in Main.qml; ThumbDelegate receives `listView` from ThumbPanel; RuntimeTab/RuntimeTabInternal receive `setupWizardRef`/`logWindowRef` from SettingsDialog (a fourth reach — `ExportDialog.qml:55-58` writing `exportDialog.*` — found and fixed en route). qmllint: zero errors; cross-document [unqualified] count dropped 826 → 815; remaining warnings are the pre-existing runtime-singleton noise classes.

**User-reported regression from round 4 (D-Q-17), fixed:** the Footer Start/Stop button's `enabled` expression lost its `||` during the enum migration (`serverActive && (configValid && …)` instead of `serverActive || (configValid && …)`) — the button stayed disabled while the server was stopped even with a fully valid config (recognition auto-start worked, exposing the inconsistency). Restored; caught only by manual testing (qmllint/tests do not see QML boolean semantics).

**Fixed (fix round 5, verified by build + ctest 24/24 + qmllint):**

1. **D-C-15 shared download aggregation**: new `runtime/DownloadGroup` owns the install download accounting (counters, the sync-fail post-enqueue re-check from D-C-04, byte-progress aggregation, single `allFinished` per group); `RuntimeInstaller`/`ModelInstaller` keep only their state transitions. Registered in `src/CMakeLists.txt` and in the `test_model_installer`/`test_install_lock` source lists. The generic async-step wrapper was declined — the two async flows differ in guard strategy (generation counter vs `.then` chain shape), a wrapper would be a forced abstraction.
2. **D-Q-16 shared installer components**: `Common/RuntimeBuildsList`, `Common/ModelInstalledList`, `Common/ModelPresetList`, `Common/HfSearchList`, `Common/InstallerProgressRow`, `Common/InstallerStatusLabel` replace the four duplicated installer blocks in RuntimeTabInternal/StepRuntime/ModelsTab/StepModel; all 10 `Connections`-rebind glue blocks are gone (refresh helpers live inside the components). Reconciled divergences: backendDisplay fallback now everywhere; Activate is busy-gated everywhere; preset title column unified at 150 px. License/update dialogs stay consumer-side (they genuinely differ).
3. **User-reported (pre-existing): stale update plaque** — after a successful update install (or a manual build activation) `hasUpdate` stayed stale because it was only recomputed in `onCatalogLoaded`; the "A newer build is available" plaque never cleared. Now extracted as `RuntimeInstaller::recomputeHasUpdate()` and called after every commit point: catalog load, install commit (`onInstallFinished`) and `activateBuild` — so installing the newest release clears the plaque, and manually activating an older build honestly brings it back.

**Remaining queue (suggested order):**

1. **Structure (bigger refactors, schedule deliberately):**
   - (empty — D-Q-18 done in round 6)
2. **ASan / verify-only:** ~~I-04/I-05~~ — ✅ fixed in round 8 without ASan (I-04: state-transition fix + mock-server regression test; I-05: self-contained async chain — the "larger refactor" the review feared turned out to be static-ifying two member functions and a per-request client). Remaining verify-only: I-14 (ModelsTab search-vs-installed staleness), I-15 (wizard clipping in Russian locale) — manual recipes in §3.
3. **Optional leftovers:** D-Q-11 per-page URL keying (thumbnails are small now); D-Q-12 (preview sourceSize) and D-Q-14 (sticky Loader) if preview perf is still noticeable after the structural fixes.

*Note on rebuilds: localized MSVC breaks `/showIncludes` dependency tracking — after changing a header, delete the affected `.obj` files (or rebuild the target from clean) before linking.*
