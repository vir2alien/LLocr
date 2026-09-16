# 03. Architecture

## Layers
```
┌──────────────────────────────────────────────────────────┐
│                     QML UI (View)                        │
│  Main.qml · SettingsDialog.qml (tabbed) · ExportDialog   │
│  MainWindow/* (Header, ThumbPanel, ImagePanel,           │
│  WorkPanel, MarkdownPreview, Footer) · WindowSettings    │
├──────────────────────────────────────────────────────────┤
│                C++ Backend (ViewModel)                   │
│  AppController — state, signals/slots, orchestration     │
│  RecognitionController — run loop, stop/abort            │
│  DocumentModel · PageListModel · BoxListModel            │
│  PageEditStore — per-page edits                          │
│  OcrImageProvider (QQuickImageProvider) · SettingsStore  │
│  UiController (theme) · I18n (language/retranslate)      │
├──────────────┬──────────────┬────────────┬───────────────┤
│  OCR models  │  Image/PDF   │  Parsers   │  Exporter     │
│  OcrModel    │  Loader      │ IOutputParser │ (TXT/MD/HTML)│
│  Unlimited…  │ DocumentModel│ raw/det    │ DOCX/PDF      │
│  LlamaClient │              │            │               │
├──────────────┴──────┬───────┴────┬───────┴───────────────┤
│   Managed Runtime (local llama.cpp)   │   internal infra  │
│   RuntimeController — facade + resolve│   DownloadTask    │
│   RuntimeLocator · RuntimePaths       │   DownloadManager │
│   ServerCapabilities· ServerLaunchConfig│  ArchiveExtractor│
│   LlamaServerProcess · ProcessGuard   │   InstallTransaction│
│   SingleInstanceGuard                 │   ReleaseCatalog  │
│   RuntimeInstaller (stage D install)  │   ModelCatalog    │
│   ModelInstaller · ModelRegistry      │   ModelPresetCatalog│
│   ModelMemoryEstimator (H.2)          │   ResolvedConnection│
├────────────────────────────────────────┴──────────────────┤
│        RAG Service (external HTTP service) — later        │
└──────────────────────────────────────────────────────────┘
```

## Principles
- **The UI does not work directly** with network, files, or parsing — only via the backend.
- The backend orchestrates: receives a request from the UI → resolves the OCR
  model adapter → sends the request via the transport → parses → returns the result.
- Everything is asynchronous (QFuture / QPromise / signals); the UI does not block.
  Page navigation stays responsive **while a recognition run is in progress**.

## Key abstraction: OCR models (adapters)
Each supported OCR LLM gets its own adapter class; the shared mechanics live in
the abstract base:

```cpp
class OcrModel {                      // src/models/OcrModel.h — abstract base
public:
    virtual QString id() const = 0;               // "unlimited-ocr"
    virtual QString displayName() const = 0;      // "Unlimited-OCR"
    virtual QList<OcrPromptVariant> promptVariants() const = 0;
    virtual QString defaultParserId() const = 0;  // "det_tokens"

    QFuture<OcrResult> recognize(const OcrRequest &request,
                                 const ConnectionConfig &config);
    void abort();
protected:
    virtual QByteArray buildRequestBody(const OcrRequest &, const QString &imageDataUrl) const;
    virtual OcrResult parseResponse(const QByteArray &responseData) const;
};
```

The request and transport config are split:

```cpp
struct OcrRequest {                   // core/OcrRequest.h
    QImage image;
    QString prompt;
    QString modelId;
    QList<RequestParameter> parameters;   // ordered body parameters (request profile)
};

struct ConnectionConfig {             // core/ConnectionConfig.h — transport only
    QString baseUrl;      // e.g. http://localhost:8080
    QString apiKey;       // optional bearer token
    int     timeoutMs;    // per-request timeout
};
```

Implementations:
- `OcrModel` (base) implements `recognize()`/`abort()` as a template method over
  the transport: encodes the image to a base64 data URL, builds the request body
  (`buildRequestBody`, virtual), POSTs via `LlamaClient`, and parses the response
  (`parseResponse`, virtual). Async via `QPromise`; per-request timeout;
  **`abort()`** so the UI Stop button can cancel an in-flight request.
- `UnlimitedOcrModel` — one prompt variant ("document parsing."), parser
  `det_tokens`. Adding another LLM = one new subclass + one line in
  `OcrModelFactory`.
- `LlamaClient` (`core/LlamaClient.h`) — thin transport: joins the
  `/v1/chat/completions` URL, POSTs JSON with Bearer auth/timeout/`abort()`, and
  extracts the server's error message.

## Key abstraction: resolved connection
`RecognitionController` **never** deals with modes, processes, or health
checks. It asks one async facade and receives a ready-to-use connection:

