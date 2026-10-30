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
  | `archive/README.md`       | The release archive (ADR 251): the cut models' profiles (`archive/models/profiles/`), parsers, manuals (`archive/models/docs/` — UnlimitedOCR, 13-lfm25-vl-3b, 15-teleocr, 17-d1-verification-study, 18-clef-flash, 21-d3, 22-unlimited-ocr, 23-lightonocr, 25-paddleocr-vl, 28-chandra-ocr-2, 30-minicpm-ocr and their `.ru.md` mirrors), the single-pass controller and the text-decision harness; the tag `archive/pre-cleanup` reaches every file the cut touched |
  | `docs/16-gemma-4.md`      | Model manual: Gemma 4 E4B/12B QAT (MTP drafter in the model repo, image token budgets 70–1120, per-card flash-attn split); Russian mirror in `16-gemma-4.ru.md` |
  | `docs/19-glm-ocr.md`      | Model manual: GLM-OCR (trained task prefixes, no layout/coordinates, `raw` parser, glm4 metadata); Russian mirror in `19-glm-ocr.ru.md` |
  | `docs/20-model-task-rework.md` | Rework plan: the task vocabulary, two-layer verification, the fix pipeline (since ADR 192 the `full_block` protocol), page statuses, new settings windows (Oct 2026); re-stated to the four-task matrix (ADR 252), stage table with statuses |
  | `docs/24-mineru.md`       | Model manual: MinerU2.5-Pro-2605-1.2B (two-step protocol, 1036² squash, `--special`, layout absorption); Russian mirror in `24-mineru.ru.md` |
  | `docs/26-qwen3.5.md`      | Model manual: Qwen3.5-4B/9B, the fix default (OK/FIX/REVIEW, MTP drafting, framed candidate); Russian mirror in `26-qwen3.5.ru.md` |
  | `docs/27-memory-estimate.md` | The VRAM requirement (ADR 191): the formula, the profile's `memory` block, the calibration of all seven models, the measurement scripts (`scripts/measure-memory.sh`, `scripts/memory-from-log.py`), the traps and the checklist for a new model; Russian mirror in `27-memory-estimate.ru.md` |
  | `docs/29-external-runtime-tasks-and-secrets.md` | Plan + record: the external runtime gets the same four tasks as the managed one, each with its own endpoint, key and alias (ADR 226); the secrets live in the OS secret store (ADR 227) |
  | `docs/31-minicpm-jev.md`  | Model manual: MiniCPM-OCR Jev — the visual-diff decision (`systemone_visual_diff_v1`, two images, the CandidateRenderer render leg, thresholds pending calibration); Russian mirror in `31-minicpm-jev.ru.md` |
