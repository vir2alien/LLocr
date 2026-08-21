# 07. Glossary and Adopted Decisions

## Terms
- **OCR** — recognizing text from images.
- **LLM provider** — the connection to the model; here always an
  **OpenAI-compatible** endpoint (llama.cpp server, Ollama, LM Studio, hosted API).
- **ProviderConfig** — the **transport** settings struct (base URL, API key,
  timeout). It is separate from the model settings: when a recognition starts,
  `RecognitionController` assembles an `OcrRequest` (model + prompt + generation
  params) from `SettingsStore` and a `ProviderConfig` (connection) for the
  provider.
- **OcrRequest** — the per-request payload for the provider: `image`, `prompt`,
  `modelId`, plus generation params (`temperature`, `maxTokens`, and the DRY
  sampling parameters). Defined in `providers/ILlmProvider.h`.
- **DRY sampling parameters** — llama.cpp's "Don't Repeat Yourself" sampler
  settings (`dry_multiplier`, `dry_base`, `dry_allowed_length`,
  `dry_penalty_last_n`), exposed in **Settings → Model** and sent in the request
  body. Tuned for the Unlimited-OCR model.
- **Output parser** — the strategy for parsing a model's response into a
  structured result (`raw`, `det_tokens`; default `det_tokens`), selected in
  Settings → Output.
- **BlockStyle** — mapping from model block labels (`title`, `image`, `chart`,
  `equation`, `table`, `ref_text`, captions, …) to Markdown rendering styles
  (`BlockStyle.h`).
- **bbox** — bounding box coordinates of a recognized fragment for overlay on
  the preview (produced by the `det_tokens` parser).
- **Image/chart block** — a `det_tokens` box labeled `image` or `chart`;
  editable on the preview (move / resize / delete) and exported as a cropped
  image.
- **Markdown preview** — client-side render of the current page's Markdown via
  Qt WebEngine + marked + KaTeX, toggled in the right text pane.
- **RAG** — Retrieval-Augmented Generation; here — indexing scans into a vector DB.
- **Document / page model** — `DocumentModel` holds pages; `PageListModel` feeds
  the thumbnail strip (recognized/edited/duplicate/current flags, no boxes).
- **Recognized marker** — per-page indicator showing whether a page has been
  OCR'd yet; edited pages and pages with duplicate boxes carry additional
  markers.

