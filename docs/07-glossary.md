# 07. Glossary and Adopted Decisions

## Terms
- **OCR** — recognizing text from images.
- **LLM provider** — the connection to the model; here always an
  **OpenAI-compatible** endpoint (llama.cpp server, Ollama, LM Studio, hosted API).
- **ProviderConfig** — the settings struct (connection + model + parser) that
  `AppController` fills from `SettingsStore` when building a request. It is no
  longer the persisted source of truth: settings are persisted by
  `SettingsStore` via `QSettings` and edited in the Settings dialog. **Note:**
  `ProviderConfig::prompt` exists but the active prompt is taken from
  `AppController::m_prompt`, which is not persisted (yet).
- **Output parser** — the strategy for parsing a model's response into a
  structured result (`raw`, `det_tokens`), selected in Settings → Output.
- **bbox** — bounding box coordinates of a recognized fragment for overlay on
  the preview (produced by the `det_tokens` parser).
- **RAG** — Retrieval-Augmented Generation; here — indexing scans into a vector DB.
- **Document / page model** — `DocumentModel` holds pages; `PageListModel` feeds
  the thumbnail strip (recognized/edited/current flags, no boxes).
- **Recognized marker** — per-page indicator (green/grey) showing whether a page
  has been OCR'd yet.

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

> When decisions change — add a row to the table and update the affected files.
