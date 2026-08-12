- # LLocr — Context for AI Agents

  > Read this file first. It describes the project and points to where details live.

  ## What it is
  **LLocr** is a cross-platform desktop application for OCR based on local LLMs.
  It provides a convenient interface to connect to a local LLM provider
  (llama.cpp or an OpenAI-compatible API) and recognize text from images/PDFs.

  ## Stack (short)
  - **Application:** Qt6 + C++ + QML, built with CMake, dependencies via vcpkg.
  - **PDF input:** Qt PDF module (`QPdfDocument`) — see ADR #8.
  - **RAG service:** a separate local Python service (FastAPI + vector DB),
    communicating over HTTP. To be implemented at a later stage.

  ## Documentation map
  | File                      | Content                                     |
  | ------------------------- | ------------------------------------------- |
  | `docs/01-overview.md`     | Idea, goals, requirements                   |
  | `docs/02-tech-stack.md`   | Technologies and rationale                  |
  | `docs/03-architecture.md` | Architecture, layers, key abstractions      |
  | `docs/04-components.md`   | Providers, settings, export, PDF, RAG        |
  | `docs/05-roadmap.md`      | Development plan by stages                  |
  | `docs/06-dev-setup.md`    | Tooling, build, CI/CD, distribution         |
  | `docs/07-glossary.md`     | Terms and adopted decisions                 |
  | `docs/UnlimitedOCR.md`    | Reference: the Unlimited-OCR model (baidu)   |

  ## Rules for the agent
  1. Communication and comments language — **English**; code identifiers in English.
  2. Do not change the base stack (Qt/C++/QML) without an explicit request.
  3. Follow the layered architecture (see `03-architecture.md`): the UI must not
     directly access network/files.
  4. New models are added via the **Settings** dialog (persisted in
     `QSettings`), not hardcoded.
  5. Build system — **CMake** (not qmake).
  6. When decisions change — update `07-glossary.md`.

  ## Current status
  - Phase: **Stage 3 complete** (formats & documents); Stage 4 (RAG) not started.
  - Done: Stage 1 (MVP OCR), Stage 2 (extensibility), and Stage 3 (PDF input,
    batch/multi-page processing, HTML/DOCX/PDF export, editable text panel).
    Initial unit tests for parsers exist under `tests/`.
  - Working end-to-end today: open image **or PDF** → set the model in Settings →
    recognize a page or **all** pages → browse pages (incl. **during**
    recognition) via a thumbnail strip with a recognized/not-recognized marker →
    **stop** a running job → edit the recognized text per page → **export** the
    result to TXT / MD / HTML / DOCX (Pandoc) / PDF (Pandoc or built-in writer),
    with **All / Current / page-range** scope.
  - Immediate goal: persist edits with the document and lay the **Stage 4 RAG stub**.
