# 03. Architecture

## Layers
```
┌─────────────────────────────────────────────────────────┐
│                     QML UI (View)                        │
│  Main.qml · SettingsDialog.qml (tabbed)                  │
├─────────────────────────────────────────────────────────┤
│                C++ Backend (ViewModel)                   │
│  AppController — state, signals/slots, orchestration     │
│  DocumentModel · PageListModel · BoxListModel            │
│  OcrImageProvider (QQuickImageProvider) · SettingsStore  │
├──────────────┬──────────────┬────────────┬──────────────┤
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
  server, hosted APIs). **This is the only supported connection type.**
  Async via `QPromise`; supports **`abort()`** so the UI Stop button can cancel
  an in-flight request. Per-request timeout via `QTimer`.

> **Concept change:** the `LlamaCppProvider` idea and the JSON **model-profile**
> system were dropped. All model/connection/parser configuration now lives in
> **`ProviderConfig`**, edited in the Settings dialog and persisted by
> `SettingsStore`.

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
    int     bboxCoordinateRange;  // raw coord scale for positional parsers
};
```

- `SettingsStore` loads/saves this via `QSettings` (grouped keys:
  `provider/*`, `model/*`, `output/*`).
- `parserId` selects the response-parsing strategy via `ParserFactory`.

## Backend building blocks (implemented)
- **AppController** — the ViewModel. Exposes `busy`, `resultText`,
  `statusMessage`, `hasImage`, `pageCount`, `currentPage`, `hasResult`,
  `canRecognize`, `parserNames`, the settings getters (`baseUrl`, `apiKey`,
  `timeoutMs`, `modelName`, `prompt`, `temperature`, `maxTokens`, `parserId`,
  `bboxCoordinateRange`), and the `pageModel` / `boxModel` list models to QML.
  Owns the recognition run loop (single page / "recognize all"), the **stop**
  flag, per-page edits, and **export**. Settings are applied in one call:
  `applySettings(QVariantMap)`.
- **DocumentModel** — holds the loaded pages (image + per-page `OcrResult` +
  `recognized` flag); loads images (`QImage`) and PDFs (`QPdfDocument`).
- **PageListModel** — feeds the left thumbnail strip: page index, recognized
  flag, edited flag, current-page highlight. Deliberately carries **no** boxes.
- **BoxListModel** — normalized bbox rectangles for the current page's overlay.
- **OcrImageProvider** — a `QQuickImageProvider` serving both the full current
  page (`image://ocr/current`) and per-page thumbnails (`image://ocr/page/N`).
- **SettingsStore** — persists the full `ProviderConfig`.

## Data flow (OCR)
```
UI (file selection)
  → AppController (builds OcrRequest from ProviderConfig)
    → DocumentModel (decode image / render PDF page → QImage)
    → OpenAiProvider.recognize()  [async, cancellable]
    → OutputParser (from settings: raw / det_tokens)
  → OcrResult (text + optional normalized boxes)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
