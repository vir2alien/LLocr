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
  
  ## Rules for the agent
  
  1. Do not change the base stack (Qt/C++/QML) without an explicit request.
  2. Follow the layered architecture (see `03-architecture.md`): the UI must not
     directly access network/files.
  3. New models are added via the **Settings** dialog (persisted in
     `QSettings`), not hardcoded.
  4. Build system — **CMake** (not qmake).
  5. When decisions change — update `07-glossary.md`.
  6. UI text sizes — use `Theme.*Size` / `Theme.*` fonts from
     `resources/qml/Theme.qml`; never hardcode `font.pointSize` in QML. They
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
  see ADR 54. In Qt Creator pick the “Desktop Qt 6.10.3 MSVC2022 64bit” kit
  (generator `NMake Makefiles JOM`); the `.qtcreator` user config references
  this kit.
  
  ### Windows (MSVC 2022 64-bit) — concrete steps
  
  The Windows machine has Visual Studio 2022 (Community) and Qt
  `C:/Qt/6.10.3` with both `mingw_64` and `msvc2022_64` packages. **Use only
  `msvc2022_64`** — the MinGW package has no Qt WebEngine, so `find_package`
  fails and the app cannot build. ZLIB comes from vcpkg at `C:/vcpkg`
  (toolchain `x64-windows`, set in the Qt Creator kit environment — vcpkg
  participates only on Windows, not on the macOS build; see
  `THIRD_PARTY_NOTICES.md`).
  
  The ready-made directories `build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug`
  and `build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release` are configured with
  MSVC; a fresh agent should reuse them (run `cmake .` inside to re-generate,
  then build) instead of configuring from scratch:
  
  ```bat
  :: Start a “MSVC … x64 Developer Command Prompt” (or call vcvars64.bat),
  :: then for the Debug tree:
  cd build\Desktop_Qt_6_10_3_MSVC2022_64bit_Debug
  set PATH=C:\Qt\Tools\QtCreator\bin\jom;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
  cmake.exe .
  jom.exe -j 8          :: build llocr + tests
  :: Before running tests, put the Qt DLLs on PATH (the exes need them):
  set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
  ctest.exe --test-dir . -j 4
  ```
  
  From scratch (no existing tree):
  
  ```bat
  cmake -S . -B build/win-msvc2022 -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
  cmake --build build/win-msvc2022 -j 8
  ctest --test-dir build/win-msvc2022
  ```
  
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
  `PATH`) plus the local-runtime suite
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
    - **F** ✅ — first-run wizard: `SetupWizard.qml` + `Setup/Step{Welcome,
      Runtime,Model,Launch,Done}.qml`, trigger per §4.4 (Timer in Main.qml, no
      network probes), per-step gating; External path sets `setupVersion = 1`;
      Launch step uses the QML self-test bridge `runSelfTestQml()`.
    - **G-UI** ✅ — `Footer.qml` managed-runtime indicator (dot yellow/green/red
      + text, click opens the shared `ServerLogWindow`), Start/Stop toggle for
        the managed server (Managed mode only, `startServer()`/`stopServer()`),
        busy spinner (28 px, recognition + server startup)
      + stderr status text while StartingRuntime, §7.5 error surfacing for
        Failed, and
        the «Launch settings changed — restart» banner with a Restart button.
    - **H** 🔄 — H.2 ✅ (memory estimate + warning in the wizard Launch step,
      `ModelMemoryEstimator`), H.7 ✅ (waitForStarted 10s→5s, health 500→250ms,
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
    markers, **delete** and **drag-reorder** pages → **stop** a running job →
    edit the recognized text per page (per-page edits via `PageEditStore`) and
    toggle a **Markdown preview** (Qt WebEngine + marked + KaTeX) → **edit
    image/chart blocks** (move / resize / delete) directly on the preview →
    **export** to TXT / MD / HTML / DOCX (Pandoc) / PDF (Pandoc or built-in
    writer), with **All / Current / page-range** scope, **save** the whole
    working state to a self-contained **`.llocr`** project (pages + sources,
    recognized blocks, verification results, manual edits) and **open** it
    again (File menu; `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S`; ADR 131), and — in
    **Settings → Runtime** — install a local llama.cpp runtime, and in
    **Settings → Models** — install GGUF models from Hugging Face. That list is
    **one row per model** with the quantization picked on the row
    (`ModelQuantModel` + `Common/ModelDownloadList.qml`, ADR 122): «Use» /
    «Download» are one button, the ⋮ menu has Open folder / Delete quantization
    / Delete model, and the chosen quantization is remembered per profile
    (`models/quant/<id>`). The two-list UI (installed + preset catalog) and
    `InstalledModelsModel` are gone. A clean
    profile goes through the **first-run wizard** (SetupWizard) from scratch.
    The UI is localizable (System / English / Русский) and themed
    (System / Light / Dark).
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