```cpp
struct ResolvedConnection {   // src/runtime/ResolvedConnection.h
    QString baseUrl;    // http://127.0.0.1:<port> (Managed) or external URL
    QString apiKey;     // from settings (External) or empty (Managed)
    QString modelId;    // alias (Managed) or model/name (External)
    int     timeoutMs;
};

class RuntimeController : public QObject {   // src/runtime/RuntimeController.h
    // Callback-based resolve (review 3.3): External invokes synchronously;
    // Managed start → /health → /v1/models → alias, then onResolved() runs.
    // Concurrent callers register their callback and share one in-flight resolve.
    void ensureConnectionReady(std::function<void(const ResolvedConnection &)> onResolved);
    void cancelPendingStart();   // Stop during StartingRuntime
};
```

The wizard “Check” button (self-test: start → health → `/v1/models` → one OCR
request) is exposed via the dedicated `SelfTestController` singleton
(`SelfTest`), which consumes `ensureConnectionReady()`; the live server log
window binds to the `RuntimeLog` singleton.

ARM-coordination principle: *all* async work that decides “is a connection
ready, and what is it” is owned by `RuntimeController` (ADR 26/32/37). The
recognition flow is then a pure pipeline:

```
ensureConnectionReady() → ResolvedConnection → ConnectionConfig → OcrRequest → OcrModel (via LlamaClient)
```

