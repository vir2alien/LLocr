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
   models and output formats are supported without recompilation. For the
   managed local runtime the prompt/parser/context are supplied by **model
   presets** (pre-verified pairs; see Models below).
3. **Result export** — Markdown, DOCX (+ TXT, PDF, HTML).
4. **RAG integration** — populate a vector database from scans.
5. **Localizable & themeable UI** — language (System / English / Русский) and
   theme (System / Light / Dark) selectable in Settings.

> **Note:** the app targets **only OpenAI-compatible connections**. Every
> model/connection/parser setting is edited in the **Settings dialog** and
> persisted with `QSettings`. The recognition **prompt** is supplied by the
> chosen **model preset** (a pre-verified model+parser+prompt pair, see Models);
> a built-in default ("document parsing.") applies when no preset is in use.

## User scenario (MVP) — status
1. The user starts a local LLM runner (Ollama / LM Studio / llama.cpp server),
   **installs a managed llama.cpp runtime**, or points at a hosted
   OpenAI-compatible API.
2. In **Settings** (or the first-run **Setup wizard**), they pick a **connection
   mode** — `External` (connect to an existing server) or `Managed` (the app
   launches its own llama.cpp server) — and configure the endpoint URL, model
   name, API key and (optionally) generation/parser options.
                                                                  ✅ done (tabbed Settings)
3. They load one or more images or a PDF.                        ✅ done (multi-image + PDF)
4. They start recognition.                                       ✅ done (single page + "recognize all")
5. They see the recognized text (+ bounding boxes if the parser
   is `det_tokens`).                                             ✅ done (text panel + bbox overlay)
6. They save the result in the desired format.                   ✅ (TXT/MD/HTML; DOCX via Pandoc; PDF)

## Local runtime & models (managed mode)
Besides connecting to an existing OpenAI-compatible server (`External`),
LLocr can **own the whole runtime** (`Managed` mode):

- **First-run wizard** (`Setup/` steps Welcome → Runtime → Model → Launch →
  Done) drives a clean profile from scratch to a recognized page — no terminal
  needed. Existing profiles are treated as already configured and the wizard
  does **not** appear (ADR 31).
- **Runtime (llama.cpp):** in `Settings → Runtime` the user picks a release +
  backend (CPU / CUDA / Vulkan / Metal) and **downloads and installs** a
  `llama-server` build from **GitHub Releases** (`ggml-org/llama.cpp`), verified
  by `sha256` from the release body. Launch parameters (`--port`, `--ctx-size`,
  `--n-gpu-layers`, `--alias`, `autoStart`) are configured in the wizard /
  launch panel.
- **Models (Hugging Face):** in `Settings → Models` the user installs **GGUF**
  vision models from Hugging Face (revision pinned to a commit `sha`, `sha256`
  from `lfs.oid`), picks a managed model from the **preset catalog**, imports a
  custom catalog, or points at a local `.gguf`. Presets bundle a pre-verified
  `model + mmproj + parser + prompt + ctx-size` pair.
- **Recognition:** in `Managed` mode LLocr automatically starts `llama-server`
  on localhost, waits for `/health`, queries `/v1/models`, and resolves the
  connection — recognition and manual Start/Stop/Restart go through the single
  `RuntimeController` facade.

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
Tabs: **UI** · **Connection** · **Model** · **Output** · **Runtime** · **Models**.

- **UI** — language (System / English / Русский), theme (System / Light / Dark).
- **Connection** — endpoint base URL, API key (optional), request timeout.
  The recognition **mode** (`External` / `Managed`) is chosen by the first-run
  wizard; existing profiles stay `External` by default (ADR 26/31).
- **Model** — model name, temperature, max tokens, and the **DRY sampling
  parameters** (multiplier, base, allowed length, penalty last-N). In
  `Managed` mode the model id / alias is computed by the runtime, not typed.
- **Output** — output parser (`raw` / `det_tokens`; default `det_tokens`).
- **Runtime** — managed `llama-server` binary path + probe, Start/Stop/Restart,
  **Show log**, and the **stage-D installer** (release + backend pickers,
  «Download and install» with progress, «Installed: bXXXX (CUDA)», «Check for
  updates», «Clean up unused builds»).
- **Models** — installed-model table (activate/remove), the **preset catalog**,
  Hugging Face search + download, HF token, catalog import/export, GGUF check.

> The recognition **prompt** is supplied by the selected **model preset**;
> without a preset a built-in default (`AppController::m_prompt`, "document
> parsing.") is used. The bbox coordinate range is hardcoded
> (`DetTokensParser::kBboxCoordinateRange = 1000`), not exposed as a setting.
>
> **Secrets:** the API key and HF token are stored **in plaintext** in
> `QSettings` (existing behavior) with an explicit UI warning; they are never
> written to logs, error messages, or the command preview. Moving them into the
> OS keychain is a planned optional improvement (H.5).
