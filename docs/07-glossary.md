# 07. Glossary and Adopted Decisions

## Terms
- **OCR** — recognizing text from images.
- **Tasks** (ADR 252) —
  the four model tasks a profile declares by string id; the same ids name the
  profile files (`roles.<task>`), the enum (`ConnectionRole::BlockRecognition,
  Decision, Layout, Fix`), the settings and the QML. The list of the four ids
  lives in `core/ConnectionRole.h` and nowhere else (ADR 215).
  **blockRecognition** — single-block transcription (plain: the type prompt +
  the crop, no candidate — the previous text is not sent, ADR 163);
  **decision** — the fast yes/no gate on `/v1/systemone` (the visual diff
  `systemone_visual_diff_v1` is its only protocol, ADR 254/235);
  **layout** — the manual page-markup pass (ADR 150); **fix** — a universal
  model confirms / corrects / refuses the candidate (OK/FIX/REVIEW, ADR 163).
  Per-task settings live in
  `tasks/<task>/{modelPath, mmprojPath, draftPath, requestProfileId,
  modelName}` — blockRecognition's launch files keep the legacy `check`
  prefix; `check/` keeps only the behavior key `showConfidence` and
  `decision/matchThreshold` stays (the `autoCheck`/`autoFix` switches are
  gone, ADR 178). Every task has its own request-profile store (user files
  `requestBlock.json`, `requestDecision.json`, `requestLayout.json`,
  `requestFix.json`; legacy `request.json` / `requestValidate.json` are
  renamed on load; QML singletons `RequestProfilesBlock` …
  `RequestProfilesFix`). Model ↔ task assignment, downloads and management
  live in the «Модели» window. The legacy vocabulary survives where renaming
  would cost user data or churn: `CheckController`/`CheckRequest`/
  `CheckResult`, the queue's `Phase::Recheck`; the registry index renames
  the legacy `"check"` tag on load and drops every tag that is not a
  canonical role name — an entry left without a role waits for
  re-activation in Settings → Models (ADR 146); the unknown-name fallback
  to BlockRecognition lives in `connectionRoleFromName`, the settings/QML
  dispatch.
- **OCR model (adapter)** — the per-model recipe (`models/OcrModel.h`): how to
  talk to a specific OCR LLM — prompt variants, the default output parser, and
  (via the base class) the OpenAI-style chat-completions request body and
  response parsing. Created by `OcrModel::create(modelId, role)` from the
  task's model id (default recipe `mineru`); the adapters of the removed
  models are gone with them (ADR 251) — the surviving recipes are the model
  profiles in `resources/profiles/models/`.
- **ConnectionConfig** — the **transport** settings struct (base URL, API key,
  connection timeout, response timeout), formerly `ProviderConfig`. It is
  separate from the model settings: when a recognition starts,
  `RecognitionController` assembles an `OcrRequest`
  (model + prompt + request-body parameters) from `SettingsStore` and a
  `ConnectionConfig` (connection) for the transport.
- **LlamaClient** — the thin HTTP transport (`core/LlamaClient.h`): joins the
  `/v1/chat/completions` endpoint URL (`endpointUrl`), pre-flights the TCP
  connect against the connection timeout, POSTs JSON with an optional bearer
  token and a whole-request response timeout, supports `abort()`, and
  surfaces the server's error message. The former provider layer was reduced
  to exactly this (ADR 58, ADR 148).
- **OcrRequest** — the per-request payload: `image`, `prompt`, `modelId`, and
  the ordered request-body parameters (`RequestParameter`). Defined in
  `core/OcrRequest.h`.
- **DRY sampling parameters** — llama.cpp's "Don't Repeat Yourself" sampler
  settings (`dry_multiplier`, `dry_base`, `dry_allowed_length`,
  `dry_penalty_last_n`), part of the **request profile** and sent in the request
  body. Tuned originally for the Unlimited-OCR model (now in the archive,
  ADR 251).
- **Output parser** — the strategy for parsing a model's response into a
  structured result (`raw`, the layout markup parser `mineru-layout`; default
  `auto`), selected in Settings → Output. The parsers that existed only for
  the removed models live in `archive/parsers/` (ADR 251).
- **BlockStyle** — mapping from model block labels (`title`, `image`, `chart`,
  `equation`, `table`, `ref_text`, captions, …) to Markdown rendering styles
  (`BlockStyle.h`).