## Configuration model
The model + parser settings live in `SettingsStore` (persisted via `QSettings`,
edited in the Settings dialog). When recognition starts, `RecognitionController`
assembles an `OcrRequest` (prompt from the active OCR model adapter + the
request-body parameters of that model's request profile, ADR 59) and a
`ConnectionConfig` (connection transport) from the resolved connection and the
store.

- `SettingsStore` loads/saves connection/model/parser/UI settings via
  `QSettings` (grouped keys: `provider/*`, `model/*`, `parser/*`, `ui/*`,
  `runtime/*`, `launch/*`, `hf/*`; the `provider/` prefix is legacy storage,
  kept for profile compatibility). `model/recipeId` selects the OCR model
  adapter via `OcrModelFactory` (default `unlimited-ocr`). The recognition
  **prompt** comes from the selected adapter's `promptVariants()` — the
  authority moved from `AppController`/presets to the model adapter (ADR 58).
- `parserId` selects the response-parsing strategy via `ParserFactory`
  (`raw` | `det_tokens`; default `det_tokens`).
- In `Managed` mode the *connection* is **computed**, not configured: the
  managed server's `baseUrl` (loopback + chosen port), `modelId` (the `--alias`)
  come from `ensureConnectionReady()` (ADR 32). `model/name` is not overwritten.

## Managed runtime layer (stages A–G)
A dedicated `src/runtime/` layer sits between the backend and the OS:

- **RuntimeController** (facade, singleton instance created in `main.cpp`,
  ADR 36) — owns `ensureConnectionReady()` (External immediate resolve;
  Managed: start → `/health` → `/v1/models` → alias), `cancelPendingStart()`,
  server lifecycle (start/stop/restart), `estimateModelMemory()` (H.2), and
  exposes `state`/`busyState`/`statusMessage`/`loadProgressPercent`/`configValid`/
  `lockedOut` to QML. Self-test (`SelfTest` singleton) and the server-log live
  view (`RuntimeLog` singleton) were extracted into their own QML singletons
  (review 3.4).
- **RuntimeLocator** — probe (`--version`/`--help`, tolerant version parse),
  `autoDiscover()`, `probeCached()` (LRU, H.7). **ServerCapabilities** — build
  allowlist + `--help` parse (ADR 41). **ServerLaunchConfig** — argv builder +
  `toDisplayCommand()` (secrets never shown).
- **LlamaServerProcess** — `QProcess` argv-only, ring-buffer log (2000 lines) +
  rotating file log (5 MB × 3), health polling, crash → auto-restart ≤3×/5 min,
  stop via terminate→kill. **ProcessGuard** — platform no-orphan binding
  (Job Object / `PDEATHSIG` / macOS best-effort + `owner.json`, ADR 30).
- **RuntimeInstaller** (stage D) — `ReleaseCatalog` (GitHub Releases + `sha256`
  from body, TTL 6 h), `detectPlatform()`/backend recommendation,
  hardened `ArchiveExtractor` (ZIP + `.tar.gz` via zlib; anti-bomb/zip-slip,
  ADR 34), transactional `InstallTransaction` (staging → verify → probe →
  atomic rename → commit, ADR 39). QML singleton `RuntimeInstaller`.
- **ModelInstaller** (stage E) — `ModelCatalog` (HF tree w/ pagination,
  commit-`sha` pinning, mmproj/multi-part, ADR 29), `ModelPresetCatalog`
  (built-in `:/models/default-presets.json` + user `models/catalog.json`, ADR 42),
  `ModelRegistry` (index.json, managed vs external, removal guards).
  QML singleton `ModelInstaller`.
- **DownloadTask/DownloadManager** — resumable downloads (`.part`+`.part.meta`,
  `Range`/`If-Range`, streaming `sha256`), ≤2 parallel, `.part` lives next to
  the target (ADR 40); redirects https-only with `Authorization` dropped on
  host change (ADR 45).
- **SingleInstanceGuard** — `.instance.lock` (runtime-owner); when another
  instance holds it, Managed server ops are blocked, External keeps working
  (ADR 26). Install exclusivity is guarded separately by `.install.lock` inside
  the `RuntimeInstaller` pipeline, and model-index writes by `.registry.lock`
  (ADR 46) — so the 2nd instance can install while the 1st uses External.
- **ModelMemoryEstimator** — GGUF size + KV-cache estimate (H.2), used by the
  wizard's Launch step to warn about RAM.

State: `AppBusyState` (`Idle`/`StartingRuntime`/`Recognizing`/`StoppingRuntime`/
`Downloading`/`Installing`) and `RuntimeState` (`NotConfigured → Stopped →
Starting → Ready → Stopping → Stopped`, plus `Failed`) are exposed to QML;
`canRecognize` per §1.4 of the local-runtime plan (`docs/09-local-runtime-plan.md`).

## Backend building blocks (implemented)
- **AppController** — the ViewModel. Exposes `busy`, `resultText`,
  `statusMessage`, `hasImage`, `pageCount`, `currentPage`, `hasResult`,
  `imageRevision`, `docRevision`, `currentPageEditable`, `currentPageEdited`,
  `exportNameFilters`, `canRecognize`, `parserNames`, `prompt`, and the
  `pageModel` / `boxModel` list models to QML. Owns page lifecycle
  (open/append/remove/reorder), per-page edits, image-block editing, and
  **export**. Delegates the recognition run loop to `RecognitionController`.
  All connection/model/parser settings live in `SettingsStore` (exposed to QML
  as the `Settings` singleton), not on the controller.
- **RecognitionController** — owns the recognition run loop (single page /
  "recognize all"), the **stop** flag, and the active `OcrModel` instance
  (resolved via `OcrModelFactory` from `model/recipeId` at run start).
  Obtains the connection **only** via `RuntimeController::ensureConnectionReady()`
  (ADR 37), builds `OcrRequest` + `ConnectionConfig` from the resolved connection,
  runs sequentially through pages, and emits `rawResultReady` per page.
- **DocumentModel** — holds the loaded pages (`DocumentPage`: image +
  per-page `OcrResult` + `recognized` flag); loads single/multiple images
  (`QImage`) and PDFs (`QPdfDocument`), and supports append/remove/reorder.
- **PageListModel** — feeds the left thumbnail strip: page index, recognized
  flag, edited flag, duplicate flag, current-page highlight. Deliberately
  carries **no** boxes.
- **BoxListModel** — normalized bbox rectangles for the current page's overlay;
  exposes `updateBoxRect` / `removeBox` / `isImageBox` for image-block editing.
- **PageEditStore** — per-page user edits keyed by page index; computes the
  "effective" text (edit overrides recognition), handles revert and remapping
  after page removal/reordering.
- **OcrImageProvider** — a `QQuickImageProvider` serving the full current page
  (`image://ocr/current`), per-page thumbnails (`image://ocr/page/N`), and
  cropped image blocks (`image://ocr/crop/N`).
- **SettingsStore** — persists settings (connection/model incl. DRY params/parser
  via `QSettings`, grouped keys `provider/*`, `model/*`, `output/*`, `runtime/*`,
  `launch/*`, `hf/*`) plus UI state (`ui/*`: theme mode, language, window
  geometry). Exposed to QML as the `Settings` singleton; also read directly by
  `AppController`, `RecognitionController`, `RuntimeController`, and
  `UiController`.
- **UiController** — System / Light / Dark theme handling (`QML_ELEMENT`).
- **I18n** — runtime language switching via Qt Linguist (`qsTr`/`tr` +
  `.ts`); installs translators, emits `languageApplied` for `engine.retranslate()`.
- **Exporter** — turns pages into TXT / MD directly, assembles the standalone
  HTML file from the rendered sections, keeps the DOCX (Pandoc) and PDF
  (`QPdfWriter`) fallback writers, and resolves `image://ocr/crop/*`
  references (files for direct writers, `data:` URLs for the render path).
- **ExportRenderer** — headless `QWebEnginePage` running the preview bundle
  (`qrc:/preview/export.html`: marked + DOMPurify + KaTeX) to produce the
  formatted HTML sections for export and to print PDF via `printToPdf`;
  UI-thread only, callback-based (ADR 63).
- **WindowSettings** (QML) — persists window position/size/visibility.

## Data flow (OCR)
```
UI (file selection)
  → AppController (stores pages in DocumentModel)
    → RecognitionController
        → RuntimeController::ensureConnectionReady()   [External: immediate;
            Managed: start → /health → /v1/models → alias]
        → OcrModelFactory::create(model/recipeId)
        → ResolvedConnection → ConnectionConfig + OcrRequest
      → DocumentModel (decode image / render PDF page → QImage)
      → OcrModel.recognize() → LlamaClient.postJson()  [async, cancellable]
      → OutputParser (from settings: raw / det_tokens)
    → OcrResult (text + optional normalized boxes)
  → AppController (applyRawResult → per-page OcrResult + PageEditStore)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
