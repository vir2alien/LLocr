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
  - **DjVu input:** optional DjVuLibre C API (`DjVuDocument`), `.djvu` / `.djv`;
    see ADR 65/69 and `docs/06-dev-setup.md`. Without the dependency CMake
    warns and builds without DjVu (option `LLOCR_WITH_DJVU`); opening `.djvu`
    reports an actionable error at runtime. Windows needs an MSVC x64 build
    (`DJVULIBRE_ROOT`) and the matching DjVuLibre DLL alongside the executable
    or on PATH; do not assume a vcpkg port exists. `test_djvu_document` (and
    `test_app_import`) are built only when DjVu support is enabled.
  - **RAG service:** a separate local Python service (FastAPI + vector DB),
    communicating over HTTP. To be implemented at a later stage.
  
  ## Documentation map
  
  | File                      | Content                                    |
  | ------------------------- | ------------------------------------------ |
  | `docs/01-overview.md`     | Idea, goals, requirements                  |
  | `docs/02-tech-stack.md`   | Technologies and rationale                 |
  | `docs/03-architecture.md` | Architecture, layers, key abstractions     |
  | `docs/04-components.md`   | Providers, settings, export, PDF, RAG      |
  | `docs/05-roadmap.md`      | Development plan by stages                 |
  | `docs/06-dev-setup.md`    | Tooling, build, CI/CD, distribution        |
  | `docs/07-glossary.md`     | Terms and adopted decisions                |
  | `docs/08-app-icon.md`     | Cross-platform application icon            |
  | `docs/09-local-runtime-plan.md` | Managed runtime: llama.cpp + model install |
  | `docs/12-code-debt-audit.md` | Code audit: dead code, duplication, cleanup plan (Oct 2026) |
  | `docs/architecture-plan/README.md` | Architectural review (Sep 2026) and the   |
  |                           | remediation plan (stages 0–7)              |
  | `docs/TODO.md`            | Short backlog of open items                 |
  | `docs/UnlimitedOCR.md`    | Reference: the Unlimited-OCR model (baidu) |
  | `docs/13-lfm25-vl-3b.md`  | Model manual: LFM2.5-VL-3B (launch, requests, 2:3 calibration, links); Russian mirror in `13-lfm25-vl-3b.ru.md` |
  | `docs/15-teleocr.md`      | Model manual: TeleOCR/NaviDC-OCR (block-recognition prompts, GGUF quants, roles); Russian mirror in `15-teleocr.ru.md` |
  | `docs/16-gemma-4.md`      | Model manual: Gemma 4 E4B/12B QAT (MTP drafter in the model repo, image token budgets 70–1120, per-card flash-attn split); Russian mirror in `16-gemma-4.ru.md` |
  | `docs/17-d1-verification-study.md` | Study: decision models as OCR verifiers (d1-3B vs clef-flash) — methodology, request schemes v0–v9, calibration, why clef-flash replaces d1-3B |
  | `docs/18-clef-flash.md`   | Model manual: Cloudflare clef-flash decision model (launch, recommended v5 request scheme, thresholds); Russian mirror in `18-clef-flash.ru.md` |
  | `docs/19-glm-ocr.md`      | Model manual: GLM-OCR (trained task prefixes, no layout/coordinates, `raw` parser, glm4 metadata); Russian mirror in `19-glm-ocr.ru.md` |
  | `docs/20-model-task-rework.md` | Rework plan: six tasks instead of four roles, two-layer verification, the fix pipeline (since ADR 192 the `full_block` protocol), page statuses, new settings windows (Oct 2026); stage table with statuses |
  | `docs/21-d3.md`           | Model manual: d3-flash / d3-mini decision models (System One protocol, non-causal state reading, minBuild b11572); Russian mirror in `21-d3.ru.md` |
  | `docs/22-unlimited-ocr.md` | Model manual: Unlimited-OCR (pageOcr default, DRY anti-loop sampling, vision budget, `unlimited-ocr` det parser); Russian mirror in `22-unlimited-ocr.ru.md` |
  | `docs/23-lightonocr.md`   | Model manual: LightOnOCR-3-4B (two trained prompts — grounding + empty transcription, three roles, everything else out of distribution); Russian mirror in `23-lightonocr.ru.md` |
  | `docs/24-mineru.md`       | Model manual: MinerU2.5-Pro-2605-1.2B (two-step protocol, 1036² squash, `--special`, layout absorption); Russian mirror in `24-mineru.ru.md` |
  | `docs/25-paddleocr-vl.md` | Model manual: PaddleOCR-VL-1.6 (blockRecognition only — the layout model ships outside the GGUF, official single-quant repo); Russian mirror in `25-paddleocr-vl.ru.md` |
  | `docs/26-qwen3.5.md`      | Model manual: Qwen3.5-4B/9B, the fix default (OK/FIX/REVIEW, MTP drafting, framed candidate); Russian mirror in `26-qwen3.5.ru.md` |
  
  ## Rules for the agent
  
  1. Do not change the base stack (Qt/C++/QML) without an explicit request.
  2. Follow the layered architecture (see `03-architecture.md`): the UI must not
     directly access network/files.
  3. New models are added via the **Settings** dialog (persisted in
     `QSettings`), not hardcoded.
  4. Build system — **CMake** (not qmake).
  5. When decisions change — update `07-glossary.md`.
  6. UI text sizes — use `Theme.*Size` / `Theme.*` fonts from
     `src/qml/Theme.qml`; never hardcode `font.pointSize` in QML. They
     carry the macOS point-size compensation (ADR 77).
  7. **Comments only in the most extreme cases** — when the code cannot be
     understood from the surrounding context and from the names of the classes,
     functions and variables. Never restate what the code says, never narrate
     the obvious ("loop over pages", "returns true on success"), never leave
     commented-out code, section banners, or TODO notes where the commit/git
     history and the ADRs belong. When a piece of code seems to need a comment
     to be clear, the first response is better naming or a smaller function —
     comment last. A comment that survives must answer *why* this way and not
     *what* happens (workarounds, invariants, ADR references, non-obvious
     platform behaviour).
  
  ## Build & test (fastest path)

  One command runs the whole verification (configure if needed → build →
  `ctest` → clang-format → `qmllint`); it is exactly what CI runs, and it is the
  fastest way to be sure the tree is green (ADR 100):

  ```sh
  scripts/check.sh                 # reuse the configured build/ tree
  scripts/check.sh --configure     # re-configure it first
  ```

  CI runs this script in two tiers (ADR 121): `--format-only` (clang-format
  alone, no Qt) on every push, the full sequence on `workflow_dispatch` only —
  the hosted runners have no Qt 6.10.3 and the provisioning step is still a
  `TODO(provision-qt)`. Read the ADR before touching the workflow; a push-triggered
  matrix that cannot pass is what made CI mail “all jobs failed” on every commit.

  Tell future agents to compile/run tests by reusing the ready-made `build/`
  directory — do **not** try to re-configure from scratch. The `dev` preset in
  `CMakePresets.json` matches that tree (**Unix Makefiles**, Qt at
  `~/Qt/6.10.3/macos`); Ninja and vcpkg are **not** used. `VCPKG_ROOT` is **not**
  set as a shell variable, and vcpkg does **not** participate in the macOS build.

  ```sh
  # from the repo root:
  cmake --build build -j 8   # build llocr + tests
  ctest --test-dir build      # run unit tests
  ```

  Two things to know before touching the build:

  * Every test has a `TIMEOUT` and a headless platform by default, applied
    directory-wide at the end of `tests/CMakeLists.txt` (ADR 99) — a hung
    `waitForStarted()` fails the run instead of blocking `ctest` forever.
  * The QML tree is a real module (`qt_add_qml_module`, ADR 101): new `.qml`
    files are globbed, `qmlcachegen` runs, and the files live at
    `qrc:/qt/qml/LLocr/…`. The C++ singletons are still registered by hand in
    `main.cpp`; the `QML_ELEMENT`/`QML_SINGLETON` macros were removed from the
    five classes that carried them (they would make `qmltyperegistrar` emit a
    default-constructor call for classes that have none) and come back with the
    `create()` factories. See `.qmllint.ini` for what the lint gates.
  * The C++ code is split into **layer targets** (ADR 106): `llocr_core` →
    `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` →
    `llocr_app` → `llocr`. A layer can only use what it links, and the 31 tests
    link the same targets — never a hand-listed copy of the sources. Each layer
    owns a directory of the same name under `src/`, and the include root is
    `src/`, so an include's spelling already names the layer it comes from. Note
    `LaunchProfileStore` lives in `llocr_runtime` (it needs the release
    catalog and a QML list model), while `RequestProfileStore` is in
    `llocr_config`.
  
  If a clean (from-scratch) configure is needed, do it manually (not via preset):
  
  ```sh
  cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/macos
  ```
  
  **On Windows** use the MSVC 2022 64-bit Qt package
  (`C:/Qt/6.10.3/msvc2022_64`) and the MSVC 2022 64-bit compiler — the MinGW
  Qt build does **not** ship Qt WebEngine (required for the Markdown preview),
  see ADR 54. The supported path is the `dev-win` preset (Ninja,
  `build/win-msvc2022`); the old NMake JOM kit builds again too after the
  ADR 176 QML-module relayout — fixing from-scratch builds was the point.
  
  ### Windows (MSVC 2022 64-bit) — concrete steps
  
  The Windows machine has Visual Studio 2022 (Community) and Qt
  `C:/Qt/6.10.3` with both `mingw_64` and `msvc2022_64` packages. **Use only
  `msvc2022_64`** — the MinGW package has no Qt WebEngine, so `find_package`
  fails and the app cannot build. ZLIB comes from vcpkg at `C:/vcpkg`
  (toolchain `x64-windows`; vcpkg participates only on Windows, not on the
  macOS build; see `THIRD_PARTY_NOTICES.md`).
  
  The ready-made tree is `build/win-msvc2022` (the `dev-win` preset: Ninja,
  Debug). Incremental builds reuse it directly; a from-scratch build is the
  same two commands after deleting the tree — nothing else, that is the point
  of ADR 176:
  
  ```bat
  :: In an “MSVC … x64 Developer Command Prompt” (or after vcvars64.bat):
  cmake --preset dev-win             :: configure (skip if the tree exists)
  cmake --build --preset dev-win     :: build llocr + tests
  :: Before running tests, put the Qt DLLs on PATH (the exes need them):
  set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
  ctest --test-dir build\win-msvc2022 -j 4
  ```
  
  The legacy `build/Desktop_Qt_6_10_3_MSVC2022_64bit_*` trees are the old
  NMake JOM kits; after `cmake .` inside them they build again (ADR 176), but
  new work should go through the preset tree.
  
  Windows-specific gotchas future agents should remember:
  
  - **Qt DLLs must be on `PATH` to run any built exe** (ctest fails with exit
    code `0xc0000135` = DLL not found otherwise).
  - The MSVC compiler is strict about things MinGW/Clang accepted: octal
    literals are `0755u`, not `0o755u` (GCC/Clang extension); Windows API
    constants must come from an explicit `#include <windows.h>` (never rely on
    transitive MinGW headers); the Job-Object flag is
    `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (there is no `…_KILL_ON_CLOSE` in the
    SDK).
  - `LLOCR_MOCK_SERVER` is injected as `$<TARGET_FILE:mock_llama_server>` so
    the `.exe` suffix resolves; never hardcode `…/bin/mock_llama_server`.
  - Path comparisons must normalize `/` vs `\` (`QDir::fromNativeSeparators` +
    `QDir::cleanPath`) — `QDir::separator()` is `\` on Windows.
  - Unit tests must set the Qt app/org identity for `QSettings` to work
    (`SettingsStore::makeSettings()` does this automatically); a bare
    `QSettings()` is read-only on Windows (`status()==AccessError`).
  - A test class that constructs `SettingsStore` must declare
    `TestSettingsIsolation m_settingsIsolation;` (from `tests/testsettings.h`)
    as its **first** member. Without it `makeSettings()` falls back to the
    real `llocr` / `LLM OCR` identity, and the test overwrites the user's
    actual profile (`runtime/rootDir`, `runtime/modelsDir`, …) with paths
    inside a self-deleting `QTemporaryDir` — the app then "forgets" the
    installed runtime and models on every start (the Sep-2026
    "Models tab resets after restart" bug). The guard redirects the default
    `QSettings()` constructor to a temp INI file instead of the registry.
  - Tests needing symlinks are `#ifdef Q_OS_UNIX`-guarded: creating symlinks
    on Windows needs Developer Mode/admin (WinError 1314).
  
  Unit tests are all wired into `tests/CMakeLists.txt` and run via ctest:
  the four base targets (`test_det_parser`, `test_pagemodel`,
  `test_settings_store`, `test_exporter`) plus the export-render integration
  test (`test_export_renderer` — headless WebEngine, needs the Qt DLLs on
  `PATH`; it runs Chromium single-process on macOS, because a restricted
  session cannot register Chromium's Mach rendezvous server and Chromium
  would abort with SIGTRAP) plus the local-runtime suite
  (`test_launch_config`, `test_runtime_lifetime`, `test_runtime_locator`,
  `test_capabilities`, `test_server_process`, `test_ensure_connection`,
  (`test_download_manager`, `test_release_catalog`, `test_archive_extractor`,
  `test_install_transaction`, `test_model_catalog`, `test_model_registry`,
  `test_model_memory_estimator`, `test_install_lock`), the project-file suite
  `test_project_store` (`.llocr` save/open, ADR 131) and the helper
  `mock_llama_server`.
  
  ## Current status
  
  - Phase: the app itself — **Stages 1/2/3 complete** (OCR MVP, extensibility,
    PDF/formats); the **local-runtime plan** (`docs/09-local-runtime-plan.md`)
    — managed llama.cpp autostart + model management — has **stages A–E,
    G-core, F, G-UI complete**; **Stage H (polish & documentation) complete**:
    H.1–H.3, H.6–H.8 done, **H.4 closed** (no watchdog), **H.5 deferred**
    (keychain).
  - Also done: PDF input, batch/multi-page processing, HTML/DOCX/PDF export,
    editable text panel, page reordering, image-block editing, Markdown
    preview, i18n, **project save/open** (self-contained `.llocr` ZIP with the
    embedded sources — pages, blocks, verification state, manual edits; ADR
    131). Unit tests: four base targets + fifteen local-runtime
    targets under `tests/` (the OCR-model suite `test_ocr_models` guards the
    model ↔ parser link; `test_project_store` covers the project container).
  - Local-runtime plan progress:
    - **A** (skeleton, `ConnectionMode`, resolver, settings groups,
      `SingleInstanceGuard`) ✅
    - **B** (binary + process lifecycle, capability detection, no-orphan
      `ProcessGuard`) ✅
    - **C** (`DownloadTask` resume + `DownloadManager`, tests) ✅
    - **D** ✅ — llama.cpp install: `ReleaseCatalog` (GitHub releases + sha256),
      `detectPlatform()`/backend recommendation, CUDA cudart join, hardened
      `ArchiveExtractor` (ZIP + `.tar.gz` via zlib, ADR 34 amended),
      transactional `InstallTransaction`, cleanup of
      unused builds (refused while no build is active — the sweep would
      otherwise delete every download); backend covered by `test_release_catalog` /
      `test_archive_extractor` / `test_install_transaction`. UI in
      **Settings → Runtime**: release/backend pickers, «Download and install»
      with progress, «Installed: bXXXX (CUDA)», «Check for updates»,
      «Clean up unused builds» — driven by the QML singleton `RuntimeInstaller`;
      ru translations updated. Installed-build scan + activation (ADR 60,
      `scanInstalledBuilds` + `activateBuild`): previously downloaded builds
      are listed in Settings → Runtime and in the wizard's Runtime step and
      can be activated without a re-download — recovers after a settings
      reset; covered by `test_install_lock`.
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
      ADR 61: the `configValid` gate applies only when a start would be
      needed — a live Ready/Starting server is resolved via /v1/models
      (first-model fallback) even when `launch/modelPath` is stale (settings
      reset), and `startServer()` refuses a model-less Managed start with an
      actionable message; `translateServerLine()` messages moved to the
      correct `tr()` context. Tests: `readyServerResolvesAfterModelSettingWiped`,
      `managedStartRefusedWithoutModel`, `missingModelPathNamedInErrors`.
      ADR 62: model selection also recovers after a reset without a
      re-download — `ModelInstaller` reads the registry via fresh
      `RuntimePaths`, gained `refreshInstalled()` + `activatePreset()`, the
      wizard's Model step lists installed models with Activate (and an
      installed preset's Install button becomes Activate), refusals name the
      recorded model path.
      ADR 88 (**output parsers are now per-model and data-driven**): parser
      options moved out of the app layer into `ParserOptions`
      (`keepPageNumbers`/`tablesAsHtml`/`bboxRange`/`modelId`, passed to
      `ParserFactory::create`), so `AppController` no longer `dynamic_cast`es a
      concrete parser; `rebuildText(page)` + `displayName()` joined
      `IOutputParser` (the free `rebuildPageText()` is gone — `RawParser`
      returns the page text, closing a latent empty-page rebuild);
      `parser/id` now defaults to **`auto`** and resolves through
      `OcrModel::defaultParserId()` (no more dead code, no mismatched parser
      after a model switch — guarded by
      `test_ocr_models::everyModelDeclaresARegisteredParser`); the label →
      block-style map moved to `resources/profiles/labels.json` +
      `BlockStyleMap` (per-model overrides, compiled-in fallback); and a
      reply with no layout tokens now produces an `OcrResult::notes` entry
      surfaced as `Controller.parseWarning` in the footer instead of a silent
      "successful" blob. The stage-4 pipeline split is recorded as deferred
      (ADR 89) — do it when a model with a non-det-token reply shape lands.
    - **F** ✅ — first-run wizard, **reworked to five steps (ADR 170)**:
      `SetupWizard.qml` + `Setup/Step{Intro,Welcome,Hardware,External,
      Output,Check}.qml`, trigger per §4.4 (Timer in Main.qml, no network
      probes), per-step gating. Managed: Intro (language/theme) → mode →
      **Hardware and models** (GPU/VRAM via `HardwareInfo` — DXGI on Windows,
      Metal on macOS; hardware-backed backend recommendation in an editable
      ComboBox; the shared `Common/TaskModelsTable` prefilled by
      `ModelInstaller::defaultTaskChoices()` — role-default models, ADR 169,
      memory-fitting quants, budget 0.9 × (VRAM ‖ RAM)) → Output → **Check**
      (one button: download the missing runtime — an installed one is reused —
      and the missing quants via the sequential queue, then `runSelfTestQml()`;
      a passed self-test is the only way to Finish). External: Intro → mode →
      External endpoint → Output → Finish (StepExternal is awaiting its own
      rework). The old Runtime/Model/Launch/Done steps are deleted.
    - **G-UI** ✅ — `Footer.qml` managed-runtime indicator (dot yellow/green/red
      + text, click opens the shared `ServerLogWindow`), Start/Stop toggle for
        the managed server (Managed mode only, `startServer()`/`stopServer()`),
        busy spinner (28 px, recognition + server startup)
      + stderr status text while StartingRuntime, §7.5 error surfacing for
        Failed, and
        the «Launch settings changed — restart» banner with a Restart button.
    - **H** 🔄 — H.2 ✅ (memory estimate + warning, `ModelMemoryEstimator`;
      the wizard Launch step that hosted it is gone since ADR 170 — the
      estimate lives on in Settings → Launch; **ADR 191** — the estimate became
      a *requirement*: the model profiles ship the architecture and the curated
      sizes, so every quantization of every model carries the minimum video
      memory it needs (weights · projector · draft · KV cache · working set ·
      vision encoder · overhead) and the quant dropdown shows it
      (`VRAM ≥ 11.4 GiB`) instead of the parenthetical hint), H.7 ✅ (waitForStarted 10s→5s, health 500→250ms,
      probe bounds 2.5s/5s, `probeCached()` LRU — plus a **shared probe budget**
      (`--version` + `--help`, worst case = `timeoutMs`, ADR 51) and a
      **persistent capabilities cache** `capabilities-<sha1(path+mtime+size)>.json`
      served across app runs (ADR 52), `loadProgressPercent()`
      stderr classification (status text; the footer ProgressBar was later
      removed as uninformative — only the busy spinner remains), `llocr_ru.ts`
      cleaned), **H.8 ✅ (documentation — pages 01–07 + AGENTS.md, ADR 26–45
      recorded)**, **H.6 ✅ (separate locks**: `.install.lock` in the
      `RuntimeInstaller` install/cleanup pipeline, per-write `.registry.lock`,
      `.instance.lock` as runtime-owner; second GUI instance keeps using
      External while runtime/model ops stay exclusive; tested by
      `test_install_lock`), **H.1 ✅ (UI polish**: log-window buttons + autoscroll
      + live indicator, empty-state hints, consistent Stop text, banner
        "Hide" + **restart confirmation when recognition is in progress**,
        indicator tooltip), **H.3 ✅ (update check opt-in**: "Check for updates"
        does a check only, 6 h cache; plaque "A newer build bNNNN is available"
        with Update / View changes buttons; deferred install when the server is
        Ready — "Stop server and update"; auto-check on tab-open only when
        `runtime/checkUpdates` is enabled via a checkbox in Settings → Runtime)**.
        **H.4 closed** (watchdog-helper not shipped in MVP — see ADR 47);
        **H.5 deferred** (keychain — see ADR 48).
  - Working end-to-end today: open image(s) **or PDF** → configure connection /
    model (incl. DRY sampling params) / output parser in **Settings** →
    recognize a page or **all** pages → browse pages (incl. **during**
    recognition) via a thumbnail strip with recognized / edited / duplicate
    markers, **delete** and **drag-reorder** pages → **zoom and pan the page
    preview** (the wheel scrolls, Ctrl/⌘+wheel zooms, the page drags; the
    selected block is scrolled clear of the block panel — ADR 175) → **stop** a
    running job →
    edit the recognized text per page (per-page edits via `PageEditStore`) and
    toggle a **Markdown preview** (Qt WebEngine + marked + KaTeX) → **edit
    image/chart blocks** (move / resize / delete) directly on the preview →
    **export** to TXT / MD / HTML / DOCX (Pandoc) / PDF (Pandoc or built-in
    writer), with **All / Current / page-range** scope, **save** the whole
    working state to a self-contained **`.llocr`** project (pages + sources,
    recognized blocks, verification results, manual edits) and **open** it
    again (File menu; `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S`; ADR 131), and — in
    **Settings → Runtime** — install a local llama.cpp runtime, and in
    **Settings → Models** — pick a model+quantization per task and download it
    from Hugging Face (one window since ADR 168: quant dropdowns with curated
    sizes `models/quant/<id>` remembered per profile, ⚙ per row for
    Launch/Request, and a «files on disk» block with guarded delete; the
    wizard's Model step keeps the `ModelDownloadList` list, ADR 122). A clean
    profile goes through the **first-run wizard** (SetupWizard) from scratch.
    The UI is localizable (System / English / Русский) and themed
    (System / Light / Dark).
  - **Verification and fix pipeline (ADR 145–147, 161–164, 172, 183; Oct 2026)** —
    verification runs in two stages: a fast **decision model** (clef-flash or
    d3-flash/d3-mini via `/v1/systemone`, the `decision` task in «Модели»)
    judges every queued block. All three ship the **same scheme v7**
    (`docs/21-d3.md` §4b): ONE question per request (a noul `match`) and the
    **axes** the block is checked on — script, case, face, position,
    delimiters, designations, units — in `state.notation`, with the
    document language named there through a `{{LANGUAGE}}` placeholder.
    d3 answers each question of a request with its own forward pass, so the
    v6 second question (`diagnosis`) doubled the price of a check and is
    gone; the verdict is routed two ways: `match ≥ threshold` → **Ok**,
    else **Mismatch** (a crop the model cannot read lands on **Review** in
    the fix task, which answers REVIEW for an unreadable, clipped or empty
    crop); thresholds resolve per block type (`matchThresholdByBlock` in the
    profile → role `matchThreshold` → the setting). A formula reaches the
    decision model in its **canonical wire form** (the same
    `FixDiffGate::normalizeLatexCandidate` the fix task gets), so the OCR
    models' spaced LaTeX does not read as noise and both tasks judge the
    same text. d1-3B failed the task
    in every phrasing and was removed (ADR 161). Every block carries two text layers with independent verdicts
    (ADR 164): `box.text` (the OCR result) and `box.correctedText` (the fix
    model's correction); the dot and the confidence badge follow the
    **effective** layer. The **fix** task (universal models,
    OK/FIX/REVIEW, the `full_block` protocol — ADR 192, which dropped the
    ADR 180 `span_hybrid` span contract because the small models could not
    carry it; the three-verdict scheme and the
    document-language placeholder are ADR 181) runs as a separate action
    (ADR 178): the
    «Исправить» button sends the mismatched blocks; a FIX carries the
    **complete corrected block** and nothing else — the gate
    (`FixDiffGate::fixBlock`) keeps the candidate's own bytes when the
    corrected block is equivalent to it (the model re-spelled it), and an
    empty reply, a `FIX none` or a reply cut by the token budget demotes the
    verdict to Review (the reason lands in the footer); a reply outside the
    three verdicts (an old `EMPTY`, a free-text remark) lands on
    Review as a deviation. The fix profiles carry the same per-block equivalence rules as
    the decision prompts (ADR 183: the axes and the language-parameterized
    script rule) so the two models cannot disagree on е/ё, homoglyphs, LaTeX
    commands or table cells; `maxOutputByBlock` budgets the reply per block
    type — the whole block travels in the reply, so the budget must fit it.
    The budgets are the protocol's own rule, so they live in the **request
    profile** (`fix-requests.json`), one copy for all four fix models; a model
    profile may still ship its own entry for a block type and it wins. The
    fix models may also **think first**: the mode is the request parameter
    `reasoning_budget_tokens` with the four steps off / low 512 / med 1024 /
    high 1536 (ADR 193). The model profile says whether the model can think at
    all (`reasoning: true`), the request profile holds the steps, the budget
    rides the request (llama.cpp reads it per request — no server restart) and
    it is also the switch: `enable_thinking` follows it, so zero keeps the old
    behaviour; the thinking tokens come out of the generation budget, so
    `max_tokens` grows by the reasoning budget and the answer keeps the room
    the block needs. Block recognition (plain re-transcription)
    entry points: per-block «Распознать», the page toolbar's «Распознать
    страницу ▾» (problem blocks / all blocks of the page); the header
    «Исправить» runs the fix task over the problem blocks of the whole
    document. Planned blocks (filter-enabled type or duplicate suspect,
    unanswered) show the gray dot (`boxVerificationPlanned`). Model manuals:
    `docs/18-clef-flash.md` (v5), `docs/21-d3.md` (v6).
  - **The block panel is a version inspector (ADR 185–187, Oct 2026)** — the
    panel shows the two versions of the selected block as two clickable
    cards (`MainWindow/BlockVersionCard.qml`); the card in view is the version
    the document carries, the other one stays for comparison (the preview
    text follows the choice). Every status has one place: the state of the
    block in the header (распознан / проверен / исправлен / утверждён /
    изменён вручную), the
    verdict and the score of each layer in its own card header, and the fix
    outcome («Не требует исправления») next to the state when there is no
    correction to show. The fragments a correction changed are **tinted in
    place** — soft red in the original, soft green in the correction, computed
    word by word in `core/TextDiff.h` (a word is a run of letters, digits and
    the characters that belong to a word inside LaTeX) and rendered as rich
    text with `white-space: pre-wrap`, so the spaces and the line breaks of a
    formula survive; the «Различия» switch turns the tint off — the switch
    appears only when there is a second version to compare (ADR 188). A formula, a
    table or a code block is read in a monospace face (`Theme.mono`).
    «Утвердить блок» / «Утвердить исправление» approves the version in view
    (`BoxCheckStatus::Approved`); while the confirmation stands the same button
    reads «Отозвать утверждение» and puts the model's verdict back (ADR 187 —
    the block keeps the verdict the approval replaced, `checkStatusBeforeApproval`).
    `Ctrl`/`Cmd+Return` approves it and jumps to
    the next problem block, and «Изм.» lets the operator write the text by
    hand: the button becomes «Сохранить» (the exit from the edit is explicit,
    `Esc` cancels), the version is stored as **«Изменён вручную»**
    (`BoxCheckStatus::Edited` — the user's own text, not a confirmation of the
    model's verdict), and the page text follows it. The cards carry a uniform
    frame — no colored stripe on the left. The panel carries **no colour of its
    own** (ADR 188): the confirm button and «Сохранить» are ordinary buttons,
    the verdicts are the only green and red in it.
  - **A block is selected, drawn and moved in three separate gestures
    (ADR 189, Oct 2026)** — a click on a block only **selects** it (the block
    panel opens); a drag that starts on a block moves the **page**, as a drag
    on the empty page does (the pan lives in `ImagePreview.beginPan` /
    `continuePan`, shared by the page's and the blocks' mouse areas). The
    block's **mini toolbar** carries the edit button (icon «move»): it opens
    the block for moving and resizing (the eight handles appear only then) and
    becomes a **checkmark** («готово») in the mode; the checkmark or a click
    outside the block leaves the mode (another block takes the selection, the
    empty page deselects). The toolbar's add buttons and the block-type menu
    **arm the draw mode** instead of dropping a block at a default place: the
    cursor turns into a crosshair, a chip follows it and names the block being
    drawn, the drag paints the rectangle in the hue the block will wear, and
    the release creates the block (`Controller.addBlockAt(type, x, y, w, h)`,
    clamped into the page) and selects it. `Esc` or a second click on the same
    button cancels the mode. A block that carries no text and no verdict says
    **«Разметка»** in the panel (and shows no dot) — the same word the page
    uses for itself, «Только разметка»; «Распознано» is only true for a block
    the OCR has read.
  - **A structural change is not a manual edit (ADR 190, Oct 2026)** — the
    «Изменено» marker means one thing only: *the user typed the text by hand*
    (`setCurrentPageText`). A **removed** block — like a block landing
    (recognition, fix, re-check) — only moves the revert target forward, so
    the page text keeps following the blocks and the next recognition writes
    it again. Before the fix, a removal froze the page text: a block drawn and
    recognized after a deletion never reached the panel (the MD view kept the
    old text), so the work looked lost. Two rules keep the user's own text
    safe: a removal never replaces it, and a pipeline change on a hand-edited
    page is **merged** into that text (the common part anchors the change,
    the user's wording wins where it diverges) instead of being dropped. The
    removed block's text still never comes back — the revert target moves
    with the blocks.
  - **Manual page markup (ADR 149–150, Oct 2026)** — the page toolbar's
    «Разметить страницу» (+ ▾ «Разметить все страницы») runs the **layout
    task** (`roles.layout` in a model profile — TeleOCR ships it): the page
    goes to the layout model with the card's layout prompt, the reply's
    `<box:…><label:…><orientation>` lines become **text-less** positioned
    blocks (parser `teleocr-layout`), and the text is then filled in through
    blockRecognition — per-block «Распознать» or the
    «Распознать все блоки страницы» menu item (queues every NotChecked
    text-less box). The markup pass replaces the page's blocks (confirmation
    if the page has results); the page reads «Только разметка» until every
    text block has text (ADR 165). The layout model is configured as the
    `layout` task in «Модели» (`tasks/layout/*`,
    `ConnectionRole::Layout`, request profile `requestLayout.json`;
    deliberately absent from the setup wizard). The 1036² page squash mirrors
    the upstream calibration. Manual only — not wired into the automatic
    pipeline. Model manual: `docs/15-teleocr.md`.
  - **Interface rework (ADR 151, Oct 2026; toolbar ADR 167 → ADR 171)** — the
    header is one row of labeled groups: «Файл / Импорт» (stacked; «Файл» is
    the project menu — open / save / save-as project, Ctrl+O moved to
    «Импорт»), «Страницы» (a page-range field in the compact 1-based form
    "1-10, 15" — a two-way editor of the thumbnail selection through
    `PageListModel::selectedPagesRange()` / `applyPagesRange()`: the
    thumbnails update the field, typing a range replaces the selection,
    an empty input = the whole document, garbage changes nothing; empty
    selection = the whole document as before), «Авто» (the
    Fast/Balanced/Accurate segmented switch is live, its «Распознать N стр»
    button stays disabled until the automatic pipelines land), «Разметка»
    (Разметить / Распознать блоки), «Распознать» (С разметкой / Без
    разметки — the ADR 167 three-mode picker dissolved into explicit
    buttons) and «Верификация» (Проверить / Исправить) — all acting on the
    **thumbnail selection** (Ctrl/Cmd+click toggles, «Выбрать все» in the
    column header; «Исправить» lights `Theme.accentAttention` with a `(N)`
    badge of `scopedPendingFixCount` — the blocks that still wait for the
    fixer (ADR 188): a block leaves the count when the fixer has answered its
    candidate (OK or REVIEW), a correction the decision model rejects comes
    back into it). «Проверить» runs the decision pass
    only (`checkScopeEnabledBlocks()`) — verify and fix are separate buttons
    in the manual pipeline; the automatic check/fix switches are gone
    (ADR 178).
    While anything runs the pipeline buttons go inactive and the started
    action's button turns into the stop control — a black square inside a
    spinning ring; the mapping is by flags/phases (`layoutBusy` → «Разметить»,
    `busy` + the last-used page-recognition button, `checkBusy` +
    `checkDecisionPhase`/`checkRecheckPhase`/`checkFixPhase` → «Проверить» /
    «Распознать блоки» / «Исправить»), so a run started elsewhere still shows
    its stop on the matching button. The model-download queue has no toolbar
    stop — it is cancelled where it starts (Models window, wizard). The
    progress text stays in the footer (the `TaskStatus` singleton). On the
    right — Экспорт (opens the
    export dialog, the scope is chosen there) and «Настройки» (the settings
    menu). The ADR 167 preset stub («Ручной (Пошаговый)» + three grey
    presets) is deleted — the «Авто» switch is its successor. The **page
    toolbar**
    (`MainWindow/PageToolBar.qml`) collapses to one row: «Стр. ‹ n / N ›»
    page navigation plus the block-add buttons (T / image / table / Σ / code
    + a ▾ with every `BlockNames` type — they add an empty block of that type
    to the current page, ADR 177) and page rotation. The right panel switches text/preview through
    the segmented «Текст (MD) | Предпросмотр» control next to «Копировать»
    (`copyCurrentPageText`). The selected block carries a floating
    **mini-toolbar** (`MainWindow/BlockMiniToolbar.qml`: recognize / verify /
    fix / delete). `BlockEditPanel` is a **version inspector** (ADR 186): the
    recognized text (OCR) and the correction (AI) are two
    `MainWindow/BlockVersionCard.qml` cards, the card in view is the version the
    document carries (accent border, stripe, radio, «Используется в документе»)
    and clicking a card switches the layer (the preview text follows it). The
    header carries one status word (распознан / проверен / исправлен /
    утверждён), the **Различия** toggle, «Сбросить исправление» and the
    primary «Утвердить блок» / «Утвердить исправление» (ADR 185). Each card
    header carries the verdict and the score of **its own** layer; the changed
    fragments are **tinted in place** — soft red in the original, soft green in
    the correction, computed word by word (`core/TextDiff.h`) — and a formula,
    table or code block is read in a monospace face (`Theme.mono`). `Ctrl`/
    `Cmd+Return` approves the block in view and jumps to the next problem
    block; «Изм.» turns the card into a field where the operator writes the
    text by hand (the version is approved and the page text follows).
    Icons: Lucide SVGs in `resources/icons/ui/` (qrc
    `/icons/ui`, `ui_icons` CMake resource) colorized by `Common/Icon.qml`.
  - Immediate goal: **Stage H.8 (documentation)** — done; **H.6** (separate
    install/registry/owner locks) done; **H.1** (UI polish) done; **H.3**
    (update-check opt-in) done; **H.4 closed** (watchdog-helper not shipped),
    **H.5 deferred** (secrets keychain). Done after that: the **code-review
    fixes** and the **refactoring plan** (rounds 1–5, recorded in ADR 82–88)
    — all stages complete, incl. `ProfileStorage`,
    `BlockGroupFilterModel`, `ModelInstallTransaction` (ADR 84/85/82) and the
    `AppController` split into `VerificationQueueController` +
    `ExportController` (ADR 86; the QML API is unchanged) and **round 5 — the
    output-parser layer** (`ParserOptions`, `IOutputParser::rebuildText`, the
    `auto` parser id, the data-driven label map and parse diagnostics, ADR 88;
    its stage-4 pipeline split is deferred as ADR 89). Next: the
    remaining roadmap items (see `docs/09-local-runtime-plan.md`).
  - **Model/task rework complete (Oct 2026)** — the staged program in
    `docs/20-model-task-rework.md` (stages 0–9, one stage = one commit) is
    done; ADR 162–166 in `docs/07-glossary.md` are the decision record. Six
    tasks replace the four roles — `pageOcr`/`rawPageOcr`/`blockRecognition`/
    `decision`/`layout`/`fix` — one vocabulary across `ConnectionRole`, the
    model profiles (`roles.<task>`), the settings
    (`tasks/<task>/{modelPath, mmprojPath, draftPath, requestProfileId,
    modelName}`, one-time migration from the old per-role keys; `check/`
    keeps only `showConfidence` — the automatic check/fix switches are gone,
    ADR 178) and one `RequestProfileStore` per task. Shipped model → task matrix:
    `unlimited-ocr` → pageOcr; `lfm25-vl-3b` → pageOcr + blockRecognition;
    `glm-ocr` → rawPageOcr + blockRecognition; `teleocr` → layout +
    blockRecognition; `clef-flash`, `d3-flash`/`d3-mini` → decision
    (`docs/21-d3.md`); `qwen3.5-4b/9b`,
    `gemma-4-e4b/12b` → fix (OK/FIX/REVIEW, ADR 180/181). Task defaults (ADR 169,
    `roles.<task>.default: true` in the profile JSONs, exactly one per
    task, read by the upcoming auto-selection): pageOcr = `unlimited-ocr`,
    rawPageOcr = `glm-ocr`, blockRecognition = `glm-ocr`,
    decision = `clef-flash`, layout = `teleocr`, fix = `qwen3.5-4b`.
    Block verification is two-layer
    (ADR 164: `box.text` + `box.correctedText`, independent verdicts; the
    amber re-recognized state is gone), pages carry a persisted recognition
    mode plus a computed verification aggregate (ADR 165), and the model
    settings live in **one window, «Модели»** (ADR 166 → superseded by
    ADR 168): six task rows (`task | model ▾ | quant ▾ | ⚙`), the quant
    dropdown listing every profile quantization with its curated size
    (`sizeBytes` in the profile JSONs, `scripts/fetch-quant-sizes.sh`
    refreshes them from HF) and a family hint, the per-task Launch/Request
    tabs behind each row's ⚙, a sequential install queue for missing quants,
    and a «Загруженные файлы моделей на диске» block (one row per installed
    entry: size, "used by N tasks", guarded delete via
    `ModelInstaller::removeInstalledEntry`); the separate all-models manager
    window is deleted. The rows live in the shared
    `Common/TaskModelsTable.qml` — also the heart of the wizard's hardware
    step — and the auto-selection the defaults feed is live (ADR 170):
    `ModelInstaller::defaultTaskChoices()` picks each task's role-default
    model and the best quantization the memory budget fits (0.9 × (VRAM ‖
    RAM), `runtime/HardwareInfo`), preferring already-installed quants.
    Recognizing a page is a
    split-button choice (pageOcr vs rawPageOcr), not a setting. A restart of
    the managed server brings back the role it was serving (stage-8
    regression `restartBringsBackTheStartedRole`).
  - **The request texts of a task live in their own profiles (ADR 182)** —
    `resources/profiles/requests/{fix-requests,decision-requests}.json`. The
    fix and decision tasks send the same requests whatever model serves them,
    so a model role only *names* the request profile
    (`"promptProfile": "fix-requests"`) and the model profiles keep the
    parameters (launch, request, `maxOutput`/`maxOutputByBlock`, the decision
    calibration, the routing rules). `ModelProfiles` loads the request
    profiles from `:/profiles/requests` and merges their texts into the role,
    so every existing lookup keeps working. The OCR models keep their prompts
    inline — their tasks really do differ per model.
  - **The decision thresholds are measured, per model, against the request that
    ships (ADR 184)** — a threshold describes one model's p(match) distribution
    under one request; rewrite the request and the measurement is void. The
    calibration needs no GUI: `scripts/decision-calibrate.py` prints the
    FN/FP(t) curve from the stored P(match) (ADR 173) and scores a profile's
    thresholds on a run (`--policy`), and
    `scripts/decision-eval/score-project.py` sends the app's own request over a
    corpus and writes the verdicts into a `.llocr` project — it reproduces the
    app's numbers bit for bit, which needs the state's keys **sorted** (the app
    builds it in a `QJsonObject`) and the formula in its canonical wire form.
    The stand's port of that normalization is pinned to the app's output and the
    run aborts if the two drift apart. The thresholds themselves are picked by
    `scripts/decision-eval/pick-thresholds.py` (the knee of each block type's
    curve: one caught error costs at most one false alarm); the rule reproduces
    d3-mini's shipped profile exactly on its own run, so it is a rule, not a
    tuning.
