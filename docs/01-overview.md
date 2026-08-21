# 01. Project Overview

## Idea
LLocr provides a convenient interface to:
- connect to an **OpenAI-compatible LLM endpoint** (Ollama, LM Studio,
  llama.cpp server, or any hosted OpenAI-compatible API);
- perform OCR on images and PDFs using multimodal LLMs;
- save and structure the result;
- grow a knowledge base (RAG) from recognized documents.

## Target requirements
1. **Cross-platform** — Windows, macOS, Linux.
2. **Configurable model & output** — model name, prompt, generation parameters
   and the output parser are all set by the user (see Settings), so different
   models and output formats are supported without recompilation.
3. **Result export** — Markdown, DOCX (+ TXT, PDF, HTML).
4. **RAG integration** — populate a vector database from scans.
5. **Localizable & themeable UI** — language (System / English / Русский) and
   theme (System / Light / Dark) selectable in Settings.

> **Note:** the app targets **only OpenAI-compatible connections**. Every
> model/connection/parser setting is edited in the **Settings dialog** and
> persisted with `QSettings`. The model **prompt/instruction is still
> hardcoded** in `AppController::m_prompt` ("document parsing.").

## User scenario (MVP) — status
1. The user starts a local LLM runner (Ollama / LM Studio / llama.cpp server)
   or points at a hosted OpenAI-compatible API.
2. In **Settings**, they enter the endpoint URL, model name, API key and
   (optionally) tune generation/parser options.                 ✅ done (tabbed Settings)
3. They load one or more images or a PDF.                        ✅ done (multi-image + PDF)
4. They start recognition.                                       ✅ done (single page + "recognize all")
5. They see the recognized text (+ bounding boxes if the parser
   is `det_tokens`).                                             ✅ done (text panel + bbox overlay)
6. They save the result in the desired format.                   ✅ (TXT/MD/HTML; DOCX via Pandoc; PDF)

## UI reference (current implementation)
The window uses a **toolbar + three-pane** layout.

- **Toolbar** — Open, Recognize, Recognize all, Stop, page navigation
  (‹ n / N ›), Export, and **Settings**.
- **Left strip** — page thumbnails for multi-page documents; each carries a
  **recognized / not-recognized** marker, an **edited** marker, and
  a **duplicate** marker (red). Pages can be **deleted** and **drag-reordered**.
  Clicking a thumbnail jumps to that page (works even while recognition is
  running). No boxes are drawn here.
- **Center pane** — full preview of the current page with **bounding-box
  overlay** when the `det_tokens` parser is selected. Image/chart blocks can be
  **moved, resized, or deleted** directly on the preview.
- **Right pane** — recognized text of the current page, **editable** once the
  page has been recognized, with a **Preview** switch that renders the text as
  Markdown (via Qt WebEngine + marked + KaTeX).

## Settings dialog (tabbed)
- **UI** — language (System / English / Русский), theme (System / Light / Dark).
- **Connection** — endpoint base URL, API key (optional), request timeout.
- **Model** — model name, temperature, max tokens, and the **DRY sampling
  parameters** (multiplier, base, allowed length, penalty last-N).
- **Output** — output parser (`raw` / `det_tokens`; default `det_tokens`).

> The prompt/instruction shown to the model is currently **hardcoded** in
> `AppController` (`m_prompt`) — it is not editable in the UI yet. The bbox
> coordinate range is likewise hardcoded (`DetTokensParser::kBboxCoordinateRange
> = 1000`), not exposed as a setting.
