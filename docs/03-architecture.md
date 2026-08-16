# 03. Architecture

## Layers
```
┌──────────────────────────────────────────────────────────┐
│                     QML UI (View)                        │
│  Main.qml · SettingsDialog.qml (tabbed) · WindowSettings │
├──────────────────────────────────────────────────────────┤
│                C++ Backend (ViewModel)                   │
│  AppController — state, signals/slots, orchestration     │
│  DocumentModel · PageListModel · BoxListModel            │
│  OcrImageProvider (QQuickImageProvider) · SettingsStore  │
│  UiController (theme)                                    │
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
    virtual QFuture<OcrResult> recognize(const OcrRequest& req) = 0;
    virtual QString name() const = 0;
};
```

Implementations:
- `OpenAiProvider`  — any OpenAI-compatible API (Ollama, LM Studio, llama.cpp
  server, hosted APIs).
  Async via `QPromise`; supports **`abort()`** so the UI Stop button can cancel
  an in-flight request. Per-request timeout via `QTimer`.

## Configuration model
Everything the app needs to talk to a model is a single struct:

```cpp
struct ProviderConfig {
    // Connection
    QString baseUrl;      // e.g. http://localhost:8080
    QString apiKey;       // optional bearer token
    int     timeoutMs;    // per-request timeout

    // Model
    QString modelName;    // sent as "model"
    QString prompt;       // task instruction
    double  temperature;
    int     maxTokens;

    // Output / parser
    QString parserId;             // "raw" | "det_tokens"
    // int  bboxCoordinateRange;  // (commented out; not used by the app yet)
};
```

- `SettingsStore` loads/saves the connection/model/parser settings via
  `QSettings` (grouped keys: `provider/*`, `model/*`, `output/*`). The prompt is
  **not** persisted yet: it is hardcoded in `AppController::m_prompt`.
- `parserId` selects the response-parsing strategy via `ParserFactory`.

## Backend building blocks (implemented)
- **AppController** — the ViewModel. Exposes `busy`, `resultText`,
  `statusMessage`, `hasImage`, `pageCount`, `currentPage`, `hasResult`,
  `currentPageEditable`, `currentPageEdited`, `pandocAvailable`,
  `exportNameFilters`, `canRecognize`, `parserNames`, `prompt`, and the
  `pageModel` / `boxModel` list models to QML. Owns the recognition run loop
  (single page / "recognize all"), the **stop** flag, per-page edits, and
  **export**. All connection/model/parser settings live in `SettingsStore`
  (exposed to QML as the `Settings` singleton), not on the controller.
- **DocumentModel** — holds the loaded pages (image + per-page `OcrResult` +
  `recognized` flag); loads images (`QImage`) and PDFs (`QPdfDocument`).
- **PageListModel** — feeds the left thumbnail strip: page index, recognized
  flag, edited flag, current-page highlight. Deliberately carries **no** boxes.
- **BoxListModel** — normalized bbox rectangles for the current page's overlay.
- **OcrImageProvider** — a `QQuickImageProvider` serving both the full current
  page (`image://ocr/current`) and per-page thumbnails (`image://ocr/page/N`).
- **SettingsStore** — persists the settings (connection/model/parser via
  `QSettings`, grouped keys `provider/*`, `model/*`, `output/*`) plus UI state
  (`ui/*`: theme mode, window geometry). Exposed to QML as the `Settings`
  singleton; also read directly by `AppController` and `UiController`.
- **UiController** — System / Light / Dark theme handling (`QML_ELEMENT`).
- **WindowSettings** (QML) — persists window position/size/visibility.

## Data flow (OCR)
```
UI (file selection)
  → AppController (builds OcrRequest from ProviderConfig + SettingsStore)
    → DocumentModel (decode image / render PDF page → QImage)
    → OpenAiProvider.recognize()  [async, cancellable]
    → OutputParser (from settings: raw / det_tokens)
  → OcrResult (text + optional normalized boxes)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
