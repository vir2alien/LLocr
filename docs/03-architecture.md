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
│ LLM Provider │  Image/PDF   │  Parsers   │  Exporter     │
│ ILlmProvider │  Loader      │ IOutputParser │ (TXT/MD/HTML)│
│ OpenAiProvider│ DocumentModel│ raw/det    │ DOCX/PDF      │
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
- The backend orchestrates: receives a request from the UI → calls the provider → parses → returns the result.
- Everything is asynchronous (QFuture / QPromise / signals); the UI does not block.
  Page navigation stays responsive **while a recognition run is in progress**.

## Key abstraction: providers
```cpp
// Interface for a connection provider to an LLM
class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;
    virtual QFuture<OcrResult> recognize(const OcrRequest& request,
                                         const ProviderConfig& config) = 0;
    virtual QString name() const = 0;
};
```

The request and transport config are split:

```cpp
struct OcrRequest {
    QImage image;
    QString prompt;
    QString modelId;
    double temperature = 0.0;
    int    maxTokens = 8192;
    double dryMultiplier = 0.8;
    double dryBase = 1.75;
    int    dryAllowedLength = 35;
    int    dryPenaltyLastN = 2048;
};

struct ProviderConfig {   // transport only
    QString baseUrl;      // e.g. http://localhost:8080
    QString apiKey;       // optional bearer token
    int     timeoutMs;    // per-request timeout
};
```

Implementations:
- `OpenAiProvider`  — any OpenAI-compatible API (Ollama, LM Studio, llama.cpp
  server, hosted APIs). Transport-only. Async via `QPromise`; supports
  **`abort()`** so the UI Stop button can cancel an in-flight request.
  Per-request timeout via `QTimer`.

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
ensureConnectionReady() → ResolvedConnection → ProviderConfig → OcrRequest → OpenAiProvider
```

## Configuration model
The model + parser settings live in `SettingsStore` (persisted via `QSettings`,
edited in the Settings dialog). When recognition starts, `RecognitionController`
assembles them into an `OcrRequest` (model + prompt + generation params) and a
`ProviderConfig` (connection transport) from the store.

- `SettingsStore` loads/saves connection/model/parser/UI settings via
  `QSettings` (grouped keys: `provider/*`, `model/*`, `output/*`, `ui/*`,
  `runtime/*`, `launch/*`, `hf/*`). Model settings include the **DRY sampling
  parameters** (`model/dryMultiplier`, `model/dryBase`, `model/dryAllowedLength`,
  `model/dryPenaltyLastN`). The recognition **prompt** is supplied by the chosen
  **model preset** (a `ModelPreset.prompt`); a built-in default
  (`AppController::m_prompt`, "document parsing.") applies when no preset is used.
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
  "recognize all"), the **stop** flag, and the `OpenAiProvider` instance.
  Obtains the connection **only** via `RuntimeController::ensureConnectionReady()`
  (ADR 37), builds `OcrRequest` + `ProviderConfig` from the resolved connection,
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
- **Exporter** — turns pages into TXT / MD / HTML directly, DOCX / PDF via
  Pandoc (with a `QPdfWriter` PDF fallback); resolves `image://ocr/crop/*`
  references to real files on export.
- **WindowSettings** (QML) — persists window position/size/visibility.

## Data flow (OCR)
```
UI (file selection)
  → AppController (stores pages in DocumentModel)
    → RecognitionController
        → RuntimeController::ensureConnectionReady()   [External: immediate;
            Managed: start → /health → /v1/models → alias]
        → ResolvedConnection → ProviderConfig + OcrRequest
      → DocumentModel (decode image / render PDF page → QImage)
      → OpenAiProvider.recognize()  [async, cancellable]
      → OutputParser (from settings: raw / det_tokens)
    → OcrResult (text + optional normalized boxes)
  → AppController (applyRawResult → per-page OcrResult + PageEditStore)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