## Adopted decisions (ADR-lite)
| #    | Decision                                                     | Reason                                                       |
| ---- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 1    | Qt6/C++/QML stack                                            | Existing skill, covers all GUI requirements                  |
| 2    | CMake instead of qmake                                       | Standard for cross-platform C++                              |
| 3    | RAG as a separate Python service                             | Python's ecosystem is stronger for ML/RAG                    |
| 4    | Build the OpenAI-compatible provider first                   | Supported by almost all local runners                        |
| 5    | Internal format — Markdown, export via Pandoc                | Cheap support for many formats                               |
| 6    | RAG deferred, only interface design sketched               | Working OCR first. Backend RAG interface stub not implemented yet — see Stage 4.
| 7    | PDF via Qt PDF (`QPdfDocument`), not MuPDF/Poppler           | No extra native dependency; bundled with Qt6; adequate at ~150 DPI. |
| 8    | Recognition is cancellable (`abort()`)                       | Users must be able to Stop long multi-page runs.             |
| 9    | Navigation stays live during recognition                     | Users browse other pages while a batch run proceeds.         |
| 10   | Export written directly (TXT/MD/HTML); Markdown-as-source + Pandoc for DOCX/PDF | Ship useful export now; converge on the single-source pipeline. |
| 11   | Thumbnails carry no bounding boxes                           | The strip only signals recognized/edited/current; boxes belong on the center preview only. |
| 12   | OpenAI-like(llama.cpp-like)-compatible only                  | One clear connection type; connection/model/parser/generation params live in `SettingsStore` (Settings dialog + `QSettings`). The prompt/instruction is still hardcoded in `AppController::m_prompt` for now — see `04.2`. |
| 13   | Window geometry persisted (`WindowSettings` + `SettingsStore` `ui/*`) | Persist window pos/size/visibility across sessions. |
| 14   | Unit tests live in `tests/` (Qt Test), gated by `LLOCR_BUILD_TESTS` | Early coverage for parsers; more to come. |
| 15   | JSON model-profile classes (`ModelProfile` / `ProfileRepository`) removed | Stage 2 replaced them with the **Settings dialog**; the leftover classes were dead code and were deleted. |
| 16   | i18n via Qt Linguist (`qsTr`/`tr` + `.ts`/`.qm`), language selectable in Settings (System/English/Русский), default **System**, runtime retranslate | Standard Qt i18n; persisted via `SettingsStore` (`ui/language`); embedded `.qm` loaded at startup and on change. |
| 17   | `det_tokens` parser consumes the `<|det|>…<|/det|>` block stream and JSON-unescapes the content (`\n` → newline, `\\` → `\`, …); legacy bare `label [x1,y1,x2,y2] text` stays as a fallback | The model streams one block per token with JSON-escaped text; the legacy form is kept for older responses. No `<PAGE>` splitting — one page is sent per request. |
| 18   | `det_tokens` also strips model control tokens from content — `<|end_of_sentence|>` (ASCII and full-width-pipe `｜ U+FF5C` + `▁ U+2581` variants) and any stray `<|…|>` wrappers | The model appends an EOS marker after the last block; it must not leak into the recognized text. |
| 19   | The `table` block style is recognized and rendered as a **GFM pipe table** (`\| a \| b \|`) built from the model's `<table>` HTML. `colspan`/`rowspan` are honored by laying cells into a dense grid (a spanned value is repeated across the occupied columns/rows, since GFM has no native spanning); cell math is converted via `convertMath`, and `|` / newlines inside cells are escaped. `table_caption`/`table_footnote` render italic like figure captions | The model emits table content as inline HTML (`<table><tr><td …`) inside the `table` det token; raw HTML must not leak into the markdown, so it is parsed into a pipe table. |
| 20   | The `ref_text` det-token (bibliography/reference list) parses like `text` and renders as a plain paragraph | The model labels reference entries (`[1] …`, `[2] …`) as `ref_text`; they are ordinary text runs (no heading/italic markers), keep their `ref_text` label, and get no extra Markdown styling. |
| 21   | **DRY sampling parameters** (`dry_multiplier`, `dry_base`, `dry_allowed_length`, `dry_penalty_last_n`) exposed in **Settings → Model** and sent in the request body | They materially affect recognition quality with llama.cpp for the Unlimited-OCR model; users can tune them without recompiling. |
| 22   | **Markdown preview** rendered client-side via **Qt WebEngine + bundled marked + KaTeX** (`resources/preview/`) | LaTeX/tables/images render locally with no network access; `image://ocr/crop/*` refs are converted to `data:` URIs before rendering. |
| 23   | Pages can be **deleted** and **drag-reordered** in the thumbnail strip; `PageEditStore` and `PageListModel` remap indices afterwards | Multi-page documents need page management; edits must follow their page across reorder/removal. |
| 24   | **Image/chart block editing** (move / resize / delete) directly on the preview; `rebuildPageText()` regenerates the page Markdown from the boxes | Editing regions is more convenient than re-running OCR; markdown image refs embed the box index, so removal forces a rebuild to keep `image://ocr/crop/<N>` indices consistent. |
| 25   | Detected **duplicate** bounding boxes are collapsed and flagged (`OcrPage::hasDuplicates`, `PageListModel` duplicate role → red marker) | The model can emit the same region twice; the parser dedups it and the UI surfaces it. |

> When decisions change — add a row to the table and update the affected files.