| `docs/32-orientation-pipeline.md` | The page-orientation correction pipeline: the two-stage pixel check (Sauvola, the skew sweep, periodicity + variation), the auto-straightening, the markup pass's rotation-token vote and the auto-turn with the second pass; Russian mirror in `32-orientation-pipeline.ru.md` |
  
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
  8. **Secrets never go into `QSettings`.** The API key and the Hugging Face
     token live in the OS secret store (`src/config/SecretStore.*`: the macOS
     Keychain, the Windows Credential Manager, the Linux Secret Service,
     ADR 227). Read them through `SecretStore`, write them through
     `SettingsStore::setApiKey` / `setApiKeyForRole`; a stored key is never
     shown back to the UI and never reaches a log, a command line or the
     project file. When the OS store refuses the write, the key lives in the
     memory until the app exits — say so, do not fall back to a file.
     On macOS the store binds the key to the code that wrote it, and the
     identity of an ad-hoc signed dev build is its code hash: **a rebuild
     loses the key**. Sign the dev build once with `scripts/macos-dev-sign.sh
     --create` (ADR 232); a failed read or write must reach the user, not
     stay an empty field.
     A store that **refuses** (the item belongs to another build) and a store
     that simply has **no key** are different answers: the refusal travels
     with the connection and the request does not go out, while a missing key
     is not a refusal — a server without auth needs none (ADR 233). The
     reason lives in `SecretStore::Status` and reaches the UI; it must not
     stay in the thread that read it.
  
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
    H.1–H.3, H.6–H.8 done, **H.4 closed** (no watchdog), **H.5 done** (the
    secrets live in the OS secret store, ADR 227).
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
      memory it needs (weights · projector · draft (with its own context) ·
      KV cache · working set · vision encoder · overhead) and the quant
      dropdown shows it (`VRAM ≥ 11.4 GiB`) instead of the parenthetical hint;
      the method, the calibration of every shipped model and the checklist for
      a new one live in `docs/27-memory-estimate.md`, the measurement scripts
      in `scripts/measure-memory.sh` + `scripts/memory-from-log.py`), H.7 ✅ (waitForStarted 10s→5s, health 500→250ms,
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
        **H.5 done** (the OS secret store — ADR 227).
  - Working end-to-end today: open image(s) **or PDF** → configure connection /
    model (incl. DRY sampling params) / output parser in **Settings** →
    recognize a page or **all** pages → browse pages (incl. **during**
    recognition) via a thumbnail strip with recognized / edited / duplicate
    markers, **delete** and **drag-reorder** pages → **zoom and pan the page
    preview** (the wheel scrolls, Ctrl/⌘+wheel zooms, the page drags; the
    selected block is scrolled clear of the block panel — ADR 175); the
    **block frame carries the state** (ADR 238): no frame = layout markup
    only, gray = recognized, red = verified with an error, green = verified
    without one (the error frame is 1 px heavier), and the model's confidence
    (when the option is on) is a bare number in the frame's color, without a
    badge → **stop** a running job →
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
    wizard's Model step keeps the `ModelDownloadList` list, ADR 122). The
    **external** runtime is configured in the same window: a shared server plus
    a row per task with its own endpoint, API key and model alias (ADR 226),
    and the keys live in the OS secret store, not in the settings file
    (ADR 227). In external mode the log window shows the client's own
    request journal — the endpoint, the model, the sizes, the status and the
    head of the reply (ADR 228; in memory, the key never enters it), the
    System One request names the model that answers (ADR 229), the external
    server's context window comes from `/v1/models` (ADR 231), and a server
    that ignores the reasoning budget gets the fix request once more without
    the thinking — with a note in the footer (ADR 230). A clean
    profile goes through the **first-run wizard** (SetupWizard) from scratch.
    The UI is localizable (System / English / Русский) and themed
    (System / Light / Dark).
  - **Verification and fix pipeline (ADR 145–147, 161–164, 172, 183; Oct 2026)** —
    verification runs in two stages: a fast **decision model** — MiniCPM-OCR
    Jev via `/v1/systemone`, the visual diff `systemone_visual_diff_v1`
    (ADR 254; the text protocol and its answer set are cut together with the
    models that spoke them — their manuals are in `archive/models/docs/`),
    the `decision` task in «Модели»
    judges every queued block. The request carries the crop, the candidate
    rendered back into an image and the candidate text; the noul head answers
    P(consistent) in one pass, and
    the verdict is routed two ways: `match ≥ threshold` → **Ok**,
    else **Mismatch** (a crop the model cannot read lands on **Review** in
    the fix task, which answers REVIEW for an unreadable, clipped or empty
    crop); thresholds resolve per block type (`matchThresholdByBlock` in the
    profile → role `matchThreshold` → the setting). A formula reaches the
    decision model in its **canonical form** — the block itself carries it
    from the moment it was recognized (`core/LatexText::canonical`, ADR 194),
    so the OCR models' spaced LaTeX does not read as noise, both tasks judge
    the same text and the wire carries the block's own bytes (the old
    `FixDiffGate::normalizeLatexCandidate` is gone). A **markdown code fence**
    inside a formula is carrier markup, not content: it is unwrapped when the
    formula lands, and a leftover backtick is a defect of its own —
    «блок кода внутри формулы» (ADR 195). A formula whose LaTeX
    **cannot be parsed** is rejected right after the recognition: the status
    is **«неверный синтаксис»** (red), the panel names what is broken, and
    the block is **excluded from the verification and from the fix** — it
    waits for a new recognition (ADR 194). The **carriers** of a formula are
    read as carriers: one equation split into several «$$…$$» fragments
    renders as several display blocks (one per line), and the invisible
    delimiter «\right.» / «\left.» pairs with any bracket — it is how a
    bracket is split across the lines of a formula (ADR 196). The syntax
    verdict is **derived from the text**: a project written before the rules
    changed is recomputed when it opens. d1-3B failed the task
    in every phrasing and was removed (ADR 161). Every block carries two text layers with independent verdicts
    (ADR 164): `box.text` (the OCR result) and `box.correctedText` (the fix
    model's correction); the frame and the confidence number follow the
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
    behaviour. The generation budget is computed inside the server's context
    window (`src/core/RequestBudget.h`): the answer's block budget plus the
    thinking, clipped so the request fits — the thinking yields before the
    answer, because a correction matters more than a thought; the window
    comes from the running server's `ctx-size`, the prompt's size is estimated
    (the text at two characters per token for Cyrillic and four for the rest,
    the image at the model's token floor), and a cut thinking is reported in
    the footer. Block recognition (plain re-transcription)
    entry points: per-block «Распознать», the page toolbar's «Распознать
    страницу ▾» (problem blocks / all blocks of the page); the header
    «Исправить» runs the fix task over the problem blocks of the whole
    document. Planned blocks (filter-enabled type or duplicate suspect,
    unanswered) show the gray frame (`boxVerificationPlanned`). Model manual:
    `docs/31-minicpm-jev.md` (the visual diff); the cut text-protocol manuals
    are in `archive/models/docs/`.
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
    **«Разметка»** in the panel (and the preview shows no frame) — the same word the page
    uses for itself, «Только разметка»; «Распознано» is only true for a block
    the OCR has read.
  - **A moved block goes back to markup (ADR 241, Oct 2026)** — moving or
    resizing a block (`onBoxRectChanged`) erases what belonged to the old
    region: the text, the verdict, the correction and the fix outcome
    (`BoxTransitions::blockRectChanged`). The block is markup again — the
    preview shows no frame, the panel says «Разметка» — and the page text
    follows the blocks. The drag calls it on every mouse move, so the page
    text is rebuilt once — when the block still carried something.
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
    task** (`roles.layout` in a model profile — MinerU is the default): the page
    goes to the layout model with the card's layout prompt, the reply's
    `<|box_start|>…<|ref_start|>` lines become **text-less** positioned
    blocks (parser `mineru-layout`), and the text is then filled in through
    blockRecognition — per-block «Распознать» or the
    «Распознать все блоки страницы» menu item (queues every NotChecked
    text-less box). The markup pass replaces the page's blocks (confirmation
    if the page has results); the page reads «Только разметка» until every
    text block has text (ADR 165). The layout model is configured as the
    `layout` task in «Модели» (`tasks/layout/*`,
    `ConnectionRole::Layout`, request profile `requestLayout.json`;
    deliberately absent from the setup wizard). The 1036² page squash mirrors
    the upstream calibration. Manual only — not wired into the automatic
    pipeline. Model manual: `docs/24-mineru.md` (the archive's TeleOCR
    carried the pass first — `archive/models/docs/15-teleocr.md`).
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
    header carries the **Различия** toggle, «Сбросить исправление» and the
    primary «Утвердить блок» / «Утвердить исправление» (ADR 185); the status
    word next to the block's name is gone — each card header carries the
    verdict and the score of **its own** layer, and the header repeats the
    word only when there is no card to carry it (a markup or an image block,
    ADR 239).
    The changed fragments are **tinted in place** — soft red in the original,
    soft green in the correction, computed word by word (`core/TextDiff.h`) — and
    a formula, table or code block is read in a monospace face (`Theme.mono`). `Ctrl`/
    `Cmd+Return` approves the block in view and jumps to the next problem
    block; «Изм.» turns the card into a field where the operator writes the
    text by hand (the version is approved and the page text follows).
    Icons: Lucide SVGs in `resources/icons/ui/` (qrc
    `/icons/ui`, `ui_icons` CMake resource) colorized by `Common/Icon.qml`.
  - Immediate goal: **Stage H.8 (documentation)** — done; **H.6** (separate
    install/registry/owner locks) done; **H.1** (UI polish) done; **H.3**
    (update-check opt-in) done; **H.4 closed** (watchdog-helper not shipped),
    **H.5 done** (the OS secret store, ADR 227). Done after that: the **code-review
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
    done; ADR 162–166 in `docs/07-glossary.md` are the decision record. Four
    tasks — `blockRecognition`/`decision`/`layout`/`fix` (ADR 252: the
    single-pass `pageOcr`/`rawPageOcr` are cut with every model that served
    them) — one vocabulary across `ConnectionRole`, the
    model profiles (`roles.<task>`), the settings
    (`tasks/<task>/{modelPath, mmprojPath, draftPath, requestProfileId,
    modelName}`; blockRecognition keeps the legacy `check` prefix for its
    launch files; `check/`
    keeps only `showConfidence` — the automatic check/fix switches are gone,
    ADR 178) and one `RequestProfileStore` per task. Shipped model → task matrix
    (seven models; the ten removed ones live in `archive/models/`, ADR 251):
    `mineru` → layout + blockRecognition (the default for both — one model
    closes the markup → recognition pipeline, ADR 252); `glm-ocr` →
    blockRecognition; `minicpm-jev` (visual diff,
    ADR 235/254) → decision (`docs/31-minicpm-jev.md`); `qwen3.5-4b/9b`,
    `gemma-4-e4b/12b` → fix (OK/FIX/REVIEW, ADR 180/181).
    Task defaults (ADR 169,
    `roles.<task>.default: true` in the profile JSONs, exactly one per
    task, read by the auto-selection): blockRecognition = `mineru`,
    decision = `minicpm-jev`, layout = `mineru`, fix = `qwen3.5-4b`.
    Block verification is two-layer
    (ADR 164: `box.text` + `box.correctedText`, independent verdicts; the
    amber re-recognized state is gone), pages carry a persisted recognition
    mode plus a computed verification aggregate (ADR 165), and the model
    settings live in **one window, «Модели»** (ADR 166 → superseded by
    ADR 168): four task rows (`task | model ▾ | quant ▾ | ⚙`), the quant
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
    Recognizing a page is the block stage of the markup flow — the
    single-pass page recognition is gone (ADR 252). A restart of
    the managed server brings back the role it was serving (stage-8
    regression `restartBringsBackTheStartedRole`).
  - **The request texts of a task live in their own profiles (ADR 182)** —
    `resources/profiles/requests/fix-requests.json` (the decision's
    `decision-requests.json` went to `archive/decision-text/` with the text
    protocol, ADR 254). The
    fix task sends the same requests whatever model serves it,
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
    calibration needs no GUI: `archive/decision-text/scripts/decision-calibrate.py`
    prints the
    FN/FP(t) curve from the stored P(match) (ADR 173) and scores a profile's
    thresholds on a run (`--policy`), and
    `archive/decision-text/scripts/decision-eval/score-project.py` sends the app's own request over a
    corpus and writes the verdicts into a `.llocr` project — it reproduces the
    app's numbers bit for bit, which needs the state's keys **sorted** (the app
    builds it in a `QJsonObject`) and the formula in its canonical wire form.
    The stand's port of that normalization is pinned to the app's output and the
    run aborts if the two drift apart. The thresholds themselves are picked by
    `archive/decision-text/scripts/decision-eval/pick-thresholds.py` (the knee of each block type's
    curve: one caught error costs at most one false alarm); the rule reproduced
    d3-mini's shipped profile exactly on its own run (the model and the harness
    are in the archive, ADR 251), so it is a rule, not a
    tuning.
