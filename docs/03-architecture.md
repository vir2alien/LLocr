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
├──────────────┴──────────────┴────────────┴──────────────┤
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

## Configuration model
The model + parser settings live in `SettingsStore` (persisted via `QSettings`,
edited in the Settings dialog). When recognition starts, `RecognitionController`
assembles them into an `OcrRequest` (model + prompt + generation params) and a
`ProviderConfig` (connection transport) from the store.

- `SettingsStore` loads/saves connection/model/parser/UI settings via
  `QSettings` (grouped keys: `provider/*`, `model/*`, `output/*`, `ui/*`).
  Model settings now include the **DRY sampling parameters**
  (`model/dryMultiplier`, `model/dryBase`, `model/dryAllowedLength`,
  `model/dryPenaltyLastN`). The prompt is **not** persisted yet: it is
  hardcoded in `AppController::m_prompt`.
- `parserId` selects the response-parsing strategy via `ParserFactory`
  (`raw` | `det_tokens`; default `det_tokens`).

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
  Builds `OcrRequest` + `ProviderConfig` from `SettingsStore` and the prompt,
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
  via `QSettings`, grouped keys `provider/*`, `model/*`, `output/*`) plus UI
  state (`ui/*`: theme mode, language, window geometry). Exposed to QML as the
  `Settings` singleton; also read directly by `AppController`,
  `RecognitionController`, and `UiController`.
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
    → RecognitionController (builds OcrRequest from SettingsStore + prompt)
      → DocumentModel (decode image / render PDF page → QImage)
      → OpenAiProvider.recognize()  [async, cancellable]
      → OutputParser (from settings: raw / det_tokens)
    → OcrResult (text + optional normalized boxes)
  → AppController (applyRawResult → per-page OcrResult + PageEditStore)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
