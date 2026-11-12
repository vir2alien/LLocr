- # LLocr — Context for AI Agents

  > Read this file first. It describes the project and points to where details live.

  ## What it is

  **LLocr** is a cross-platform desktop application for OCR based on local LLMs.
  It runs a managed local llama.cpp server (or connects to an
  OpenAI-compatible endpoint) and recognizes text from images, PDFs and DjVu
  files through **four model tasks** — page markup (`layout`), block OCR
  (`blockRecognition`), visual-diff verification (`decision`) and AI
  correction (`fix`) — then exports the result. RAG is deferred (ADR 6), no
  code exists for it.

  ## Stack (short)

  - **Application:** Qt6 + C++ + QML, built with CMake (vcpkg declared in the
    `dev` preset, not used in the active build).
  - **PDF input:** Qt PDF module (`QPdfDocument`) — see ADR 7.
  - **DjVu input:** optional DjVuLibre C API (`DjVuDocument`), `.djvu` / `.djv`;
    see ADR 69 and `docs/06-dev-setup.md`. Without the dependency CMake
    warns and builds without DjVu (option `LLOCR_WITH_DJVU`); opening `.djvu`
    reports an actionable error at runtime. Windows needs an MSVC x64 build
    (`DJVULIBRE_ROOT`) and the matching DjVuLibre DLL alongside the executable
    or on PATH; do not assume a vcpkg port exists. `test_djvu_document` (and
    `test_app_import`) are built only when DjVu support is enabled.
  - **RAG service:** deferred (ADR 6) — a separate local Python service, not started.

  ## Documentation map

  All documents are bilingual: English primary, `*.ru.md` mirror. `README.md`
  indexes everything; here is the map by number:

  | File | Content |
  | --- | --- |
  | `docs/01-overview.md` | Idea, goals, requirements |
  | `docs/02-tech-stack.md` | Technologies and rationale |
  | `docs/03-architecture.md` | Architecture, layers, key abstractions |
  | `docs/04-components.md` | Component catalog (runtime, models, export, project files) |
  | `docs/05-roadmap.md` | Development plan by stages, open items |
  | `docs/06-dev-setup.md` | Tooling, build, CI/CD, distribution |
  | `docs/07-glossary.md` | Glossary and the ADR log — **all architectural decisions live here**, grouped by theme; removed decisions are listed in its appendix |
  | `docs/08-app-icon.md` | Cross-platform application icon |
  | `docs/09-tasks-and-verification.md` | The four tasks, the two-layer verification, the fix/read protocols, thresholds, block/page statuses |
  | `docs/10-external-runtime.md` | External runtime: per-task endpoints, secrets in the OS store |
  | `docs/11-auto-pipeline.md` | The auto pipeline (five stages, ADR 257) |
  | `docs/12-orientation-pipeline.md` | Page-orientation correction: two-stage pixel check, deskew, turn suggestions |
  | `docs/13-model-mineru.md` | Model manual: MinerU2.5-Pro (layout + blockRecognition; launch command, request cookbook) |
  | `docs/14-model-glm-ocr.md` | Model manual: GLM-OCR (blockRecognition) |
  | `docs/15-model-gemma-4.md` | Model manual: Gemma 4 E4B/12B QAT (fix; MTP drafter) |
  | `docs/16-model-qwen3.5.md` | Model manual: Qwen3.5-4B/9B (fix default; OK/FIX/REVIEW) |
  | `docs/17-model-minicpm-jev.md` | Model manual: MiniCPM-OCR Jev (decision; visual diff `/v1/systemone`) |
  | `docs/18-memory-estimate.md` | The VRAM requirement (ADR 191): formula, calibration of all nine models, measurement scripts |
  | `docs/19-testing.md` | The unit-test suite map and the evaluation stands |
  | `archive/README.md` | The release archive (ADR 251): the cut models' profiles (`archive/models/profiles/`), parsers, manuals (`archive/models/docs/`), the single-pass controller and the text-decision harness; the tag `archive/pre-cleanup` reaches every file the cut touched |

  ## Rules for the agent

  1. Do not change the base stack (Qt/C++/QML) without an explicit request.
  2. Follow the layered architecture (see `03-architecture.md`): the UI must not
     directly access network/files.
  3. New models are added via the **Settings** dialog (persisted in
     `QSettings`), not hardcoded.
  4. Build system — **CMake** (not qmake).
  5. When decisions change — update `07-glossary.md`.
  6. UI text and icon sizes — use `Theme.*Size` / `Theme.*` fonts from
     `src/qml/Theme.qml`; never hardcode a font or icon size in QML. The
     sizes are px, multiplied by the per-OS `uiScale` (ADR 293); the UI
     font family is the system stack set in `main.cpp` (ADR 293).
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
    The full map of the 60 test targets by domain is `docs/19-testing.md`.
  * The QML tree is a real module (`qt_add_qml_module`, ADR 101): new `.qml`
    files are globbed, `qmlcachegen` runs, and the files live at
    `qrc:/qt/qml/LLocr/…`. The C++ singletons are still registered by hand in
    `main.cpp`; the `QML_ELEMENT`/`QML_SINGLETON` macros were removed from the
    five classes that carried them (they would make `qmltyperegistrar` emit a
    default-constructor call for classes that have none) and come back with the
    `create()` factories. See `.qmllint.ini` for what the lint gates.
  * The C++ code is split into **layer targets** (ADR 106): `llocr_core` →
    `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` →
    `llocr_app` → `llocr`. A layer can only use what it links, and the tests
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

  ## Current status (short)

  The details live in the documents; this is the map.

  - **Stages 1/2/3 of the roadmap are complete** (OCR MVP, extensibility,
    PDF/formats); the **local-runtime plan** is complete (stages A–E, G-core,
    F, G-UI, H — install, downloads, wizard, models window, locks, secrets,
    docs; see `docs/04-components.md` §4.16–4.17 and the ADR log). The plan
    document itself was removed after completion. **RAG is deferred** (ADR 6).
  - **The model/task system** (`docs/09-tasks-and-verification.md`): four
    tasks — `layout`, `blockRecognition`, `decision`, `fix` (ADR 252 cut the
    single-pass `pageOcr`/`rawPageOcr`) — one vocabulary across
    `ConnectionRole`, the model profiles (`roles.<task>`), the settings
    (`tasks/<task>/…`; blockRecognition keeps the legacy `check` prefix for
    its launch files) and one `RequestProfileStore` per task. Nine shipped
    models: `mineru` (layout + blockRecognition, default for both), `glm-ocr`
    (blockRecognition), `minicpm-jev` (decision, visual diff), `qwen3.5-4b/9b`,
    `gemma-4-e4b/12b`, `qwen3-vl-4b/8b` (fix). Per-task defaults in
    `roles.<task>.default` (ADR 169). Ten removed models live in `archive/`.
  - **Verification and fix** (`docs/09-tasks-and-verification.md`):
    the decision model judges every queued block with the visual diff
    (`systemone_visual_diff_v1`, ADR 254); the fix task (OK/FIX/REVIEW over
    the `full_block` protocol, ADR 192) runs as a separate action over the
    problem blocks; the read task («Перераспознать», `read_block`, ADR 259–267)
    re-transcribes a block from the image alone — the recognized text never
    travels. Every block carries two text layers (`box.text` /
    `box.correctedText`, ADR 164); formulas carry their canonical form and a
    syntax verdict (`core/LatexText`, ADR 194–196); the frame carries the
    block state (ADR 238). Thresholds are measured, per model and block type
    (ADR 184/258). Reasoning budgets are global settings injected per request
    (ADR 193/268).
  - **Working end-to-end today:** images/PDF/DjVu import → markup and block
    recognition → verification → fix/read → export (TXT/MD/HTML/DOCX/PDF)
    with All/Current/range scope → self-contained `.llocr` projects (ADR 131);
    managed llama.cpp runtime install + model management in one «Модели»
    window (ADR 168); external mode with per-task endpoints and OS-store keys
    (`docs/10-external-runtime.md`); orientation correction
    (`docs/12-orientation-pipeline.md`); auto pipeline (`docs/11-auto-pipeline.md`).
  - **Memory estimate** (ADR 191): every quantization carries its minimum
    VRAM (`docs/18-memory-estimate.md`); the wizard auto-selects role-default
    models that fit 0.9 × (VRAM ‖ RAM).
  - **Tests:** 73 registered targets + 2 helpers (`mock_llama_server`,
    `candidate_render_dump`); the map and the evaluation stands are
    `docs/19-testing.md`. The stands in `OCRtest/` keep scripts, configs and
    result summaries; their bulk data (crops, renders, ground truth, verdict
    projects) was removed in the Oct-2026 cleanup — future runs use larger
    corpora (`OCRtest/tools/README.md`). `test_page_orientation` reads the
    corpus page renders from `tests/fixtures/orientation-corpus/`.
