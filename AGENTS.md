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
  | `docs/08-app-icon.md`     | Cross-platform application icon             |
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
  the four base targets (`test_det_parser`, `test_pagemodel`,
  `test_settings_store`, `test_exporter`) plus the local-runtime suite
  (`test_launch_config`, `test_runtime_lifetime`, `test_runtime_locator`,
  `test_capabilities`, `test_server_process`, `test_ensure_connection`,
  (`test_download_manager`, `test_release_catalog`, `test_archive_extractor`,
  `test_install_transaction`, `test_model_catalog`, `test_model_registry`,
  `test_model_memory_estimator`, `test_install_lock`) and the helper
  `mock_llama_server`.

  ## Current status
  - Phase: the app itself — **Stages 1/2/3 complete** (OCR MVP, extensibility,
    PDF/formats); the **local-runtime plan** (`docs/09-local-runtime-plan.md`)
    — managed llama.cpp autostart + model management — has **stages A–E,
    G-core, F, G-UI complete**; currently finishing **Stage H** (polish &
    documentation), of which **H.2** (memory estimate + warning) and **H.7**
    (process/performance polish) are done and **H.8** (docs) is in progress.
  - Also done: PDF input, batch/multi-page processing, HTML/DOCX/PDF export,
    editable text panel, page reordering, image-block editing, Markdown
    preview, i18n. Unit tests: four base targets + fourteen local-runtime
    targets under `tests/`.
  - Local-runtime plan progress:
    - **A** (skeleton, `ConnectionMode`, resolver, settings groups,
      `SingleInstanceGuard`) ✅
    - **B** (binary + process lifecycle, capability detection, no-orphan
      `ProcessGuard`) ✅
    - **C** (`DownloadTask` resume + `DownloadManager`, tests) ✅
    - **D** ✅ — llama.cpp install: `ReleaseCatalog` (GitHub releases + sha256),
      `detectPlatform()`/backend recommendation, CUDA cudart join, hardened
      `ArchiveExtractor` (ZIP), transactional `InstallTransaction`, cleanup of
      unused builds; backend covered by `test_release_catalog` /
      `test_archive_extractor` / `test_install_transaction`. UI in
      **Settings → Runtime**: release/backend pickers, «Download and install»
      with progress, «Installed: bXXXX (CUDA)», «Check for updates»,
      «Clean up unused builds» — driven by the QML singleton `RuntimeInstaller`;
      ru translations updated.
    - **E** ✅ — models from Hugging Face: `ModelCatalog` (tree w/ pagination,
      revision pinning, mmproj / multi-part detection, path encoding),
      `ModelPreset` + `ModelPresetCatalog` (built-in `:/models/default-presets.json`
      + user `models/catalog.json`, merge by id, import/export/reset),
      `ModelRegistry` (index.json, rescan recovery, managed vs external,
      removal guards); backend covered by `test_model_catalog` /
      `test_model_registry`. UI in **Settings → Models** via the QML singleton
      `ModelInstaller`: installed-model table (activate/remove), preset catalog,
      HF search + download, HF token, catalog import/export, GGUF verification;
      ru translations updated.
    - **G-core** ✅ — `ensureConnectionReady()` for Managed (start → health →
      /v1/models → alias, dedup of concurrent callers, `cancelPendingStart()`,
      `runSelfTest()`; error matrix §7.5); covered by `test_ensure_connection`.
    - **F** ✅ — first-run wizard: `SetupWizard.qml` + `Setup/Step{Welcome,
      Runtime,Model,Launch,Done}.qml`, trigger per §4.4 (Timer in Main.qml, no
      network probes), per-step gating; External path sets `setupVersion = 1`;
      Launch step uses the QML self-test bridge `runSelfTestQml()`.
    - **G-UI** ✅ — `Footer.qml` managed-runtime indicator (dot yellow/green/red
      + text, click opens the shared `ServerLogWindow`), indeterminate progress
      + stderr text while StartingRuntime, §7.5 error surfacing for Failed, and
      the «Launch settings changed — restart» banner with a Restart button.
    - **H** 🔄 — H.2 ✅ (memory estimate + warning in the wizard Launch step,
      `ModelMemoryEstimator`), H.7 ✅ (waitForStarted 10s→5s, health 500→250ms,
      probe bounds 2.5s/5s, `probeCached()` LRU, `loadProgressPercent()`
      stderr classification + deterministic footer ProgressBar, `llocr_ru.ts`
      cleaned), **H.8 ✅ (documentation — pages 01–07 + AGENTS.md, ADR 26–45
      recorded)**, **H.6 ✅ (separate locks**: `.install.lock` in the
      `RuntimeInstaller` install/cleanup pipeline, per-write `.registry.lock`,
      `.instance.lock` as runtime-owner; second GUI instance keeps using
      External while runtime/model ops stay exclusive; tested by
      `test_install_lock`)**. Remaining: H.1/H.3 (partial) and optional H.4–H.5.
  - Working end-to-end today: open image(s) **or PDF** → configure connection /
    model (incl. DRY sampling params) / output parser in **Settings** →
    recognize a page or **all** pages → browse pages (incl. **during**
    recognition) via a thumbnail strip with recognized / edited / duplicate
    markers, **delete** and **drag-reorder** pages → **stop** a running job →
    edit the recognized text per page (per-page edits via `PageEditStore`) and
    toggle a **Markdown preview** (Qt WebEngine + marked + KaTeX) → **edit
    image/chart blocks** (move / resize / delete) directly on the preview →
    **export** to TXT / MD / HTML / DOCX (Pandoc) / PDF (Pandoc or built-in
    writer), with **All / Current / page-range** scope, and — in
    **Settings → Runtime** — install a local llama.cpp runtime, and in
    **Settings → Models** — install GGUF models from Hugging Face. A clean
    profile goes through the **first-run wizard** (SetupWizard) from scratch.
    The UI is localizable (System / English / Русский) and themed
    (System / Light / Dark).
  - Immediate goal: **Stage H.8 (documentation)** — done; **H.6** (separate
    install/registry/owner locks) done. Next: remaining polish **H.1/H.3** and
    the optional **H.4–H.5** (see `docs/09-local-runtime-plan.md`).