- **bbox** — bounding box coordinates of a recognized fragment for overlay on
  the preview (produced by the model's own parser).
- **Block status** — the per-box verification state, shown as the dot to the
  left of the box (ADR 164, two layers): every block carries `box.text` (the
  OCR result) and `box.correctedText` (the fix model's correction); the dot
  reflects the **effective** layer (the correction when present, else the
  text) and answers one question — is the text you are looking at verified?
  A markup block without text shows no dot; **gray** = recognized, no verdict
  on that layer (including «исправлено» — a stored correction without its own
  verdict yet); **green** = the decision model confirmed it (Ok);
  **red** = rejected (Mismatch) or unreadable/damaged (Review). The amber
  re-recognized state is removed (`BoxCheckStatus::ReRecognized` deleted
  without renumbering; superseded ADR 153's two-variant semantics). The
  confidence badge follows the same layer. The box hue follows the block
  kind, not the status: text/table/formula/image each have their own fill +
  frame color.
- **Image/chart block** — a box labeled `image` or `chart`;
  editable on the preview (move / resize / delete) and exported as a cropped
  image.
- **Markdown preview** — client-side render of the current page's Markdown via
  Qt WebEngine + marked + KaTeX, toggled in the right text pane.
- **RAG** — Retrieval-Augmented Generation; here — indexing scans into a vector
  DB. Deferred (ADR 6), no code exists.
- **Document / page model** — `DocumentModel` holds pages; `PageListModel` feeds
  the thumbnail strip (recognized/edited/duplicate/current flags, no boxes).
- **Recognized marker** — per-page indicator showing whether a page has been
  OCR'd yet; edited pages and pages with duplicate boxes carry additional
  markers.
- **Connection mode** — `External` (connect to an existing OpenAI-compatible
  server; the app launches nothing) or `Managed` (the app starts and owns a
  local `llama-server`). Chosen by the first-run wizard; existing profiles
  default to `External`.
- **ResolvedConnection** — the ready-to-use connection returned by
  `RuntimeController::ensureConnectionReady()`: `baseUrl` + `apiKey` +
  `modelId` + the two timeouts (ADR 148). In `External` it mirrors settings;
  in `Managed` it is computed from the started server (loopback URL +
  `--alias`).
- **RuntimeController** — the managed-runtime facade (QML singleton `Runtime`,
  created once in `main.cpp`, ADR 36): owns connection resolution, server
  lifecycle, self-test, memory estimation; QML must not instantiate it.
- **AppBusyState** — the app-wide exclusive busy state: `Idle`,
  `StartingRuntime`, `Recognizing`, `StoppingRuntime`, `Downloading`,
  `Installing`.
- **RuntimeState** — the managed-server state machine: `NotConfigured → Stopped
  → Starting → Ready → Stopping → Stopped`, with `Failed`;
  drives `canRecognize` with `configValid`.
- **ensureConnectionReady()** — the single async entry point that recognition
  uses; External resolves immediately, Managed starts the server, waits for
  `/health`, queries `/v1/models` and verifies the alias. Callback-based
  (review 3.3): concurrent callers register a callback and share one in-flight
  resolve (dedup, ADR 37).
- **runSelfTest() / runSelfTestQml()** — an independent end-to-end check
  (start → health → `/v1/models` → one OCR request with a built-in test image),
  used by the wizard's Launch step.
- **alias** — the `--alias <name>` given to the managed `llama-server`; it
  becomes the `modelId` clients use (`/v1/models` must report it).
- **Managed / External (runtime)** — see **Connection mode**. “Managed
  runtime/model” also marks files inside the app's `models/` dir (removable),
  vs **external** user-provided GGUF files (never deleted, only listed).
- **Model preset** — a downloadable entry `repo + revision + model/mmproj/mtp
  files + sha256` expanded from a model profile's quantization list
  (`ModelPresetCatalog::expand`, one preset per quant; e.g. MinerU Q8_0),
  merged with the user's `catalog.json` by `id` (ADR 42/43). The preset
  installs the files of its `profileId` — the profile owns the role, the
  launch and the parser (ADR 58).
- **Request profile** — the ordered set of request-body parameters sent to
  llama.cpp next to `model`/`messages` (DRY params, temperature, max_tokens,
  stream, …). Built-ins are expanded from the model profiles serving the task
  (`ModelProfiles::requestWithMaxOutput`); per-task user copies live in
  `<AppData>/LLocr/profiles/requestBlock.json`, `requestDecision.json`,
  `requestLayout.json`, `requestFix.json`, merged by `id` (ADR 56); the
  **active profile follows the task's model choice** (blockRecognition:
  `model/recipeId`; decision/layout/fix: `tasks/<task>/requestProfileId`) —
  unknown ids fall back to the first built-in profile (ADR 59). One
  `RequestProfileStore` per task (QML singletons `RequestProfilesBlock` …
  `RequestProfilesFix`) owns them.
- **Launch profile** — a named llama-server command-line preset (ctx-size,
  n-gpu-layers, flags, …) tagged with `os`/`backend`. Built-in presets ship in
  `:/profiles/serverLaunch.json`; per-preset user copies live in
  `<AppData>/LLocr/profiles/serverLaunch.json` (full copies, so user deletions
  stick). The selection (`launch/profileId`) auto-follows the installed
  runtime backend (ADR 57); `LaunchProfileStore` (QML singleton
  `LaunchProfiles`) owns it. `--model/--mmproj/--alias/--host/--port` are
  never part of a profile.
- **ModelRegistry** — persists installed models in `<modelsDir>/index.json`;
  distinguishes managed vs external, recovers by rescan, guards removal.
- **GGUF** — llama.cpp's model format; vision models consist of a main
  `.gguf` (+ optional `mmproj` projector; multi-part `model-00001-of-N` files
  are supported).

## Adopted decisions (ADR-lite)

The log is grouped by theme. Every entry keeps its original ADR number, so
references from code and documents stay valid; small related decisions are
merged into one entry and name the numbers they fold. Decisions that were
fully superseded or removed together with their feature are listed in the
appendix at the end of this file.

### Stack & build

- **1.** The app is Qt6/C++/QML — the stack covers all GUI requirements.
- **2.** The build system is CMake, not qmake — the standard for cross-platform C++.
- **4.** The transport speaks the OpenAI-compatible chat-completions API
  (`/v1/chat/completions`) — the one connection type, supported by virtually
  all local runners.
- **5.** The internal document format is Markdown; export converges on
  Markdown as the single source — DOCX via Pandoc, rendered HTML/PDF via the
  preview pipeline (see ADR 63).
- **6.** RAG (indexing scans into a vector DB, first planned as a separate
  Python service) is deferred, not started — OCR first; only the backend RAG
  interface was sketched, no RAG code exists. (Merges ADR 3.)
- **7.** PDF input uses Qt PDF (`QPdfDocument`), not MuPDF/Poppler — no extra
  native dependency, bundled with Qt6, adequate at ~150 DPI.
- **14.** Unit tests live in `tests/` (Qt Test), gated by `LLOCR_BUILD_TESTS`.
- **50.** `SettingsStore::resetToDefaults()` is table-driven: one
  `SettingDefault{kQSettingsKey, Q_PROPERTY name, QVariant default}` row per
  resettable key, written through `QMetaProperty::write()` so guards,
  validation and NOTIFY are preserved; `ui/window*` state is deliberately
  excluded and the order keeps `baseUrl` before `connectionMode`.
- **54.** The Windows toolchain is MSVC 2022 64-bit (NMake Makefiles JOM);
  MinGW is not supported because the Qt MinGW package lacks Qt WebEngine (the
  Markdown preview) — a MinGW-configured build fails cleanly on WebEngine;
  macOS/Linux are unaffected.
- **58.** The provider abstraction is removed (`ILlmProvider`/`OpenAiProvider`
  deleted). Two orthogonal abstractions remain: `OcrModel` (src/models/) — the
  per-OCR-LLM recipe (prompt variants, default parser, request building and
  response parsing) — and `LlamaClient` (src/core/) — the thin transport;
  `ProviderConfig` is renamed `ConnectionConfig`, `OcrRequest` lives in
  `core/OcrRequest.h`, and the prompt belongs to the model adapter, not the
  controller.
- **66.** DjVu import is staged background preparation:
  `QtConcurrent::run` runs static `DocumentModel::prepareDjVu()` with an
  independent shared decoder, a GUI-owned `QFutureWatcher` commits
  `PreparedDjVu` under a short `m_documentLock`, mixed file selections run
  sequentially, and a separate `importing` state drives the spinner and gates
  overlapping operations without touching recognition busy.
- **69.** DjVu support is optional at configure time (`LLOCR_WITH_DJVU`,
  default ON): without DjVuLibre the build warns and continues with
  `LLOCR_DJVU_AVAILABLE=OFF`, DjVu code compiles behind `LLOCR_HAVE_DJVU`, and
  runtime calls return an actionable error (install DjVuLibre and rebuild, or
  convert to PDF); `test_djvu_document`/`test_app_import` build only when DjVu
  is enabled.
- **84.** The profile stores share one JSON persistence layer, `ProfileStorage`
  (`removeFileIfExists` / `writeJsonAtomic` / `readJson`), each store keeping
  its own `tr()` texts and JSON shape; a `ProfileStoreBase` template was
  deliberately rejected; `test_profile_storage` covers the helpers directly.
- **86.** `AppController` was split: `VerificationQueueController` owns the
  verification queue state machine (crosses the boundary via a
  `blockChecked(page, box, result)` signal and never mutates the document) and
  `ExportController` owns the whole export pipeline (`collectPages`,
  `ExportScope`, pandoc/built-in and rendered HTML/PDF paths,
  `pdfPageLayout`); the `AppController` QML API stayed unchanged as
  forwarders. (The queue state machine's owner is amended by ADR 286: the
  role queues replaced `VerificationQueueController`; the
  results-as-signals rule is unchanged.)
- **92.** Block deletion has exactly one entry point,
  `AppController::removeBlock(index)`: the document is mutated first, then
  `BoxListModel` (plain `removeBox()`, not Q_INVOKABLE, no `boxRemoved`
  signal), the `PageEditStore` and the selection are mirrored, then signals
  fire; QML (`ImagePreview`, `BlockEditPanel`) calls the controller, never the
  model. Tests: `TestAppImport::blockRemovalUpdatesDocumentAndView`,
  `TestBoxModel::removeBoxIsNotQmlCallable`.
- **95.** An optional-dependency define must not change a class layout:
  `DocumentModel.h` includes `app/DjVuDocument.h` unconditionally (it only
  forward-declares the `ddjvu_*` structs) and declares the DjVu members
  unconditionally; `LLOCR_HAVE_DJVU` selects only the implementation in the
  .cpp — wrapping members in `#ifdef` is an ODR violation and silent memory
  corruption.
- **101.** The QML tree is a real Qt module (`qt_add_qml_module`, URI `LLocr`,
  files globbed with `CONFIGURE_DEPENDS`, served at `qrc:/qt/qml/LLocr/…`,
  which `main.cpp` loads); QML singletons carry `QT_QML_SINGLETON_TYPE`, the
  inert `QML_ELEMENT` macros were removed, and the C++ services stay
  hand-registered in `main.cpp`. The module is now declared beside the `llocr`
  target with the files in `src/qml/` (see ADR 176).
- **103.** The page owns its text: all writes go through
  `AppController::setPageText()` and all reads through `pageText()`, while
  `PageEditStore` keeps only the revert baseline and the "edited" marker.
  Re-typing the visible text is a no-op, revert restores the recognized text,
  and `ExportController` reads `page.result.text` like everyone else.
- **106.** The code is split into layer targets linked in order `llocr_core` →
  `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` →
  `llocr_app` → `llocr`; what a layer may use is decided by the link graph
  (`LaunchProfileStore`/`LaunchParametersModel` sit in `llocr_runtime`,
  `RequestProfileStore` in config, `ProcessGuard_{win,mac,linux}.cpp` on
  runtime). The tests link the same targets instead of re-listing sources.
- **117.** One `ProfileStore<T>` template (`app/ProfileStore.h`) owns the
  built-in list, user overrides, merge-on-read and diff-only save for
  `LaunchProfileStore`/`RequestProfileStore`, with the single `schemaVersion`
  envelope policy (`ProfileStorage::readEnvelope`/`writeEnvelope`). The merge
  policy stays on each type (`LaunchProfile::merge` replaces the parameter
  set, `RequestProfile::merge` merges parameter-wise), and the dead
  `launch/presetId` is gone. `VerificationPromptStore` deliberately keeps its
  own shape but uses the same envelope.
- **118.** GUI-thread discipline (merges ADRs 104, 105, 209): the document's
  threading contract is written down — `DocumentModel`/`AppController` are
  GUI-thread resident with two deliberate crossings (the export worker
  resolving crops under the document lock, the image provider), callers go
  through the locking accessor, and the lock itself stays. Nothing that walks
  a file or waits for a process runs on the GUI thread — runtime probing
  (`RuntimeLocator::probeCached` on a worker), resumed-download hashing and
  projector digests all run on workers. The preview page render happens on a
  worker too: `DocumentModel::renderDetached` + `AppController::previewImage`
  answer from a cache with a generation counter and a «Rendering page…» label.
  A document handle belongs to the thread that opened it: `QPdfDocument` and
  the DjVuLibre context carry their creator's thread affinity, the wrappers
  know their owner, a worker thread keeps its own per-document handle for
  rendering, and shared provider state is guarded (thumbnail cache by its own
  mutex, document by the document lock).
- **131.** A project is a self-contained `.llocr` ZIP: `project.json` (schema
  version 1: sources + ordered pages with text, baseline, boxes,
  `checkStatus`/`correctedText`) plus the embedded original sources.
  `runtime/ZipWriter` writes it, `app/ProjectStore` owns the schema, save is
  GUI snapshot + worker write (atomic tmp+rename) and open extracts into a
  session `QTemporaryDir`, then rebuilds the document through the public
  `DocumentModel` API. App-level settings are not project state.
- **176.** The QML module is declared beside the `llocr` target and the QML
  tree lives in `src/qml` — the root-declared module mis-registered the
  metatypes chain per Windows generator, and from `src/` qmlcachegen no longer
  produces the `llocr_../…` trailing-dot path Win32 can neither create nor
  read. `CMakePresets.json` `dev-win` switched to Ninja; from-scratch builds
  pass with both generators.
- **204.** The project builds with `-Wall -Wextra` (`/W4` on MSVC, the three
  Qt-header warnings silenced), and the warnings it surfaced were fixed. A
  missed `switch` case or a signed/unsigned mix-up must not pass silently —
  this is what made the ten role switches of the settings store visible
  (review finding D-020).
- **205.** The orphan QML files `Common/ModelDownloadList.qml`,
  `Common/InstallModelDialog.qml` and `Common/SplitToolButton.qml` are deleted
  with their translation contexts; the models list lives in
  `Common/TaskModelsTable.qml` and the runtime builds list in
  `Common/RuntimeBuildsList.qml`, where the row pattern ADR 122 and ADR 158
  describe lives on.
- **210.** The model-download group holds `QPointer`s instead of a raw pointer
  to a task (the manager evicts a task once ten pile up) and remembers the
  bytes of finished tasks, so group progress does not fall back when the
  manager evicts them.
- **213.** `AppController` is a facade, not a god class: five of its eight
  property domains became classes in `src/app/` — `BlockInspector` (selected
  block, two text versions with verdicts, diff, page-text position),
  `ProjectController` (project file), `AvailabilityPresenter` (availability
  matrix), `VerificationQueueController` (already owned its state),
  `LayoutController` (layout pass). QML-bound property names and NOTIFY
  signals are unchanged, the new classes take dependencies through `Deps`, and
  document/import domains, parser choice and the status line stay in the
  facade. (The verification domain's classes are amended by ADR 286: the
  role queues replaced `VerificationQueueController`.)
- **214.** The panel's scope list (thumbnail selection; empty selection =
  whole document) is computed by `AvailabilityPresenter` alone, and the
  facade asks it for both the actions (`recognizeScopePages`,
  `checkScopeEnabledBlocks`, …) and the availability flags — the page count on
  a button and the flags cannot diverge.
- **247.** Every MSVC build warning is triaged: foreign noise silenced (C4702
  from Qt templates joins the three silenced Qt-header warnings), own warnings
  fixed. Standing rule: a `switch` over an `enum class` in a function with a
  return value keeps a `default` branch — the enum's storage range is wider
  than the enumerator list, so MSVC never sees the switch as exhaustive;
  clang and gcc do not check this.

### Runtime & install

- **12.** Only OpenAI-compatible (llama.cpp-style) servers are supported — one
  clear connection type; connection, model, parser and request-body
  parameters live in `SettingsStore` (QSettings behind the settings windows),
  and the prompt belongs to the OCR model adapter (see ADR 58).
- **26.** Two connection modes, `External` (connect to an existing
  OpenAI-compatible server; the app launches nothing) and `Managed` (the app
  starts and owns a local `llama-server`); the transport stays decoupled from
  process management (`LlamaClient` + `OcrModel`, no `QProcess` in the
  transport).
- **27.** The managed server is llama.cpp only (`llama-server`, minimum build
  b4000) — one predictable CLI; Ollama/LM Studio have their own managers, and
  older builds drift too much.
- **28.** Runtime installs come from llama.cpp GitHub Releases
  (`ggml-org/llama.cpp`), unpacked by our own code (vendored `miniz`) with
  `sha256` verification — the digest source per ADR 236; file size is not an
  integrity proof.
- **29.** Models download directly from Hugging Face with the commit SHA
  pinned and `sha256` verified against `lfs.oid` — no Python dependency, and
  `main` is mutable between browsing and download.
- **30.** No-orphan guarantee for the managed server: strong on Windows (Job
  Object + `JOB_OBJECT_LIMIT_KILL_ON_CLOSE`) and Linux
  (`prctl(PR_SET_PDEATHSIG)` via `ProcessGuard`), best-effort on macOS
  (`owner.json` + next-start detection) — macOS has no PDEATHSIG analog.
- **31.** The first-run wizard is driven purely by `runtime/setupVersion`, with
  no network health probes; pre-existing profiles count as configured
  (`setupVersion=1`), a clean profile starts at `setupVersion=0` — an
  unreachable server must not decide setup state.
- **32.** In `Managed`, `baseUrl` and `modelId` are computed from the running
  server (loopback + `--alias`) — the process is the single source of truth —
  and the `model/name` setting is never overwritten, so `External` settings
  survive mode switches.
- **34.** Both `.zip` and `.tar.gz` runtime archives are supported, dispatched
  by extension in `ArchiveExtractor::extractArchive` (llama.cpp publishes ZIP
  on Windows but tar.gz on macOS/Linux, and no ZIP asset at all on macOS);
  macOS tarballs' symlinked `.dylib`s are recreated as relative links.
- **36.** `RuntimeController` is a singleton created in `main.cpp` before the
  QML engine loads; QML cannot instantiate it (deterministic lifetime, no
  duplicate instances).
- **37.** `RecognitionController` obtains the connection only through
  `ensureConnectionReady()` (External resolves immediately; Managed
  starts/waits later) — it knows nothing about modes, processes or health
  checks; concurrent callers share one in-flight resolve (dedup).
- **39.** Runtime installation is transactional: staging → verify → probe →
  atomic rename → settings commit; any failure removes `staging/<uuid>` and
  leaves settings untouched (see ADR 28, ADR 34).
- **40.** `.part` downloads always live in the target directory, never a
  shared `downloads/` — a rename across filesystems is not atomic, and models
  often live on an external drive.
- **41.** Server capability detection = build allowlist + parsed `--help`
  refinement + one auto-retry without an offending flag on "unknown argument",
  cached as `capabilities-<sha1(path+mtime)>.json` — llama.cpp's CLI drifts
  between builds and blind argv building breaks compatibility.
- **44.** `runtime/autoStart` defaults to off — otherwise the GUI would
  reserve several GB of RAM/VRAM on every launch, even when idle.
- **46.** Locks are split instead of one global app lock: `.install.lock`
  (runtime installs/cleanup), per-write `.registry.lock` (`ModelRegistry`),
  and `.instance.lock` as the runtime-owner gate (`SingleInstanceGuard`); the
  owner lock is re-checked when the Runtime settings open
  (`RuntimeController::refreshSingleInstanceLock()`), so a second instance
  takes over as soon as the owner exits — no app restart needed.
- **49.** Self-test and the server-log ring-buffer are separate singletons
  (`SelfTestController`, `RuntimeLog`), not `RuntimeController` roles — the
  facade stays small and self-test/log logic is not duplicated.
- **51.** The `--version` + `--help` probe shares one wall-clock budget:
  `--version` gets the full `timeoutMs`, `--help` the remainder; on
  exhaustion `--help` is skipped and flags fall back to the build allowlist —
  a hung binary can no longer stall the UI for 2 × timeoutMs.
- **52.** Successful probes persist to
  `<cacheDir>/capabilities-<sha1(path+mtime+size)>.json` and are served by
  `probeCached()` on the next app run (key strength matches the in-memory
  `ProbeKey`, so any binary replacement invalidates); a fresh "Check" probe
  and the install path stay uncached by design (see ADR 41).
- **53.** Probing llama.cpp binaries uses a single patient 120 s timeout
  (install flow and `RuntimeController` Check/Start), with no kill/retry — the
  first run on macOS compiles Metal shaders and blocks `--version`; killing
  each attempt at 5 s restarts the compile forever, while one patient run
  warms the cache.
- **57.** Launch profiles replace the flat `launch/*` settings:
  `ServerLaunchConfig` emits the core flags
  (`--model/--mmproj/--alias/--host/--port`, single-sourced) then the active
  profile's rows verbatim; built-in presets in `:/profiles/serverLaunch.json`
  tagged `os`/`backend`, per-preset full user copies in
  `<AppData>/LLocr/profiles/serverLaunch.json` (deletions stick); the
  selection `launch/profileId` auto-follows the installed backend;
  `--model/--mmproj/--alias/--host/--port` are never part of a profile, and
  `estimateModelMemory` reads `ctx-size`/`cache-type-*` from the active
  profile.
- **60.** Installed llama.cpp builds are switchable without a re-download:
  `InstallTransaction::scanInstalledBuilds()` enumerates
  `<runtimeDir>/llama.cpp-*`, `RuntimeInstaller::activateBuild(index)`
  activates one with a settings-only commit (`serverPath` + `installedBuild` +
  `runtimeBackend`); "Clean up unused builds" is refused while no build is
  active.
- **61.** A running managed server is the source of truth for recognition:
  `ensureConnectionReady()` applies the settings-based `configValid` gate only
  when a start would be needed — an already Ready/Starting server resolves
  straight to the `/v1/models` alias check; in exchange `startServer()`
  refuses a Managed start without a selected model, so a model-less Ready
  server cannot arise.
- **62.** Model selection survives a settings reset without a re-download:
  `ModelInstaller` reads the registry through fresh `RuntimePaths`, exposes
  `refreshInstalled()` (a rescan stays manual — it would drop external entries
  not confirmed by files) and `activatePreset(index)`; the wizard's Model step
  and the Models window offer installed-model activation, and
  start/recognition refusals name the recorded model path.
- **73.** External-server model names are per role: resolution puts the role's
  model name into `ResolvedConnection.modelId` (recognition vs the
  verifier/decision task), and an empty name is sent as-is — llama-server
  ignores the request's `model` field, and a multi-model server's "model not
  found" is surfaced verbatim, with no silent fallback.
- **74.** The single managed llama-server is (re)loaded per task:
  `ensureConnectionReady(role)` compares the model/mmproj the running server
  was started with against the role's settings and stops/restarts on mismatch
  (`beginRoleSwitch()`); the shared alias stays `launch/modelAlias`;
  recognition and verification are mutually exclusive at the `AppController`
  level, so a role switch can never kill a running task; when the role's model
  path is empty, the live server wins (see ADR 61).
- **83.** Settings-wipe guard: a QML step that saves on hide must save only
  after a show loaded the fields (`StepExternal.valuesLoaded` — eagerly
  created invisible items fire `onVisibleChanged` before any load), and
  `applyStartupMigration()` self-heals an external profile wiped at startup by
  restoring the mode-switch stash (`provider/lastExternalBaseUrl`); regression
  tests in `test_settings_store`.
- **85.** The HF preset install pipeline (prepare → download → finalize) lives
  in `runtime/ModelInstallTransaction.{h,cpp}` (`InstallPlan`,
  `DownloadManager`/`DownloadGroup` ownership, prepare generation gate,
  complete/cancel), mirroring `RuntimeInstaller`/`InstallTransaction`;
  `ModelInstaller` is the QML façade only with its public API unchanged; the
  pure selection logic (`repoDirName`, `selectModelFiles`) is public and
  unit-tested in `test_model_install_transaction` (group-size ties break by
  key, not QHash seed).
- **93.** `RuntimeController` resolves are keyed by `ConnectionRole`: only
  same-role requests join an in-flight batch, another role goes to
  `m_deferredResolves` and is promoted by `startResolveForRole(role)` (which
  performs the role switch, see ADR 74); `cancelPendingStart()` drops the
  deferred batch first, and `completeResolve()` never clobbers a started
  resolve. Test:
  `TestEnsureConnection::crossRoleRequestDuringResolveIsNotAnsweredWithTheWrongRole`.
- **97.** The model index is updated read-modify-write under its own lock:
  `ModelRegistry::update(modelsDir, mutate, error)` holds `.registry.lock`
  across read and write, and `save()` is lock + `writeIndex` — two instances
  installing concurrently can no longer drop each other's entries.
- **98.** A failed release-catalog fetch (timeout / 403 / non-200 / malformed
  body) never drops the offline cache: `fetchReleasesLocal()` falls back to
  the last known good list via the age-agnostic `readCacheFile()`, with a
  watchdog timer on the nested event loop and an injectable endpoint so the
  policy is testable. `ReleaseCatalog::resetCache()` is gone.
- **107.** The llama-server owner record is read back: `ServerOwner` validates
  `<rootDir>/owner.json` (pid alive + image path match, parent-gone as
  evidence where the platform cannot report the image) and `RuntimeController`
  exposes `orphanDetected`/`orphanInfo`/`terminateOrphan()` with a footer «Stop
  it» — the process is never killed without a click.
  `DownloadGroup::progress()` sums over the tasks it enqueued, not the
  manager's aggregate.
- **108.** One HTTP policy for the runtime layer: `HttpClient` owns the
  transfer timeout, manual redirects with a hop budget, https-only (explicitly
  allowed loopback excepted), the system proxy, and the rule that the
  `Authorization` header never follows a redirect to another host.
  `ModelCatalog`, `ReleaseCatalog`, `DownloadTask` (its own resume logic) and
  `RuntimeController`'s `/v1/models` call all go through it.
- **109.** One `InstalledState` answers where the runtime and its install lock
  live: a single instance created in `main.cpp` is handed to
  `RuntimeController`, `RuntimeInstaller`, `ModelInstaller` and
  `ModelInstallTransaction`, derives `RuntimePaths` from the settings on every
  call and recreates the `QLockFile` when the directory moves (`pathsChanged`
  → installed-builds rescan).
- **112b.** Both installs publish by atomic rename through `StagedInstall`
  (runtime/StagedInstall.{h,cpp}): staging → `commit()` (move aside, rename
  into place, drop backup) → destructor removes uncommitted staging.
  `ModelInstallTransaction` takes the same `.install.lock` as the runtime
  install, downloads into `<staging>/model-<org>__<repo>` and commits into
  `<modelsDir>/<org>__<repo>`; staging names are deterministic so a resumed
  install finds its `.part` files. (The number 112 was duplicated in the
  original log; this entry carries the second decision.)
- **116.** One reconciliation decides between the settings, `index.json` and
  the filesystem: `reconcileInstalled()` (runtime/InstalledReconcile.{h,cpp})
  states the priority — existence and measurable file facts are the
  filesystem's, curated metadata is the index's, the settings are pointers
  reported, never silently dropped — and treats an absent models directory as
  unknown, not empty. `ModelRegistry::load()` has one path instead of four
  rescan branches.
- **126.** `ctx-size` belongs to the machine, not the weights: it lives in the
  platform profiles of `serverLaunch.json` per os+backend (16384 default, 8192
  for `cpu`), turned in Settings → Launch, while per-model launch parameters
  stay what the weights decide (`image-min/max-tokens`, `dry-sequence-breaker`,
  `special`). `compose()` fills gaps by name so no parameter reaches the
  command line twice; `n-predict` was later removed from the launch layer
  entirely (see ADR 144).
- **127.** A build tag is a number: `ServerCapabilities::buildAtLeast()` parses
  `minBuild` comparisons numerically (a `QString::compare` refused every
  five-digit build as older than a four-digit one), and
  `ModelInstaller::syncProfileToActiveModel()` re-derives the stored profile
  from the active entry's repo on every `refreshInstalled()` so a renamed or
  moved model cannot strand a stale selection.
- **128.** The manual-path options are gone: managed means the app-installed
  runtime build (`Settings.serverPath` is written only by the
  installer/`activateBuild`) and a model means one from the catalog — the
  binary/model pickers, the «Specify model files» source flags and wizard
  `StepBinary.qml` are deleted, with orphaned settings keys dropped at
  startup. A user who had chosen their own binary keeps working: `serverPath`
  is still what `startServer()` launches.
- **133.** Stop stops the job, not the server: `CheckController::stop()` no
  longer calls `RuntimeController::cancelPendingStart()` — the managed server
  is app-wide state with its own explicit control (footer toggle, Settings →
  Runtime). `RecognitionController::stop()` keeps the courtesy, since
  recognition's start exists only to serve that request.
- **148.** The provider timeout is split: `provider/connectionTimeoutMs` (5 s,
  async `QTcpSocket` pre-flight) bounds only the TCP connect and fails a dead
  endpoint fast, `provider/responseTimeoutMs` (300 000) caps the whole request
  for Managed and External alike; a stored `provider/timeoutMs` migrates to
  the response timeout.
- **159.** `runtime/stopOnExit = false` is honored end to end:
  `LlamaServerProcess::detachFromProcess()` leaves the owner record for the
  next orphan scan and routes the server's stdout into the log file (append)
  while detached. The same hygiene batch made `DownloadTask::closeFile()` fail
  a truncated download with a translated message, tar `fieldString()` stop at
  NUL only, and `BoxCheckStatus` gain the `boxCheckStatusFromInt(int)` gate.
- **202.** A failed release-catalog check says so: the cache fallback is
  announced as «Showing the cached release list — the check failed: …», the
  cache timestamp is left untouched, and the check is retried on the next run.
- **203.** The runtime tab's release combo box follows the installer: the
  selection is read from it (`syncReleaseBox()`) and the box tracks catalog
  and selection changes instead of taking the first release of the list.
- **226.** The external runtime mirrors the internal one: the same tasks as
  the managed mode (see ADR 215), each with its own endpoint
  (`tasks/<role>/baseUrl`), its own API key and its own model alias; an empty
  field means the shared value. The alias is edited in one place — the
  «External server» tab of the runtime settings; the «Models» window shows it
  read-only.
- **228.** The external server keeps no process log, so the client keeps the
  trace: `RequestJournal` (in `llocr_core`) writes one record per HTTP call —
  address, model, request/response sizes, HTTP status, timing, response head —
  and the log window shows it in external mode. The trace lives in memory only
  (the page is the user's document) and never contains the API key.
- **231.** External-server wire rules (merges ADRs 229, 230): the request body
  always names the model (`ConnectionConfig::modelId` — role alias for
  external, server `--alias` for managed), so servers that require `model` no
  longer answer 422; a server that ignores `reasoning_budget_tokens` and burns
  the budget on thoughts gets the fix request retried without thinking, with
  later requests to that server going without thoughts and the reason named;
  and the external context window is read from `max_model_len` in
  `/v1/models` (one request per server, with that role's key) into
  `ResolvedConnection::contextSize`, the visual floor taken at 1024 tokens
  because no image budget is published.
- **236.** Release digest and download resilience (merges ADR 212): the «no
  digest, no install» rule stands, but the digest comes from the GitHub API
  asset `digest` field (`sha256:<hex>`, foreign algorithms and truncated hex
  rejected) with the release-body table as fallback — upstream stopped
  publishing the body table. A dropped transfer (no HTTP answer, mid-body
  break, HTTP 5xx) is retried by the download task itself — up to three
  attempts with 2 s / 5 s pauses, resuming via `.part` with `If-Range`;
  HTTP 4xx, digest refusal and lack of space are not retried, and a group
  failure reports the task's error («Скачивание не удалось: %1»), not a
  generic connection message.
- **262.** The llama.cpp sampler dump (`sampler chain:` / `sampler params:`)
  is filtered out of the managed server's log in
  `LlamaServerProcess::appendLine` — the `?`-prefixed samplers are disabled
  server defaults, not app settings, and log level 4 (see ADR 250) is kept for
  the memory breakdown.

### Models & tasks

- **8.** Recognition is cancellable (`abort()`) — users must be able to Stop
  long multi-page runs.
- **17.** The layout-token parser consumes the `<|det|>…<|/det|>` block stream
  and JSON-unescapes the content (`\n` → newline, `\\` → `\`, …); the legacy
  bare `label [x1,y1,x2,y2] text` form stays a fallback; no `<PAGE>` splitting
  — one page is sent per request.
- **18.** The parser strips model control tokens from content —
  `<|end_of_sentence|>` including the full-width-pipe (`｜` U+FF5C) and `▁`
  (U+2581) variants and any stray `<|…|>` wrappers — so the EOS marker never
  leaks into recognized text (shared `core/ServiceMarkers.h`).
- **19.** The `table` block renders as a GFM pipe table built from the model's
  `<table>` HTML: `colspan`/`rowspan` are laid into a dense grid but a spanned
  value is written once into the top-left cell it covers; a row made of one
  wide cell becomes a full-width section header; cell math goes through
  `convertMath`; `table_caption`/`table_footnote` render italic; the «Keep
  tables as HTML» setting (`output/tablesAsHtml`, default off) passes the
  `<table>` through verbatim instead (see ADR 64).
- **20.** The `ref_text` det-token (bibliography/reference entries) parses
  like `text` and renders as a plain paragraph, keeping its label and getting
  no extra Markdown styling.
- **21.** DRY sampling parameters (`dry_multiplier`, `dry_base`,
  `dry_allowed_length`, `dry_penalty_last_n`) are exposed in Settings and sent
  in the request body (part of the request profile) — they materially affect
  llama.cpp recognition quality and are tunable without recompiling.
- **25.** Duplicate bounding boxes are collapsed and flagged
  (`OcrPage::hasDuplicates`, `PageListModel` duplicate role → red marker) —
  the model can emit the same region twice; the parser dedups it and the UI
  surfaces it.
- **42.** Preset catalogs are split: read-only built-ins
  (`:/models/default-presets.json`, later `:/profiles`) + user
  `<AppData>/…/catalog.json`, merged by `id` (user wins) — files inside Qt
  resources cannot be edited and users must be able to add or customize
  presets.
- **43.** A preset pins the exact pair model + mmproj + parser + prompt +
  ctx-size — `det_tokens` and the prompt are model-specific; an arbitrary
  vision GGUF yields unparseable output.
- **56.** Request profiles replace the individual request settings: the
  request body (after `model`/`messages`) is assembled in profile order,
  built-in `:/profiles/request.json` + user copies merged by parameter name
  (user wins; file written only when values differ from defaults); UI = draft
  table with Save/Cancel/Restore-defaults; values keep their JSON kind, and
  `dry_sequence_breakers` defaults to `["\uE000"]` because llama.cpp rejects
  an empty array with 400.
- **59.** Request profiles are per-model: the file is an array keyed by a
  unique profile `id` (the OCR model id), and the active profile follows the
  task's model choice (`model/recipeId` for blockRecognition; unknown ids fall
  back to the first built-in profile); legacy single-profile v1 files are
  adopted as the default profile so user values survive; the model combobox
  lives on the Request tab, and switching it switches the draft profile
  immediately.
- **67.** Unreadable DjVu pages become explicit white replacements instead of
  rejecting the book: source index and bounded metadata dimensions preserved,
  `DocumentPage.sourceError` and `PreparedDjVu.warnings` persisted, warning
  count/first detail shown in the UI; OCR skips replacements, which remain
  unrecognized and excluded from recognized-text export; document-open
  failures remain fatal.
- **72.** Profile stores are split per model role: each role has its own
  request-profile, launch-profile and download-catalog files plus QML
  singletons, with `RequestProfileStore`/`LaunchProfileStore` carrying a
  `Role` that picks the user-override file and the active-id settings key;
  legacy two-role names (`request.json`/`requestValidate.json`,
  `serverLaunchValidate.json`, `catalogValidate.json`) are adopted/renamed on
  load; the runtime and wizard keep consuming the recognition profiles.
- **80.** Installed-model lists are role-filtered: `ModelEntry` carries a
  persisted `roles` list in `index.json`, recorded at install time and merged
  (never dropped) on re-install from another role; a model is listed when it
  is the role's active model, has the role tag, or — for legacy entries with
  empty roles — is inferred from the file layout (mmproj projector → OCR,
  plain text → verifier); `parser`/`prompt` deliberately prove nothing. The
  legacy `"check"` tag is renamed on load (see ADR 146).
- **87.** An OCR model is added by recipe, not surgery: one adapter class +
  one same-id built-in request profile + preset-catalog entries, everything
  data-driven through the existing Settings surfaces. Model-specific reply
  quirks live in the parser: OTSL table tokens
  (`<fcel>/<lcel>/<ucel>/<xcel>/<nl>`) convert to a GFM pipe table — or a real
  HTML table with live `rowspan`/`colspan` under `output/tablesAsHtml` —
  wherever the tokens appear, not only in `table` blocks (OtslTable), with
  `convertMath` hardening for truncated formulas and tilde artifacts.
- **88.** Output parsers are a per-model, data-driven layer:
  `ParserFactory::create(id, ParserOptions)` configures a parser once at
  construction (`keepPageNumbers`, `tablesAsHtml`, `bboxRange`, `modelId` —
  the 0–1000 bbox range is data, tolerance 1 % of it); `IOutputParser` owns
  `rebuildText(const OcrPage&)` and `displayName()`; `parser/id` defaults to
  `auto`, resolved via `OcrModel::defaultParserId()`; label→style mapping is
  data in `resources/profiles/labels.json` (`BlockStyleMap`, compiled-in
  fallback for tests); parse failures surface via `OcrResult::notes` →
  `Controller.parseWarning` instead of a silent single blob.
- **89.** The stage-4 parser split (`Preprocessor[]` → `TokenScanner` →
  `BlockStyler` → `BlockFormatter[]`) is deliberately deferred: with one
  parser per model (see ADR 112) the model seam lives in the class hierarchy,
  and the flat engine is split only once a model with a genuinely different
  reply shape (JSON, foreign XML, float coordinates) arrives.
- **96.** `ParserFactory::create()` rejects `auto` — `kAutoId` is a Settings
  value, not a parser id (`create("auto")` returns `nullptr` with a warning;
  an unregistered id still degrades to `raw`) — and activating a model no
  longer copies `ModelEntry::parser` into `Settings.parserId` (see ADR 88).
- **102.** One index space in the det-token parser: it builds `page.boxes` and
  nothing else, then sets `page.text = rebuildText(page)` — the same renderer
  the app calls after an edit. The parallel `blocks`/`rawCoords` lists are
  gone, and duplicate detection compares normalized rects directly over
  `page.boxes` (1 % tolerance, skipping fragments with
  `BoundingBox::positioned == false`).
- **110.** The OCR model is identified by id, never by display name:
  `model/recipeId` names the adapter and the UI binds combos through
  `modelId`/`displayName` roles. The separate `model/requestProfileId` setting
  introduced here was later removed — the model id is the request profile's
  only selector, so picking a model can never silently swap or pin the
  sampling profile.
- **111.** One chat exchange and one image encoder for the model adapters:
  `runChatExchange<Result>()` (models/ChatExchange.h) owns the round trip
  (body built on a worker, POST, parse, promise resolve),
  `encodeImageDataUrl()` is the single encoder, and
  `ConnectionConfig::timeoutMs` matches `SettingsStore`'s 120 s. The
  body/parse hooks stay `static` so requests complete after the adapter is
  destroyed.
- **112.** One parser per model, named after the model: `DetTokenParserBase`
  holds the shared engine (token scan → normalized boxes → Markdown via
  `rebuildText()`, OTSL and `<table>` sub-formats, service-token stripping),
  each parser answers header pattern / `\n` escaping / pre-tokenize repair,
  and the profile names its parser in `roles.<role>.parser`. The criterion: a
  new model is one JSON profile, one small subclass and two CMake lines.
- **122.** One models list — a row is a model and its quantizations are chosen
  on the row (`ModelQuantModel`): quants on disk that the profile does not
  list stay in the row (not downloadable, activatable and deletable), models
  belonging to no profile get their own row, and the selected quantization is
  remembered per profile (`models/quant/<profileId>`).
  `ModelInstallTransaction::preserveExistingFiles()` moves files the install
  does not write into staging before the commit, so publishing keeps sibling
  quantizations. The models-window surface itself was later reworked (see
  ADR 168).
- **123.** The order of the models of a role is one rule —
  `ModelProfiles::forRole()` (the recommended `default: true` profile first,
  the rest by title) — called by the models list and the request-profile
  pickers alike, and it is what the unknown-id fallback follows. The
  check-role profile picker introduced here was later removed with the other
  pickers (see ADR 129); the machinery survives as External mode's «Model
  profile» combo.
- **124.** A profile's `runtimeNote` says only what is still true: the
  mechanism (`ModelProfiles::runtimeNoteFor()` + the Launch tab, wizard step
  and download dialog) stays for the next unrunnable model, but TeleOCR's
  stale "needs a patched llama-server" note is gone — its
  `konradjr007/NaviDC-OCR-GGUF` weights load in a stock build. `minBuild`
  still cannot express "needs a patched core".
- **129.** A profile is resolved by the app, not picked: the «Profile» combos
  and «Restore profile» buttons left the Request/Launch tabs, and the draft
  follows the selection (`RequestProfileStore::followActiveProfile()` on the
  model ids, `LaunchProfileStore::ensureProfileResolved()` on backend
  changes). External mode keeps the single «Model profile» combo over
  `RequestProfileListModel` that writes the same settings ids the automatic
  paths write.
- **132.** «Recognize all» skips pages that already have a result;
  «Recognize» does not: the skip predicate is
  `PageSkip { None, Unreadable, AlreadyRecognized }` with `SkipResolver`, the
  batch flag deciding, and `firstPageToRecognize()` bails out before
  `setBusy()`/`ensureConnectionReady()` when nothing is left — a no-op click
  never starts a server or loads a model.
- **135.** Duplicate handling goes beyond bboxes: after the exact-rect pass
  (ADR 102) the parser drops a block whose text substantially repeats an
  earlier one (normalized equality or a ≥ 8-word verbatim run covering
  ≥ 60 %), tail-joins a resumed block's unique tail onto the earlier block,
  and marks survivors `BoundingBox::duplicateSuspect` (persisted in `.llocr`).
  Suspects are verified regardless of the block-type filter; a short-text
  guard keeps titles and captions out of the net.
- **136.** LFM2.5-VL output fixes (the model is archived, the mechanism
  pattern remains): equation blocks lose their inner `$…$` wrappers (the
  trailing `(n)` becomes `\tag{n}`), and letterbox coordinate insets are
  mapped back out by the parser (`calibrateRect()` with
  `ParserOptions::pageAspect` carrying the page geometry) — the exact-rect
  duplicate pass compares calibrated rects.
- **144.** Speculative decoding via MTP draft models as shipped:
  `--spec-type draft-mtp --spec-draft-n-max 4` in the fix model profiles; the
  exploration ADRs 137–143 led here. A draft module is first-class
  model-profile data (`files.mtp` with repo/pinned sha, `ModelEntry::draftPath`,
  `launch/draftPath` → `--model-draft`, sidecar drafts scanned as
  `ModelFileKind::Draft`), and the merged set also settled the request
  plumbing: the generation budget lives in the request (`max_tokens`; no
  `n-predict`, no fallback launch layer — the Launch table shows the machine's
  `n-gpu-layers`/`flash-attn`/`ctx-size`), the model id is the request
  profile's only selector, the wire part order is per-model data
  (`promptBeforeImage`), and the verifier's wording lives only in model data
  (the shared `verifyPrompts.json` prompt tabs are gone — the verification
  window keeps block-type enablement).
- **146.** The check role is named `blockRecognition` everywhere renaming
  costs nothing (profile role keys, `ConnectionRole::BlockRecognition`,
  registry migration of a stored `"check"` on load); the legacy vocabulary
  survives where it would churn user data (`check/*` settings keys,
  `CheckController`/`CheckRequest`/`CheckResult`, UI «Модель OCR блока»).
- **150.** The manual page-markup pass is the `layout` task with its own full
  model settings (`ConnectionRole::Layout`, the `layout/*` settings group,
  the «Модель разметки» window — reversing the shared-with-check configuration
  of ADR 149). `LayoutController` queues pages (current/all), squashes each to
  the upstream-calibrated 1036×1036 square, and the layout parser rewrites the
  reply's box/label lines into canonical tokens; `AppController::applyLayout`
  replaces the page's blocks — content in the reply is ignored, text arrives
  per block through blockRecognition. ADR 252 folded the role into the task
  matrix.
- **169.** Every task ships an explicit default model —
  `roles.<task>.default: true`, exactly one per task, machine-checked by
  `test_model_preset_catalog::roleDefaultsFollowTheShippedProfiles`. It is the
  data layer for auto-selection: the quant is the first profile entry whose
  curated/measured size fits the machine.
- **179.** The memory estimate tells the truth: `ModelMemoryEstimator` reads
  the declared architecture, the true head dim (`attention.key_length`), the
  projector and the ubatch working set, counting attention layers only (a
  hybrid Qwen3.5 keeps KV in 8 of 32 layers), and reports the footprint
  against the card's VRAM in the Launch tab.
- **182.** The request texts of a task live in their own request profiles —
  `resources/profiles/requests/fix-requests.json` and `read-requests.json`,
  named by a role's `"promptProfile"` — while the model profiles keep what is
  genuinely per-model: launch and request parameters, output budget, decision
  calibration and routing. A role naming an unknown request profile is a load
  error; the OCR models keep their prompts inline because their tasks differ
  per model.
- **191.** The quant dropdown answers «what card does this quantization need»:
  `✓ Q8_0 — 9.1 GiB · VRAM ≥ 11.4 GiB`. The model profile carries a `memory`
  block (architecture plus curated `sizeBytes`/`draftBytes`), so
  `ModelMemoryEstimator` computes the whole server's requirement from either
  the GGUF header or the profile — one formula on llama.cpp's own numbers
  (exact KV cache, working set within 12 %, vision encoder measured per image
  token, draft context, `kvBytesFixed` for sliding-window hybrids, the
  `nextn` head keeps no KV cache); the Launch tab compares the requirement,
  not bare buffers, with the card. Method and calibration:
  `docs/18-memory-estimate.md`, `scripts/measure-memory.sh`,
  `scripts/memory-from-log.py`.
- **198.** `ModelRegistry::update()` reads the index under the write lock,
  checks the version, and rebuilds a broken index from the models directory
  before mutating — the models on disk are the truth, the index is their
  cache; the caller is told via `update(..., bool *rebuilt)`.
- **207.** The parser id is a property of the profile, not of a model
  instance: `OcrModel::defaultParserIdFor(modelId)` reads the profile (with
  the same fallback `create()` applies) instead of building an `OcrModel`,
  which would construct a `LlamaClient` with a `QNetworkAccessManager`; the
  controller remembers the id per profile.
- **215.** The list of tasks lives once in `core/ConnectionRole.h`
  (`connectionRoles`, `connectionRoleIds`): runtime state is
  `std::array<bool, connectionRoleCount>` indexed by role, QML reads
  `Settings.taskRoles`, the wizard reads `ModelInstaller::defaultTaskChoices`;
  a `static_assert` and `TestSettingsStore::theTaskListIsTheSingleSource` keep
  the list and the name parsing in step.
- **216.** Per-task settings in `SettingsStore` are dispatched by one table (a
  record per role: launch paths — model, mmproj, draft — request profile,
  model name) instead of ten `switch (connectionRoleFromName(role))` branches;
  `TestSettingsStore::theRoleDispatchReachesEveryRole` proves the dispatch
  hits its own task.
- **217.** `RoleProfiles.qml` resolves a task from one table (row = task,
  column = property) instead of six condition chains; texts stay values
  translated in the file's own context, singleton-reading columns are
  functions so bindings keep tracking settings, and the dead
  `quantModels("all")` branch is gone.
- **218.** Rotation is a property of the page: `DocumentPage::rotation`
  (0/90/180/270) is applied where the page is rendered
  (`DocumentModel::renderFull`/`renderDetached`), `pageAspect` is computed
  from the rotated size, and a rotation resets the page to its
  pre-recognition state. Manual rotation (two page-panel buttons) touches only
  the current page, asks for confirmation when a result exists, and marks the
  page «повёрнута вручную» so the orientation check leaves it alone.
  Orientation is read by the pixel check only — the model arbitration once
  planned here and in ADR 221 was removed (see ADR 242, ADR 243).
- **220.** A task reaches the profile: `OcrModel::create(id, role)` and
  `roleForTask()` pick the task's role when the model declares it, and both
  the prompt and the parser come from that role
  (`effectiveParserIdFor(profileId, role)`, the role in the cache key) — a
  model serving several roles no longer answers with the first-found role's
  prompt and parser.
- **222.** A projector is recognized by `mmproj` in the file name
  (`ModelCatalog::fileKind` → `ModelFileKind::Vision`); the installer
  (`ModelInstallTransaction::selectModelFiles`) and
  `ModelRegistry::scanModelsDir` take the first projector, and every shipped
  model names its projector `mmproj*` — the profile field declaring its
  projector was dropped (its only user was removed by ADR 223).
- **223.** MiniCPM-o 4.5 is measured as a fix model and not shipped: it
  returns a verdict without a corrected block (2 Ok / 0 Fix / 5 Review, CER
  0.147 → 0.147), so it stays in the stand
  (`OCRtest/fix_models/profiles/minicpm-o-4.5.json`) with no profile in the
  app catalog — a capability is declared only with a measurement.
- **224.** The HTML reader (`src/parsers/HtmlMarkup.cpp`) knows self-closing
  tags — a `/` after the tag name closes the element, so `<br/>` no longer
  swallows the rest of the page as its children — and a block the model
  labeled `Equation-Block` that carries prose outside `<math>` gets the `text`
  label, not the formula pipeline.
- **225.** A formula inside a table cell is the app's math markup, not a
  `<math>` tag: `serializeHtml` keeps tables as HTML but converts `<math>` to
  `$…$`, so KaTeX renders the cell in the preview and in every export that
  carries the HTML.
- **242.** The orientation check is pixel-only: the image reads the text axis,
  and a page that lies sideways or that it cannot read is marked red (page
  marker plus «лежит боком» / «ориентация не определена» status) and left as
  it is — the user turns it, and turning clears the marker. The model part of
  ADR 221 is removed entirely: `orientation-requests.json`,
  `DecisionController::askChoice` and the `questions` field
  (`Role::questions`, `DecisionBlock::questions`) are gone.
- **243.** The orientation detector is two-stage (`app/PageOrientation`): ink
  mask by local Sauvola threshold, bbox crop, deskew by a ±15° sweep at 0.5°
  steps (maximum of summed squared profile differences, parabolic
  refinement), then quadrant by normalized autocorrelation of the line
  profile (window 12…60 px, threshold ≥ 0.15) with alternation depth cv ≥
  0.65 picking the axis; closing 1×25 bridges words into bands and counts
  linear components (fewer than ten → «don't know»). The verdict carries
  `deskewDegrees` and `confidence`; 180° is still unread (projections are
  half-turn invariant), and the check stays fast (tens of ms per page).
- **244.** The orientation verdict straightens the page: an axis read with
  `deskewDegrees` ≥ 0.5° signals `skewed` and `AppController` applies it to
  `DocumentPage::skewDegrees`, rendering deskews by smooth white resampling
  (`PageRotation::deskewImage`); the correction accumulates, self-settles
  below threshold, resets recognition and is saved in `.llocr` next to the
  rotation. A quarter-turn still waits for a direction source — the layout
  model's rotation tokens (see ADR 245).
- **245.** Layout rotation tokens reach the page as a one-click suggestion:
  MinerU's `<|rotate_up/left/right/down|>` after each block is kept by the
  parsers in `BoundingBox::orientation`, `applyLayout` votes, and a page the
  pixel check marked sideways gets a «Повернуть влево/вправо» button that
  turns it through the normal `rotatePage`. Auto-turn by tokens is
  deliberately off — direction is guessed in 14 of 20 cases, and a wrong turn
  is worse than an unturned page (ADR 242).
- **246.** Layout turns a sideways page itself and re-layouts: with
  «поворачивать боковую страницу после разметки автоматически» on (default),
  the layout run turns the page once (`turnPage`, clearing the marker and the
  just-read markup) and starts a second layout pass; the vote is counted
  straight from the answer lines (`OcrPage::orientationVote`, winner needs
  ≥ 9/10 «not up» and ≥ 6/10 of all lines). The table-of-contents false
  «sideways» label is fixed by alternation depth. The 180° hole is honest —
  no cheap 0/180 detector yet; direction remains a model guess (14/20). With
  the setting off, ADR 245's one-click hint works.

### Verification & fix

- **134.** The verification queue asks only for blocks that have no answer
  yet: the `checkStatus != NotChecked` filter is unconditional for
  «Проверить страницу» and «Проверить всё» (`collectEnabledBoxes()`), an
  empty queue reports «Nothing to check: N block(s) already verified.», and
  the explicit per-block re-verify still works — a re-recognized block returns
  to the queue.
- **155.** The per-block verification loop pays no connection taxes:
  `RuntimeController` caches the last successful `ResolvedConnection` per role
  (invalidated by state transitions and settings changes, failures never
  cached), each model owns one `LlamaClient` (keep-alive across per-page /
  per-block requests), and `availabilityChanged` fires only on an actual flag
  flip.
- **163.** The `fix` task's protocol is OK / FIX / REVIEW: the request carries
  the profile's verifier prompt, the block-type prompt, the OCR candidate
  framed `<ocr_candidate>…</ocr_candidate>` (one canonical candidate — a
  stored correction is never re-sent) and the crop, ordered per
  `promptBeforeImage`. OK stores no correction (see ADR 185), FIX stores the
  corrected text, REVIEW marks the block damaged, an empty FIX degrades to
  REVIEW, and any other reply is a request error. The wire wording was later
  refined (see ADR 181).
- **164.** Two-layer block verification: every block carries
  `box.text`/`box.correctedText` with independent verdict + confidence
  (`checkStatus`/`confidence` vs `correctedCheckStatus`/`correctedConfidence`,
  pure `BoxTransitions` functions). Any OCR result lands in the text layer and
  erases the correction entirely, a fix result lands in the correction layer,
  the decision judges the effective layer, «Отменить исправление» rolls back,
  and there is no automatic promotion — export, page text and preview use the
  effective text. `BoxCheckStatus::ReRecognized` (value 2) was deleted without
  renumbering, removing the amber two-variant state of ADR 153.
- **165.** A page carries two independent statuses: the persisted
  `PageRecognitionMode {None, MarkupOnly, RawOcr, MarkupOcr}` (`.llocr` v2;
  MarkupOnly becomes MarkupOcr once every non-image block has text) and the
  computed verification aggregate over the enabled block types + duplicate
  suspects — «проверено» only when every subject block has a verdict on its
  effective layer, any Mismatch/Review makes it «есть ошибки».
- **173.** The decision verdict's P(match) is persisted in `project.json`
  (`BoundingBox::confidence`/`correctedConfidence`, −1 = none, additive field,
  no version bump), so threshold calibration is one run instead of an
  iteration per candidate threshold — the eval stand recomputes the FN/FP
  curve at any candidate threshold from the stored values.
- **178.** The automatic check and the automatic fix are gone:
  `check/autoCheck`/`check/autoFix` (and the older `check/autoRecheck`) are
  deleted with their whole code path (`FixMode` enum, `m_fixQueue`, the
  recognition-finished auto-check handler, the QML registration it required).
  Checking and fixing are two separate user actions — a check run ends after
  the decision pass, and the fix is a separate queue the «Исправить» button
  starts over the problem blocks.
- **181.** The fix protocol speaks three verdicts — OK / FIX / REVIEW: `EMPTY`
  and `FIX none` are gone (any deviation, including an old EMPTY or a
  free-text remark, lands on Review with the reason in the footer), the system
  prompt checks the block along six independent axes (script, case, face,
  position, delimiters, designations), and the formula block answers with the
  complete LaTeX formula which `FixDiffGate::fixFullBlock` applies as the
  correction after the equivalence check. The `{{LANGUAGE}}` placeholder was
  later removed (see ADR 266).
- **183.** The decision task asks ONE noul question: the request profile
  dropped the v6 two-question dict (measured: d3 answers each question of a
  request with its own forward pass), and the empty/unreadable route to Review
  moved to the fix task's REVIEW. The state text names the independent axes
  (script, case, face, position, delimiters, designations, units — a block is
  verified only if right on all), per-block rules add to the general ones
  instead of replacing them, and the formula arrives in the canonical wire
  form. The `{{LANGUAGE}}` placeholder was later removed (see ADR 266).
- **184.** The decision request texts are rewritten around «the markup is a
  carrier» — per block type, what is carrier markup (`\(…\)`, LaTeX commands,
  HTML, Markdown) and what is a content difference — and every decision
  model's `matchThresholdByBlock` is re-measured on the corpus that matches
  the shipped request. The knee of each type's FN/FP curve picks the
  threshold, the eval stand reproduces the app's request byte-for-bit (sorted
  state keys), and the calibration curve comes from the stored P(match) (see
  ADR 173).
- **192.** The fix protocol speaks full blocks: the `span_hybrid` contract of
  ADR 180 is withdrawn, and a FIX reply carries the complete corrected block
  for every block type. `FixDiffGate` asks one question — may the reply
  replace the candidate: an equivalent block keeps the candidate's bytes (Ok),
  an empty reply, `FIX none` or a budget-cut reply (`finish_reason: length`)
  land on Review. Prompt texts live in
  `resources/profiles/requests/fix-requests.json` (six axes, merged
  `caption`/`footnote` aliases), and `maxOutputByBlock` fits the whole block
  (text 2048, list 2048, code 4096, table 16384), shared by all fix models.
- **193.** The fix task may think first: the model profile declares
  `reasoning: true`, and the app derives the per-request
  `reasoning_budget_tokens` from the four steps off / low 512 / med 1024 /
  high 1536 — no server restart; the budget also drives
  `chat_template_kwargs.enable_thinking`. `src/core/RequestBudget.h` computes
  the generation budget inside the server's context window, the thinking
  yields before the answer, and a cut answer is a Review. Where the budget is
  stored moved to the global check settings (see ADR 268); the four steps
  remain its vocabulary.
- **194.** A formula is normalized where it is recognized: the canonical form
  (`core/LatexText`) is carried by the block from the moment recognition
  lands (`BoxTransitions::latexResultApplied`), so panel, export, checker and
  fixer see the same bytes (`FixDiffGate::normalizeLatexCandidate` is gone).
  `LatexText::check` validates syntax without a LaTeX engine (braces,
  environments, `\left`/`\right` pairs, math delimiters, a command cut off, a
  missing `\frac`/`\text`/`\sqrt` argument); a failing formula is rejected at
  once — status «неверный синтаксис» (red, `BoxCheckStatus::InvalidSyntax`) —
  and is excluded from verification and fix until a new recognition.
- **195.** A markdown code fence inside a formula is carrier markup, not
  formula content: it is unwrapped when the formula lands
  (`LatexText::canonical`, together with ADR 194's canonical spacing), and an
  unbalanced fence or a leftover backtick is its own defect — «неверный
  синтаксис», reason «блок кода внутри формулы»
  (`LatexText::Problem::CodeMarkup`). The page text is rebuilt from the blocks
  after the canonical form is applied (`applyFormulaVerdicts` →
  `rebuildTextFromBlocks`), so the model's spacing and the fence never reach
  the document, preview or export.
- **196.** A formula the model split into several «$$…$$» fragments renders as
  several display blocks — each fragment its own line, a standalone equation
  number becoming the `\tag` — and the «$$» carriers are balanced, not a
  syntax problem. The invisible `\left.`/`\right.` delimiters pair with any
  bracket, so valid multi-line formulas are no longer flagged broken; syntax
  verdicts are recomputed when a project opens
  (`BoxTransitions::syntaxVerdictRefreshed`).
- **206.** The availability guard comes before the work: `updateAvailability()`
  compares its inputs first (document/crop revisions, current page, selection,
  verification filter, busy states, runtime state, model flags) and computes
  the twenty flags only when an input changed. Built-in model profiles are
  indexed once (`ModelProfiles::findBuiltIn`) instead of a scan per block of
  every page.
- **235.** Visual-diff decision is its own wire protocol
  `systemone_visual_diff_v1`, not a request profile: MiniCPM-OCR Jev is a
  binary classifier (Qwen3.5-2B trunk with a classification head instead of
  `lm_head`, zero generated tokens, answer = P(candidate matches crop)), the
  wording is baked into the model, and the request carries exactly two images
  (crop, then candidate render) and a bare state string. The branch lives on
  the role's `protocol` (`DecisionModel::buildRequestBody`), the candidate is
  rendered by `app/CandidateRenderer` (marked + KaTeX → one-page PDF →
  raster), a failed render sends the block to Review, and the threshold is
  calibrated per block type (ADR 258).
- **237.** The candidate image for visual diff is drawn from the block's
  displayed text (`parsers/BlockRender.h` → `renderBlockText` — formula as
  `$$…$$`, table as a markdown grid), must fit one page (48 px margin; a
  second page is a refusal → Review, not a picture), and is cropped to the ink
  (±2 px). The corpus measurement had shown 79 % false rejections caused by
  exactly these three render-leg defects.
- **249.** The fix profiles' `stop` lists hold only the protocol marker
  `</CANDIDATE>` — end-of-text tokens in `stop` truncated generation to one
  token, because with `--special` the stop strings are matched against the
  rendered reply; llama-server stops on the model's end-of-text token by
  itself. All four fix profiles (`qwen3.5-4b`, `qwen3.5-9b`, `gemma-4-12b`,
  `gemma-4-e4b`) are cleaned.
- **250.** The `minicpm-jev` launch profile keeps
  `--ctx-size 16384 --batch-size 2048 --ubatch-size 512` — with the earlier
  fixed 32768/8192/8192 llama.cpp pushed a 1344 MiB compute buffer to host and
  the auto-fit (`common_fit_params`) was defeated by the pinned layers and
  context; the model verdict is unchanged. `serverLaunch.json` sets
  `--verbosity 4`, the minimum level that logs the memory breakdown, GPU
  layers and buffer sizes.
- **257.** Auto mode is a list of stages, not a button: the auto-run button
  runs layout → block recognition → check → fix → check-of-fixed as
  `AutoPipelineStage` contracts (`skipReason`/`start`/`stop`/`finished` /
  `failed`) in `AutoPipelineController`. Stages run per stage, not per page —
  a managed server reloads the model on every task switch; a failed stage does
  not abort the run, a missing model skips the stage with a reason in the
  footer; layout always restarts with confirmation; the fifth stage is the
  same check over the correction layer. Details: `docs/11-auto-pipeline.md`.
- **258.** Visual-diff thresholds are measured on the OCR_TEST corpus (177
  blocks, MinerU2.5-Pro recognition, app request bit-for-bit): text 0.92,
  equation 0.87, table 0.4, base 0.5 — FN 10 (13.7 %) / FP 41 (39.4 %) versus
  FN 29 / FP 15 at the old 0.5. Two structural facts: the model rejects tables
  wholesale (all p < 0.02 — the markdown-grid render is out of distribution),
  and typed scans carry a threshold-proof FP tail (the render re-typesets the
  paragraph, p ≈ 0.1–0.35); residual FN are print homoglyphs and integrals.
  Report: `OCRtest/decision_models/test 2/iteration8/README.md`.
- **259.** The fix role has a second protocol, `read_block`: the model writes
  the block from the image, and the reply is `FIX` (full block) or `REVIEW` —
  `OK` is out of the schema. `FixDiffGate::fixBlock` compares the read text
  with the block text (equivalent → Ok, different → Fixed, empty or cut →
  Review) and treats `\eqno{n}` ≡ `\eqno(n)` ≡ `\tag{n}` ≡ `(n)` and `{{x}}` ≡
  `{x}` as equivalent carriers. Prompt texts live in the shared
  `read-requests.json`; the recognized text no longer travels as a writing
  sample (see ADR 266). Measured on `gemma-4-12b`
  (`OCRtest/fix_models/READ_PROTOCOL.md`).
- **260.** Re-reading is an action, not a model property: the protocol rides
  the request (`CheckRequest::fixProtocol`), so any fix model can read — no
  role switch or model reload. «Перераспознать» (magnifier over the page) sits
  next to «Проверить» and in the block toolbar; the header button takes all
  text blocks of the selected pages except user-owned ones (manual edit or
  approval), the toolbar button takes the selected block. Outcomes are its own
  statuses: `Read` / `ReadFixed` / `ReadDamaged`; the auto pipeline (ADR 257)
  still fixes — re-reading is manual. The `\tag{n}` ≡ `(n)` gate rule actually
  matches now (the old regex read `\t` as a tab).
- **261.** The read result is visible: reader states speak in the frame (green
  — read, which is itself a verification; red — could not read; fixer states
  stay out per ADR 240), the read protocol explains its outcomes in its own
  words («Прочитано: текст совпадает с картинкой…», «Не удалось прочитать
  блок…»), and a re-read run reports its total («Прочитано блоков: переписано
  — %1, подтверждено — %2, не прочитано — %3»).
- **265.** Reading a block that has no recognized text is recognition: the
  answer lands in the text layer (`box.text`), not the correction layer; the
  block is gray («Распознано»), waits for the deciding model and then the
  fixer like any recognized block, and the page stops being «только разметка»
  once all its blocks have text.
- **266.** In reading mode the request is exactly three things — the block
  image, the system prompt, the block prompt; the recognized text never
  travels (no `<RECOGNIZED>` frame). `read-requests.json` keeps a single
  system prompt, the table prompt names HTML serialization outright (OTSL →
  HTML, `OtslTable.cpp`), and the `{{LANGUAGE}}` placeholder is gone. The
  measurement: CER 0.015 and index hit 1.00 without the sample vs 0.040/0.84
  with it — the user chose reading accuracy; the gate's comparison with the
  block text stays internal to `FixDiffGate`.
- **267.** Request wording is model calibration: a role may override all four
  text kinds (`systemPrompt`, `blockPrompts`, `readSystemPrompt`,
  `readBlockPrompts`) over the shared `fix-requests.json` /
  `read-requests.json` defaults — own wins, shared fills the gaps (amends
  ADR 182). The Qwen3-VL family reads the shared read wording as
  correction-verification and answers REVIEW on every readable block, so it
  carries its own texts; read block prompts and answer budgets fall back to
  the shared profile, and the alias hop applies to overrides too.
- **268.** Reasoning budgets are global check settings, not a request-profile
  parameter: `check/fixReasoningBudgetTokens` and
  `check/readReasoningBudgetTokens` (default 0; the four steps off / 512 /
  1024 / 1536 remain the vocabulary — the parameter of ADR 193), edited in
  Настройки → Проверка. `reasoningModes` is cut from the request profiles
  (`ModelProfiles::PromptProfile::reasoningModes` deleted),
  `FixController::fixBlock` injects the budget per protocol (full_block → fix
  budget, read_block → read budget), the fix role is found by the active
  request-profile id (`RequestProfileStore::activeProfileId`, not
  `conn.modelId`), models with `reasoning: false` get nothing, and stored user
  profiles are stripped of the parameter once
  (`ProfileStore::stripUserParameter`). The wire is unchanged.
- **286.** The verification queue is split into role queues. `BlockWorkList`
  (`app/BlockWorkList.{h,cpp}`) owns the block collection — the five
  collectors (unrecognized / enabled / fixable / problem / readable) and the
  page-level enablement predicates — as pure document queries. `BlockRunGate`
  keeps the block queues one run at a time; the markup pass and a stopped
  request still winding down hold it from the outside. The base `BlockQueue`
  owns the shared runner (work list, guards, progress, stop, the waiter's
  `finished` signal) and the three roles own their request machinery:
  `DecisionQueue` (the visual diff with its render leg and thresholds,
  ADR 235/254/184/258), `RecheckQueue` (the blockRecognition role, ADR 146)
  and `RemedyQueue` — one class over the two remedy protocols (fix and read,
  ADR 260) that also carries the read counters and summary (ADR 261).
  `VerificationQueueController` is removed: `AppController` owns the three
  queues and the readout state (the error line, the finished flag, the
  progress source), the QML API and the result signals
  (`blockVerified` / `blockRecognized` / `blockFixed` / `blockRead`) are
  unchanged. The auto pipeline stages take their typed queue and the work
  list (ADR 257) and `AvailabilityPresenter` reads its enablement predicates
  off `BlockWorkList`. Amends ADR 86 (the queue state machine's owner) and
  ADR 213 (the facade's domain list); the protocols, the statuses and the
  stop semantics are untouched. The `check*Phase` properties narrow quietly:
  they now mean «this role's run is active» instead of the old sticky
  «the last run's phase» — every QML reader consults them under
  `checkRunning`/`stopMode`, so nothing observes the difference except a
  sub-second stop-button wind-down flicker accepted for the simpler
  semantics.

### UI

- **9.** Navigation stays live during recognition — users browse other pages
  while a batch run proceeds.
- **10.** Export writes TXT/MD/HTML directly; DOCX goes through Pandoc (native
  Word equations); PDF uses the preview render pipeline (see ADR 63) — the
  Pandoc→LaTeX PDF path was removed.
- **11.** Thumbnails carry no bounding boxes — the strip signals
  recognized/edited/duplicate/current only; boxes belong on the center
  preview.
- **13.** Window geometry is persisted (`WindowSettings` + `SettingsStore`
  `ui/*`): position, size and visibility survive across sessions.
- **16.** i18n via Qt Linguist (`qsTr`/`tr` + `.ts`/`.qm`), language
  selectable in Settings (System/English/Русский), default System, runtime
  retranslate; persisted as `ui/language`, embedded `.qm` loaded at startup
  and on change.
- **22.** The Markdown preview renders client-side via Qt WebEngine + bundled
  marked + KaTeX (`resources/preview/`) — LaTeX/tables/images render locally
  with no network access; `image://ocr/crop/*` refs are converted to `data:`
  URIs before rendering.
- **23.** Pages can be deleted and drag-reordered in the thumbnail strip;
  `PageEditStore` and `PageListModel` remap indices afterwards so edits follow
  their page.
- **24.** Image/chart blocks are editable on the preview (move/resize/delete)
  and exported as cropped images; `rebuildPageText()` regenerates the page
  Markdown because image refs embed the box index (`image://ocr/crop/<N>`),
  so removal forces a rebuild to keep indices consistent.
- **55.** The restored window position is validated against the current
  screens (`WindowSettings::visiblePosition`): the saved x/y applies only
  while the window's title-bar band overlaps a connected screen, otherwise
  the window centers on the nearest screen (amendment to ADR 13).
- **63.** HTML/PDF export reuses the preview render pipeline: a headless
  `QWebEnginePage` loads `qrc:/preview/export.html` (marked + DOMPurify +
  KaTeX), crops embed as `data:` URLs, CSS from `Exporter::exportStyleSheet()`
  is shared by both outputs; PDF falls back to `QPdfWriter`, HTML to the
  escaped-text writer; DOCX stays Pandoc-only (Chromium cannot emit OOXML);
  crop encoding runs on a worker, rendering on the UI thread (`QWebEnginePage`
  restriction).
- **64.** The Output settings group is five keys: `output/splitPages` (default
  on; page separators without "Page N" labels), `output/keepPageNumbers`
  (default on; drops page furniture at parse time — see ADR 280 for the
  widened scope), `output/tablesAsHtml` (default off, see ADR 19),
  `export/pdfLandscape` + `export/pdfMarginMm` (0–50 mm, default 15 →
  `QPageLayout` in `AppController::pdfPageLayout()`).
- **70.** Settings are separate non-modal windows opened from the Header menu
  (`SettingsDialog`/`SDTabBar` removed): Interface applies immediately with a
  scoped "Restore defaults", the others commit on Save; one llama-server
  instance serves all roles, the model is (re)loaded per task;
  `RuntimeController` auto-discovery and the footer tab shortcut were removed
  as duplicates of the wizard/installer flow.
- **75.** Output and Runtime windows have scoped, immediate "Restore
  defaults" (`SettingsStore::resetOutputDefaults()` /
  `resetRuntimeDefaults()` filter the ADR 50 defaults table and write through
  the property setters); the Runtime scope deliberately excludes storage dirs,
  autostart flags and launch keys.
- **77.** UI text sizes are platform-compensated in `Theme.qml`
  (`platformTextScale = 4/3 on macOS` — the single place a point size is
  chosen; no QML file hardcodes `font.pointSize`): 1 pt is 1 px on macOS but
  1.33 px on Windows, so without the factor text came out ~25 % smaller
  relative to the pixel-sized controls.
- **79.** Connection mode is a single combo choice and each window's content
  follows it: in External mode the model window shows only a "configure
  runtime" notice, in Managed it offers the download sources; Start/Stop/
  Restart/Show-log live in the footer, not in the settings window; installed
  builds get a per-row "Open folder" (`RuntimeInstaller::openBuildFolder()`).
- **81.** The setup wizard is a self-contained linear flow (intro +
  language/theme, work-mode cards writing `provider/mode`, mode-dependent
  steps, output, check-and-launch with self-test, mode-reflecting summary),
  not a launcher for settings windows; steps render by visibility because the
  step set differs per mode, the footer shows "Step N of M", and the wizard is
  reachable from the settings menu.
- **82.** Embedded lists (`ModelInstalledList`/`ModelPresetList`/
  `RuntimeBuildsList`) publish `implicitHeight = min(count, maxVisibleRows) *
  rowHeight` and consumers bind to it instead of hand-copied formulas; the
  three-column Blocks tab uses `BlockGroupFilterModel : QSortFilterProxyModel`
  on the `group` role (exposed as
  `Verification.blockModelContent/Captions/Service`) instead of three
  full-model Repeaters, and `VerificationBlocksModel::rowOfType(type)`
  resolves source rows because `Q_INVOKABLE`s do not pass through a proxy.
- **113.** QML states the rules; C++ decides them: the «Launch settings
  changed — restart» banner is `RuntimeController::launchConfigDirty` (value
  equality of `ServerLaunchConfig`, subscribed by walking `SettingsStore`'s
  meta-object via `connectEveryChangeSignal`, cleared when the server stops),
  and the wizard gates on `Runtime.serverPathValid`/`modelPathValid` /
  `configValid` — a path that is set but missing on disk is not a
  configuration.
- **114.** A message is stored untranslated and rendered when read:
  `StatusMessage` (core/StatusMessage.{h,cpp}) carries verbatim text or
  context + key + `%n` args, and `text()` resolves in the language installed
  right now, so `AppController::retranslate()` is two signals. The factory is
  `translate(context, key)`, never `StatusMessage::tr`, because `lupdate`
  reads the first literal as the context.
- **115.** The installer lists are list models with declared roles —
  `InstalledBuildsModel` and `InstalledModelsModel` exposed as
  `RuntimeInstaller.installedBuilds` / `ModelInstaller.installedModels` — and
  QML delegates declare `required property` roles instead of reading
  `QVariantMap`s by string key. `ModelEntry` gained value equality so a
  re-scan that finds the same models does not reset the model.
- **119.** A diagnostic has one home — the append-only `ProblemLog` (`Log`
  singleton, `StatusMessage` entries, 200 ms coalescing, 2000-entry cap,
  `ProblemLogWindow.qml`) — and the footer keeps one elided line: the current
  page's decode error, else the check error, else the parser note, plus a
  count and a «Details…» button.
- **120.** A list model repaints when a derived role changes, not only when
  the entries change: `InstalledModelsModel::setEntries()` compares the
  computed row set (reset vs `dataChanged` with all roles), so activating a
  model updates the active highlight even when the entries are unchanged.
- **130.** The installed-builds list shares the models-list row grammar:
  `Common/RuntimeBuildsList.qml` (two text lines, size column via
  `InstalledBuildInfo::sizeBytes`, one action button, ⋮ menu with per-build
  delete under the install lock — `RuntimeInstaller::removeBuild(index)`
  refuses the active build). The header's book actions were consolidated into
  a File menu with one Stop; the header itself was later reworked into the
  pipeline toolbar (see ADR 171).
- **151.** The interface split stands: book actions in the header, per-page
  actions in the page toolbar, and a floating `BlockMiniToolbar` on the
  selected block (распознать / проверить / удалить; delete-only for image
  boxes). The availability flags moved to one dedicated `availabilityChanged`
  NOTIFY, and the Lucide (ISC) icons are colorized at runtime by
  `Common/Icon.qml`. The toolbar was later consolidated into the labeled
  pipeline groups (see ADR 171).
- **153.** The block overlay carries no label chip: every box is a translucent
  fill plus a double frame in its kind's hue (`BoxListModel::boxKind` +
  `Theme.overlay<Kind>{Fill,Outer,Inner}`, data-driven from `BlockStyleMap`),
  the status dot sits outside the block to the left (gray = recognized, no dot
  for markup-only). The box hue follows the block kind, not the status.
- **156.** The block-text rebuild behind `blockTextMapped`/`blockTextRange`/
  `updateBlockHighlight` is memoized on (current page, `docRevision`,
  `cropRevision`, shown page text), parser-option changes reset it through
  `invalidateRebuildCache()`, and an empty selection short-circuits to an
  empty highlight instead of rebuilding the page.
- **157.** All ten secondary windows and the setup wizard are
  `Loader { active: false }` handles in `Main.qml`, created on first use via
  `showWindow(loader)`/`windowProxy(loader)` — startup no longer builds
  eleven window trees before the first frame, and the startup disk scan is
  gone with the lazy wizard.
- **158.** Shared instances instead of per-delegate ones: one
  `quantPopup`/`rowMenu` per download list (parameterized by `activeRow`,
  actions read it imperatively at click time), and one `BlockMiniToolbar` +
  eight-handle resize set per overlay tracking the selected box — a page no
  longer pays ~30 live objects per block.
- **168.** The Models window is one window («Модели»): each task row is
  `task | model ▾ | quant ▾ | ⚙`, the quant dropdown lists the profile's
  curated quants (`sizeBytes` in `resources/profiles/models/*.json`, status +
  family hint via `ModelQuantModel`, installed quants override with measured
  `entry.byteSize`), the Launch/Request tabs live behind the row's ⚙, and a
  «Загруженные файлы моделей на диске» block (`InstalledFilesModel`,
  `core/ByteSize::formatBytes`, removal through `removeInstalledEntry` with
  the shared-mmproj/in-use guards) replaces the separate manager window.
- **170.** The setup wizard is five steps with real hardware detection:
  `runtime/HardwareInfo` (DXGI adapters on Windows,
  `MTLCreateSystemDefaultDevice` on macOS, none on Linux) feeds
  `ReleaseCatalog::detectPlatform()`'s backend mapping and the 0.9 × VRAM/RAM
  budget for `ModelInstaller::defaultTaskChoices(budget)`; the check step is
  one «Скачать и проверить» button whose self-test success is the only way to
  Finish.
- **171.** The main-window header is the pipeline toolbar of labeled groups —
  «Документ», «Целевые страницы» (range field), «Разметка и OCR», «Проверки и
  AI-коррекция», «Настройки» — with the started action's button turning into
  the stop control. The pages field is a two-way editor of the thumbnail
  selection (`PageListModel::selectedPagesRange()`/`applyPagesRange()`, empty
  = whole document), «Проверить» always runs the decision pass without
  auto-fix, and the ADR 167 preset stub, the three-mode «Распознать ▾» picker
  and the «Проверить ▾»/«Экспорт ▾» menus are deleted.
- **174.** The preview overlay paints the page furniture grey:
  `BoxListModel::BoxKind::Service` (4) covers `header`, `footer`,
  `page_header`, `page_footer`, `page_number`, `seal` and maps to the
  `Theme.overlayService*` grey triple, decided by the label only so it does
  not depend on the model's style profile.
- **175.** The page preview zooms (0.25–8×, Ctrl/⌘+wheel around the cursor)
  and pans, the zoom menu lives in `PageToolBar`, the overlay is drawn in
  screen pixels so the chrome keeps its size at any zoom, and the block
  panel's overlap is `bottomInset` with `ensureVisible()` scrolling the
  selected block clear. `OcrImageProvider` serves the page at twice the
  requested size so zooming shows pixels.
- **177.** A block can be added by hand: `AppController::addBlock(type)`
  appends an empty `BoundingBox` of the canonical block type (page-toolbar
  buttons + the `BlockNames` menu) and selects it, and every block can be
  moved and resized. The page text follows the blocks only while it still is
  the block-derived text (see ADRs 189/190 for the later gesture and
  edit-marker refinements).
- **185.** The block panel tells the two layers apart: the fix outcome is
  stored on the block (`BoxFixStatus None/NotNeeded/Fixed/Damaged` — an OK fix
  stores nothing and leaves the text layer's verdict standing), the panel
  shows the layer's score (the P(match)) and one overall word
  («Распознан»/«Проверен»/«Исправлен»/«Утверждён»), `BoundingBox::useCorrectedText`
  picks the layer the page text follows (persisted, optional in
  `project.json`), and `BoxCheckStatus::Approved` (5) is the user's verdict
  overruling the model.
- **186.** The block panel is a version inspector: two cards (OCR text vs AI
  correction) — the one in view wears the accent and clicking switches
  `useCorrectedText` — one status word per card plus the overall state in the
  header, word-by-word tinting of the changes in place (`core/TextDiff.h`,
  never a strikethrough, the «Различия» switch), monospace for code-like
  blocks, «Утвердить»/«Сбросить исправление» actions with Ctrl/⌘+Return
  jumping to the next problem block, and a per-card «Изм.» hand edit.
- **187.** The approval can be revoked: the confirm button reads «Отозвать
  утверждение» and restores the verdict the approval replaced
  (`checkStatusBeforeApproval`/`correctedCheckStatusBeforeApproval`, optional
  `project.json` keys, schema version stays 2). A hand-written text is its own
  state `BoxCheckStatus::Edited` («Изменён вручную», no score), the edit exits
  through «Сохранить»/`Esc` (Ctrl/⌘+Return saves instead of approving), and
  the version card has one uniform frame.
- **188.** The panel loses its colour noise (no green buttons — the colours
  are the verdicts and the accent of the version in view), the «Различия»
  switch appears only when a second version exists, and the «Исправить (N)»
  badge counts the blocks still waiting for the fixer
  (`BoxTransitions::needsFix` is one set with the fix queue; a rejected
  correction re-enters the count). `scopedProblemBlockCount` was renamed
  `scopedPendingFixCount`.
- **189.** A click on a block selects it and a drag pans the page; the block
  moves only in the edit mode (the mini toolbar's move button; the checkmark,
  a click outside, another selection or a page change leaves it; resize
  handles visible only there). A new block is drawn on the page
  (`AppController::addBlockAt`, crosshair cursor + hue chip, `Esc` or a second
  click cancels), and a text-less block's status word is «Разметка» with no
  dot.
- **190.** A structural change is not a manual edit: the edited marker means
  exactly "the user typed the text by hand" — set by `setCurrentPageText()`
  alone, cleared by Revert. A block removal moves the revert target forward
  and never replaces the user's text, and a pipeline change landing on a
  hand-edited page is merged into that text (the common part anchors the
  change, the user's wording wins where it diverges).
- **199.** The page index is clamped to the document, not the manifest: a
  project whose embedded files are all missing opens to page 0
  (`max(0, pageCount() - 1)`) instead of hitting undefined behaviour with
  `std::clamp` and a model/controller disagreement about the current page.
- **200.** The models-table draft is a set of `list<QtObject>` entries (`key`,
  `quantId`, `modelName`, an «edited» flag per field): `syncDraft()` updates
  entries in place and refreshes only untouched fields, `resetDraft()`
  (Cancel) discards edits, `markDraftSaved()` clears the flags — an unsaved
  model name survives a re-sync.
- **201.** Exactly one owner of Ctrl+Return: the version card owns the key
  while it is being edited, and the block panel's «approve the block» shortcut
  is enabled only when no card is editing and the page editor is not focused.
- **208.** The block panel's removed/added ranges come from one diff per pair
  of texts, kept until the texts change (the comparison used to run twice,
  quadratically), and the tokens are compared through `QStringView` without
  per-cell copies.
- **238.** The block state lives in the frame: no frame = markup only, gray =
  recognized, red = checked with a problem (Mismatch, Review, invalid formula
  syntax), green = checked clean (Ok, approved, hand-written); the error frame
  is 1 px thicker so the problem block is found by eye. The confidence number
  (option «Показывать уверенность модели») is printed next to the block in the
  frame's color, without the circle backing; the block type stays on the inner
  frame and fill.
- **239.** The block panel header no longer repeats the status: each layer's
  verdict and number live in its card header («Распознанный текст (OCR)»,
  «Исправленный текст (AI)» — `MainWindow/BlockEditPanel.qml`), and a status
  word remains only where there are no cards (markup-only or image block) —
  one place per status, per ADR 185.
- **240.** The fixer is not a verifier: a fixer's «OK — nothing to fix»
  (`BoxTransitions::fixConfirmedOriginal`) clears the verdict of the layer the
  user reads and the block returns to gray/recognized; a layer with an applied
  fix loses its own verdict too. The frame color speaks only for the deciding
  model's verdict, and a page holding such a block is no longer «checked».
- **241.** Moving or resizing a block erases its contents
  (`AppController::onBoxRectChanged` → `BoxTransitions::blockRectChanged`):
  text, check verdict, fix and fix result belonged to the old area; the block
  is markup again (no frame, panel says «Разметка»), and the page text follows
  the blocks — rebuilt once per drag.
- **248.** The «повернуть боковую страницу после разметки» setting is a
  declared `Q_PROPERTY` (`orientationAutoTurnEnabled`, next to
  `orientationCheckEnabled`) with an `OptionCheck` checkbox instead of an
  `LLOCheckBox` — without the property `resetGroup()` missed it, QML read
  `undefined` and could not persist the checkbox, while the C++ getter reading
  QSettings directly masked the breakage.
- **256.** Auto mode has a single run button and no difficulty switcher — the
  «Быстро/Баланс/Точно» levels are gone; `layout` stands first in the task
  list (`core/ConnectionRole.h`) as the pipeline's first step, and the
  external-runtime window takes that list from C++ instead of keeping a QML
  copy. (The menu itself was rebuilt again in ADR 270.)
- **263.** A run button's width is set by its caption: the stop ring occupies
  the caption's place, so the button does not compress and the menu does not
  jump when a run starts. (The button names ADR 263 introduced were replaced
  by ADR 270's grid.)
- **264.** The scoped read-block count is part of the availability state:
  `AvailabilityPresenter::Flags` compares `scopedReadBlocks`, so the number at
  «Перераспознать» follows the scope even when no other availability flag
  changes.
- **269.** The setup wizard's verification stage stays quiet while things are
  fine: `selftestFinished` is ignored while `SelfTest.selftestRunning`,
  «Готово» opens only on the current run's `phase == "done"` (self-test state
  resets on open), `RuntimeInstaller::startDownloadAndInstall` fetches the
  release list itself when empty, «Назад» exists on the last step, and the
  quant auto-pick asks the task's model — it used to pick the biggest quant
  because the filter asked for a request-profile id (ADR 191).
- **270.** The main menu is one row of titled blocks (`MainWindow/Header.qml`):
  «Документ», «Целевые страницы» (range field with «Стр:» inside,
  «Авто-проход»), «Разметка и OCR», «Проверки и AI-коррекция», «Настройки».
  Button names are «Распознать блоки» and «Перераспознать» — ADR 263's names
  are cancelled; block captions are `Theme.captionUpper`, minimum window width
  1490, default 1540.
- **279.** Every dropdown is drawn by one component, `Common/LLOComboBox.qml`
  (flat frame, `Theme.surfaceAlt`, chevron-down from Lucide, flat popup); the
  theme switcher is a `Common/SegmentedToggle.qml` row («Как в ОС» / «Светлая»
  / «Тёмная», `monitor`/`sun`/`moon` icons) driven from outside — it reports
  `activated(index)` and never writes `currentIndex` itself, the theme reads
  `Settings.themeMode`. Captions use `Theme.bodySize`, values
  `Theme.bodySmall` (10 pt, added to `Theme.qml`); the interface settings
  window is 380×268. (ADR 285 replaces the accent coloring of the selected
  segment.)
- **280.** «Сохранять колонтитулы и номера страниц» (`output/keepPageNumbers`,
  property `keepHeadersAndPageNumbers`) drops the whole page-furniture
  dictionary (`core/ServiceLabels.h::isPageFurnitureLabel`:
  `header`/`footer`/`page_header`/`page_footer`/`page_number`) — parsers skip
  such blocks entirely, no text and no frame on the preview. The output
  settings window carries no per-option explanations (`Theme.bodySmall`
  labels, a `SegmentedToggle` for orientation, window 420×426); the wizard's
  «Вывод» step shows the same options.
- **281.** The verification settings window: thinking level is a
  `SegmentedToggle` («Выкл»/«Низкое»/«Среднее»/«Высокое») over the same stored
  token counts (`check/fixReasoningBudgetTokens`,
  `check/readReasoningBudgetTokens`, `reasoningBudgetSteps` 0/512/1024/1536 —
  see ADR 268); explanations moved to `docs/04-components.md`; block types are
  cards with C++-counted counters (`VerificationPromptStore::blockGroupCounts`);
  «Equations» and «Formulas» are one «Формулы» item
  (`VerificationPromptStore::canonicalType`, math labels via
  `LatexText::isMathLabel`, aliases mapped in `labels.json`);
  `BlockNames.hintMap` is deleted; lists keep natural height inside the cards;
  window 720×640.
- **282.** The runtime settings window: the connection mode is a
  `SegmentedToggle` (stored key `provider/mode` unchanged), one «Показать»
  checkbox reveals every key field of the External section
  (`Common/SecretKeyField.qml` only displays `revealed`), «Очистить
  неиспользуемые сборки» sits in the «Установить llama.cpp» section header
  (the «Установлено: …» caption is gone), the install row is one line
  (Backend/Release/check/download), fonts follow the other windows, window
  700×640. (The reveal switch's placement is amended by ADR 283.)
- **283.** A key is removed by emptying the field: the mask is the field's
  text (not a placeholder), an untouched field keeps the key while an emptied
  one clears it on save (`SettingsStore::setApiKey("")` → `clearApiKey`, same
  per role); the mask is never mixed with the typed key and the key is never
  shown (ADR 227). The «Показать» switch moved to the roles-table header next
  to the «API-ключ» column — still one switch for the whole tab (extends
  ADR 282); in the compact wizard step it stays by the single key.
- **284.** The models window matches the others:
  `Common/LLOComboBox.qml`'s custom `contentItem` inherits
  `font: control.font` (the system-size text was a component bug, fixed for
  every list at once), fonts follow the `Theme` tokens, the window and the
  parameters popup size to their content (popup width 620, height by the
  current tab, 340…500), long names elide instead of wrapping into
  fixed-height delegates, and `Common/ParamsTableEditor.qml` reports
  `implicitHeight` with a scrollbar.
- **285.** `Common/SegmentedToggle.qml` is one block: a `Theme.surfaceSunken`
  background with `Theme.border`, the selected segment as a card
  (`Theme.surfaceAlt` + border), segments of equal width (widest caption
  rules; extra stretch shared; invisible `Text` measures because `TextMetrics`
  cannot be a `Repeater` delegate). The blue accent is gone — selection is the
  card, text `Theme.textPrimary`/`Theme.textSecondary` (replaces the
  `Theme.cardAccent` part of ADR 279); behavior unchanged (controlled from
  outside, RadioButton, arrows/space) across all seven places.

### Security & secrets

- **33.** ⛔ No automatic attach to a process listening on the configured
  port — `/health==200` proves neither process identity nor the model; a
  foreign process cannot be owned, and a fixed busy port is a hard error.
- **35.** ⛔ No automatic removal of `com.apple.quarantine` — never bypass OS
  protection for downloaded executables; QNetworkAccessManager downloads are
  usually not quarantined anyway.
- **38.** The managed server binds loopback only (`127.0.0.1`/`::1`); other
  hosts require `runtime/allowNonLoopback=true` (advanced, with a clear
  warning), and the client `baseUrl` stays `127.0.0.1` even when bound
  elsewhere.
- **45.** The `Authorization` header is dropped on any redirect to a different
  host — otherwise the HF token would leak to the CDN/host the redirect points
  at.
- **94.** The raw request/response debug dump is opt-in via `LLOCR_RAW_DEBUG=1`
  (never keyed off the application name, which made it on in every release
  build), and the request body is dumped before the POST so aborted requests
  are recorded too — the dump writes full base64 page images into
  `<AppData>/raw-debug/` unrotated, a privacy and disk-growth hazard. Test:
  `TestOcrModels::rawDebugDumpIsOptIn`.
- **197.** The `file` field of a `.llocr` manifest is untrusted input: it is
  validated (relative path, no `.`/`..` segment, no drive letter, resolved
  path stays inside the extraction folder) instead of being pasted into the
  extraction path — `QDir::filePath()` neither normalizes `..` nor refuses an
  absolute argument; a project naming an outside file opens without it. Test:
  `TestProjectStore::manifestFileOutsideTheProjectIsNotOpened`.
- **211.** One URL policy: the first URL of a request goes through the same
  filter as a redirect (`HttpClient::get`), a pagination URL must match the
  request's scheme, host and port (`HttpClient::isSameOrigin`) or the catalog
  stops with a message, and `http` stays reserved for the loopback — the
  Hugging Face pagination URL no longer carries the token to another host.
- **227.** API keys and the Hugging Face token live in the OS secret store
  (macOS Keychain via Security.framework, Windows `CredWrite`/`CredRead`,
  Linux Secret Service over `Qt6::DBus`; no new libraries); the settings file
  keeps only endpoints and aliases. The old plaintext key migrates on start;
  if the store refuses, the key lives in memory until exit and the UI says
  so; a stored key is never shown back — mask plus «Заменить»/«Удалить».
- **232.** A macOS keychain item belongs to the build that wrote it (ad-hoc
  signature binds the `cdhash`, so a rebuild loses the key): dev builds sign
  with the self-signed «LLocr Dev» certificate (`scripts/macos-dev-sign.sh`,
  wired into the build when the identity exists) and release builds need
  Developer ID. The app tells the truth about failures, verifies a write by
  reading back, and updates an existing item in place instead of
  delete-and-recreate.
- **233.** A secret-store refusal is visible: `SecretStore::Status`
  distinguishes `Ok`/`NotFound`/`Refused`/`Unavailable`/`Failed`, a `Refused`
  blocks the request and the status names the cause and the remedy (Keychain
  prompt, or delete the stale `provider/apiKey` item), a 401 is translated
  into the app's own words («ключ не отправлен или не принят»), and a merely
  absent key is not a refusal. The refusal reason no longer dies in a
  `thread_local` on the worker thread.
- **271.** The managed server answers only to a one-time token: each
  `LlamaServerProcess::spawn()` mints a fresh `QUuid` passed as `--api-key`
  (new on every `start()`, including auto-restart after a crash), `api-key` is
  a reserved argument name (`LaunchProfile::reservedArgNames()`), all app
  probes (health polling, `/v1/models`) carry `Authorization: Bearer`, and a
  server that outlives the app keeps its token in `owner.json`
  (`ServerOwnerRecord::token`) for the attach path. Security-audit
  scenario #1.
- **272.** A model file without a published sha256 is not downloaded:
  `ModelInstallTransaction::enqueueFile` sets `requireSha256 = true`
  (generalizes ADR 212's runtime rule to models), and the transport refuses
  answers without `Content-Length` in both the 200 and 206 branches — a
  chunked unbounded response cannot fill the disk as a model. Security-audit
  scenario #9.
- **274.** Export and conversion treat recognized text as untrusted (merges
  ADR 273): the exported HTML carries the CSP
  `default-src 'none'; style-src 'unsafe-inline'; img-src data:; font-src
  data:` (a script smuggled in recognized text stays inert text), and
  `Exporter::pandocArgs` builds `--from=markdown-raw_tex` (embedded TeX never
  reaches the filesystem during LaTeX conversion) with `--output=<path>` as a
  single token. Tests: `test_exporter` (`pandocArgsDisableRawTex`,
  `pandocArgsOutputIsSingleToken`). Security-audit scenarios #3, #4, #15.
- **275.** The managed server starts only on loopback:
  `RuntimeController::startServer` requires `launchHost` to be `127.0.0.1`,
  `::1` or `localhost`, otherwise an error naming the explicit
  `allowNonLoopback` setting — the one-time token (ADR 271) protects only the
  local machine. Security-audit scenario #11.
- **276.** The release-catalog cache is bound to its source: `ReleaseCatalog`
  writes `sourceUrl` into the cache file and `readCacheFile` treats an entry
  from any other URL as foreign — a substituted or stale list from one address
  is never served for another, it is just a cache miss. Security-audit
  scenario #10.
- **277.** `ArchiveExtractor::kMaxArchiveBytes = 2 GiB` rejects the archive
  file itself (plus cumulative and per-entry uncompressed limits) before
  extraction, and `ProjectStore` unpacks `.llocr` with the same limit. A zip
  entry name with `\`, `:`, a control character, an absolute path or `..` is
  rejected, and symlink targets (including pax `linkpath`) pass the same
  check. Security-audit scenario #12.
- **278.** A plaintext key never passes through launch arguments: `api-key` is
  reserved and stripped from the user's launch string
  (`ServerLaunchConfig::toArguments`), a user profile with any unknown
  parameter is dropped whole (`LaunchProfileStore::dropUserProfiles`), and
  `LaunchProfile::isSecretLikeName` (`token`, `password`, `*-key`, `*_key`)
  puts saving such a string behind a confirmation dialog in `LaunchTab.qml`.
  Security-audit scenario #13.

### Testing & CI

- **99.** Every test gets a timeout and a headless platform by default: a
  directory-level loop in `tests/CMakeLists.txt` applies `TIMEOUT 300` and
  `QT_QPA_PLATFORM=offscreen` to all `add_test` targets. `test_export_renderer`
  instead runs with
  `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu --no-sandbox --single-process`
  (Chromium's Mach rendezvous aborts in restricted sessions), and an explicit
  per-test timeout still wins.
- **100.** One verification command, and CI runs the same one:
  `scripts/check.sh` does configure-if-needed → build →
  `ctest --output-on-failure` → `clang-format --dry-run --Werror` →
  `qmllint`, reusing an existing tree by default; a missing tool degrades to a
  notice, never a silent pass.
- **121.** CI is two tiers: a Qt-free format gate (`scripts/check.sh
  --format-only` → clang-format over `src/` and `tests/`) on every push, the
  full matrix on `workflow_dispatch` only; the concurrency group includes the
  event name so a manual run is not cancelled by a push. Qt 6.10.3 is an
  official online-installer build, so provisioning can use
  `jurplel/install-qt-action`/aqtinstall.

### Cut features

- **47.** A macOS watchdog-helper process (kqueue/NOTE_EXIT) is closed — not
  shipped: low value (one residual server process after a hard GUI kill,
  already detected at next start) vs high complexity (separate bundled binary,
  signing, poor testability); the best-effort no-orphan contract stays (see
  ADR 30).
- **78.** Free-form Hugging Face search/download is removed from the UI —
  models are installed from the preset catalog or a local GGUF file
  (`HfSearchList.qml` deleted, `ModelCatalog::search()` and the catalog
  Import/Export buttons removed); `hfToken` stays for gated preset downloads.
- **161.** The decision model d1-3B was removed: the harness measured AUC
  0.668 on the raw-string state and no phrasing verified formulas or tables
  (seven schemes tried), so a structured-state scheme replaced it — the d1-3B
  profile and its manual were deleted, the harness and the study remain.
- **251.** Everything cut by the release is kept in `archive/` — nine model
  profiles in `archive/models/profiles/`, their parsers in
  `archive/parsers/`, guides in `archive/models/docs/`, single-pass page
  recognition in `archive/single-pass/`, the text decision protocol with its
  requests and calibration in `archive/decision-text/` — under git, outside
  all CMake targets, with `archive/README.md` as the manifest and the tag
  `archive/pre-cleanup` holding the intertwined code
  (`git show archive/pre-cleanup:<path>`).
- **252.** The task matrix is four tasks — `blockRecognition`, `decision`,
  `layout`, `fix` (`core/ConnectionRole.h`); `pageOcr`/`rawPageOcr` are cut
  with every model that served them. MinerU covers layout and block
  recognition (default for both via `roles.<task>.default: true`),
  MiniCPM-OCR Jev decides, Qwen3.5-4B fixes; GLM-OCR joined block recognition
  and Qwen3.5-9B / Gemma-4 (E4B/12B) joined fix (PaddleOCR-VL, added on
  recognition, was cut again by ADR 255).
- **253.** Settings reset once instead of migrating: the whole migration
  machinery (deferred keys, task-key transfer tables, stale model/parser
  resolution, plaintext-key migration) is deleted, and a
  `settings/schemaVersion` mismatch runs `SettingsStore::resetToDefaults()` —
  only `runtime/rootDir` and `runtime/modelsDir` survive (they point at the
  user's disk), keys already in the OS secret store survive (ADR 227), and a
  plaintext key from before ADR 227 is lost and must be re-entered.
- **254.** The check task speaks only the visual diff: the text protocol
  `systemone_single_question_v7` (`match`/`has_text`/`readable`) is cut with
  clef-flash and d3-flash/d3-mini; MiniCPM-OCR Jev
  (`systemone_visual_diff_v1`) is the only deciding model — crop + rendered
  candidate + candidate text, one match probability from the noul head,
  question wording baked into the model, calibration thresholds the only
  setting. An unreadable crop lands in Mismatch and goes to REVIEW through
  the fix task.
- **255.** PaddleOCR-VL is cut: its profile and guide
  (`archive/models/profiles/paddleocr-vl.json`,
  `archive/models/docs/25-paddleocr-vl.md`) are in `archive/` with the rest
  (ADR 251), leaving MinerU (default) and GLM-OCR on block recognition —
  raising it to MinerU's level would mean a second layout step with its own
  protocol and parser, since its RT-DETR layout model is not shipped in GGUF.

## Appendix: removed decisions

Decisions deleted from the log during the Oct-2026 cleanup — fully superseded,
merged into a wider entry, or removed together with their feature. The
numbers stay reserved so historical references remain traceable.

| Removed | Where it went |
|---|---|
| 3 | Merged into **6** (RAG deferred) |
| 15 | Superseded by **59** (dead profile classes; the model-profile system replaced them) |
| 48 | Superseded by **227** (the keychain deferral was reversed by the OS secret store) |
| 65 | Superseded by **69** (DjVu optional at configure time) |
| 68, 71, 76 | Superseded by **164** (free-form second-model verification → two-layer verification + the decision task) |
| 90, 91 | Superseded by **178** (the automatic check/fix switches were removed) |
| 104, 105, 209 | Merged into **118** (GUI-thread discipline) |
| 125 | Superseded by **141**, now folded into **144** (launch draft composition) |
| 137–143 | Merged into **144** (speculative decoding exploration → the shipped MTP drafting) |
| 145 | Superseded by **164** + **254** (verification pipeline v1, text protocol cut) |
| 147 | Superseded by **164** + **254** (the d1-era decision role; visual diff is the only protocol) |
| 149 | Merged into **150** (layout pass with its own settings) |
| 152 | Superseded by **163** (the fix task carries the candidate principle) |
| 154 | Superseded by **254** (decision v6 wire, text protocol cut) |
| 160 | Superseded by **254** (decision profile v2, text protocol cut) |
| 162 | Superseded by **252** (six-task matrix → four tasks) |
| 166 | Superseded by **168** (two model windows → one) |
| 167 | Superseded by **171** (toolbar rework; the preset stub deleted) |
| 172 | Superseded by **184** (v6 calibration; the threshold principle survived) |
| 180 | Withdrawn by **192** (`span_hybrid` → `full_block`) |
| 219 | Superseded by **252** (Chandra-OCR-2 cut with the single-pass page tasks) |
| 221 | Superseded by **242** (the orientation model check removed; the detector is pixel-only) |
| 234 | Superseded by **251** (MiniCPM-OCR 1.0 cut) |
| 273 | Merged into **274** (export CSP) |
