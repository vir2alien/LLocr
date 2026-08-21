- # LLocr — Context for AI Agents

  > Read this file first. It describes the project and points to where details live.

  ## What it is
  **LLocr** is a cross-platform desktop application for OCR based on local LLMs.
  It provides a convenient interface to connect to a local LLM provider
  (llama.cpp or an OpenAI-compatible API) and recognize text from images/PDFs.

  ## Stack (short)
  - **Application:** Qt6 + C++ + QML, built with CMake (vcpkg declared in the
    `dev` preset, not used in the active build).
  - **PDF input:** Qt PDF module (`QPdfDocument`) — see ADR #7.
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

  ## Build & test (fastest path)
  Tell future agents to compile/run tests by reusing the ready-made `build/`
  directory — do **not** try to re-configure from scratch. The `dev` preset in
  `CMakePresets.json` uses Ninja, but Ninja is **not** installed here; the
existing `build/` is already configured with **Unix Makefiles** and points to
Qt at `/Users/gladskih/Qt/6.10.3/macos`. `VCPKG_ROOT` is **not** set as a shell
variable, and vcpkg does **not** participate in the build.

  ```sh
  # from the repo root:
  cmake --build build -j 8   # build llocr + tests
  ctest --test-dir build      # run unit tests
  ```

  If a clean (from-scratch) configure is needed, do it manually (not via preset):

  ```sh
  cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/Users/gladskih/Qt/6.10.3/macos
  ```

  Unit tests are all wired into `tests/CMakeLists.txt` and run via ctest:
  `test_det_parser`, `test_pagemodel`, `test_settings_store`, and `test_exporter`.

  ## Current status
  - Phase: **Stage 3 complete** (formats & documents); Stage 4 (RAG) not started.
  - Done: Stage 1 (MVP OCR), Stage 2 (extensibility), and Stage 3 (PDF input,
    batch/multi-page processing, HTML/DOCX/PDF export, editable text panel,
    page reordering, image-block editing, Markdown preview, i18n).
    Four unit-test targets exist under `tests/`.
  - Working end-to-end today: open image(s) **or PDF** → configure connection /
    model (incl. DRY sampling params) / output parser in **Settings** →
    recognize a page or **all** pages → browse pages (incl. **during**
    recognition) via a thumbnail strip with recognized / edited / duplicate
    markers, **delete** and **drag-reorder** pages → **stop** a running job →
    edit the recognized text per page (per-page edits via `PageEditStore`) and
    toggle a **Markdown preview** (Qt WebEngine + marked + KaTeX) → **edit
    image/chart blocks** (move / resize / delete) directly on the preview →
    **export** to TXT / MD / HTML / DOCX (Pandoc) / PDF (Pandoc or built-in
    writer), with **All / Current / page-range** scope. The UI is localizable
    (System / English / Русский) and themed (System / Light / Dark).
  - Immediate goal: persist edits with the document across sessions and lay the
    **Stage 4 RAG stub**.
