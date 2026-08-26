# Review — `feature/llm-management` (local runtime & model management)

Scope: the full `feature/llm-management` branch (`git diff origin/main...feature/llm-management`,
~19 100 added lines across 97 files). Baseline for "intended behavior" is
`docs/09-local-runtime-plan.md` (stages A–H, ADR 26–48).

This report is a working artifact for further agent work: it lists bugs,
performance problems, architectural gaps, and documentation mismatches found
during review. It does **not** modify any code.

> Verification note: findings marked **[verified]** were re-checked against the
> source during review; the rest come from module-level analysis and carry the
> same confidence bar (≥80%). Line numbers are from the current branch HEAD.

---

## 1. Critical / High severity

### 1.1 [High] First-run wizard never auto-opens on a fresh profile — **FIXED**
- **File**: `resources/qml/Setup/StepDone.qml` (interacts with `resources/qml/Main.qml:155-178`)
- **Category**: Bug
- **Status**: [verified]
- **Description**: `StepDone` is a child of a `StackLayout`, and `StackLayout`
  instantiates **all** children eagerly. `Component.onCompleted: root.markDone()`
  therefore fires when the `SetupWizard` `Dialog` is constructed in `Main.qml`
  (at application startup), not when the Done step is reached. `markDone()`
  writes `Settings.setupVersion = 1`, so by the time the §4.4 `Timer` in
  `Main.qml:166` fires (400 ms later) the condition
  `setupVersion === 0 && !setupDismissed` is already false and the wizard never
  opens. This silently breaks the entire fresh-profile onboarding flow.
- **Recommendation**: Remove `Component.onCompleted: root.markDone()`. Mark
  completion only on the actual Finish transition (the Finish handler at
  `SetupWizard.qml:95` already calls `stepDone.markDone()`), e.g. in
  `SetupWizard`'s `onCurrentChanged` when `current === 4`.

### 1.2 [High] `InstallTransaction::cleanupStaging()` deletes the parent `runtime/` directory — **FIXED**

- **File**: `src/runtime/InstallTransaction.cpp:166-174`
- **Category**: Bug (data loss)
- **Status**: [verified]
- **Description**: `QDirIterator it{QDir(paths.stagingDir())}` uses the default
  `QDir::NoFilter`, which yields `.` and `..` as entries. Both pass `isDir()`,
  so `QDir(it.fileInfo().absoluteFilePath()).removeRecursively()` is invoked on
  `..` — i.e. on `runtimeDir()` — deleting **every installed `llama.cpp-*`
  build**. Currently latent only because `cleanupStaging()` is never called from
  production startup (see §2.14); the moment it is wired, this is catastrophic.
  (Compare `locateServer()` in the same file, which correctly skips `.`/`..`.)
- **Recommendation**: Use `QDirIterator{QDir(paths.stagingDir()), QDir::NoDotAndDotDot}`
  (or skip `.`/`..` explicitly), and add a test asserting `runtimeDir()` and
  sibling builds survive cleanup.

### 1.3 [High] CUDA `cudart` archive is extracted into `runtime/` root, not the install dir — **FIXED**

- **File**: `src/runtime/RuntimeInstaller.cpp:469,485`
- **Category**: Bug
- **Status**: [verified]
- **Description**: `const QString installDir = m_paths.runtimeDir();` is the
  runtime **root**, but the main archive is installed by
  `InstallTransaction::start()` into
  `runtime/llama.cpp-<build>-<backend>-<os>-<arch>/`. The cudart companion
  (`cudart64_*.dll`) therefore lands in `runtime/` instead of beside
  `llama-server.exe`, so a CUDA build fails with exactly the §7.5
  "cudart64_*.dll not found" error. It also violates Stage D task 3 ("both
  extracted into one directory") and the collision-logging rule.
- **Recommendation**: Extract into `QFileInfo(out.serverPath).absolutePath()`
  (equivalently `m_paths.installDir(out.tag)`), and add a collision check for
  files already produced by the main archive.

### 1.4 [High] `AppController::canRecognize()` does not implement the §1.4 formula — **FIXED**

- **File**: `src/app/AppController.cpp:81-84`
- **Category**: Bug
- **Status**: [verified]
- **Description**: It returns only `!m_settings.modelName().trimmed().isEmpty()` —
  the pre-feature check. It ignores `documentLoaded`, `busyState`, mode,
  `RuntimeState`, `configValid`, `autoStart`/`startOnDemand`. The correctly
  shaped `RuntimeController::canRecognize(bool)` (§1.4) is never called. In
  `Managed` the alias is read-only and `model/name` may be empty, so every
  Recognize action is blocked with "Set a model name in Settings first";
  conversely, Recognize stays enabled during `Downloading`/`Installing`/
  `StartingRuntime` when §1.4 requires `busyState == Idle`.
- **Recommendation**: Delegate to `m_runtime.canRecognize(!m_document.isEmpty())`
  and add `&& m_runtime.busyState() == AppBusyState::Idle`; drop the
  `modelName`-only logic.

### 1.5 [High] HF token is never sent to the tree / head-sha / search API — **FIXED**

- **File**: `src/runtime/ModelInstaller.cpp:345` (also `352`, `580`); `src/runtime/ModelCatalog.h:105-118`
- **Category**: Bug
- **Status**: [verified]
- **Description**: `ModelCatalog::fetchHeadSha`, `fetchTree`, and `search` take
  only `QNetworkAccessManager*` — no authorization parameter — and `beginPrepare`
  / `startSearch` call them without a token. The configured `hf/token` is used
  only later in `enqueueFile()` for the file download. For gated/private repos
  the tree/head-sha requests return 401/403 **before** download, so such repos
  can never be prepared or installed even with a valid read-only token.
- **Recommendation**: Thread an optional `authorization` argument through the
  three fetchers and set `Authorization: Bearer <token>` only on
  `huggingface.co` requests (keep the existing cross-host drop).

### 1.6 [High] `removeModel()` deletes files but never persists the registry update — **FIXED**

- **File**: `src/runtime/ModelInstaller.cpp:292-297`
- **Category**: Bug
- **Status**: [verified]
- **Description**: After `QDir(e.dir).removeRecursively()` it calls
  `refreshInstalled()`, which re-reads `index.json` from disk — where the entry
  is still present (no `ModelRegistry::save()`). The removed model therefore
  remains listed and can be re-activated via `setActiveModel()` while pointing
  at a now-missing file.
- **Recommendation**: Remove the entry from `m_installed`, `ModelRegistry::save(...)`,
  then refresh; surface a save failure to the UI.

### 1.7 [High] `.registry.lock` is never acquired (dead `lockPathFor`) — **FIXED**

- **File**: `src/runtime/ModelRegistry.cpp:116-119,162-187`
- **Category**: Threading / Architecture
- **Status**: [verified]
- **Description**: `save()` writes via `QSaveFile` but never takes
  `<modelsDir>/.registry.lock`; `lockPathFor()` is defined and never called. This
  contradicts H.6 / ADR 46 and the header/`AGENTS.md`/`docs/03-architecture.md`
  statements that a per-write lock guards model-index writes. A second GUI
  instance can perform a concurrent read-modify-write on `index.json`; the atomic
  rename protects against corruption but **not** against lost updates.
- **Recommendation**: Acquire a `QLockFile` on `lockPathFor(modelsDir)` around the
  read-modify-write (in `save` and in `ModelInstaller::completeInstall` /
  `removeModel` / `rescanRegistry`), releasing on all exit paths.

### 1.8 [High] Path traversal via `repoDirName()` (untrusted `repo`) — **FIXED**

- **File**: `src/runtime/ModelInstaller.cpp:22-27` (used at `373`, `413`, `506`)
- **Category**: Security
- **Status**: [verified]
- **Description**: `repoDirName()` only replaces `/` with `__`; it accepts `..`,
  absolute paths, and `\`. The `repo` field is attacker-controlled via an
  imported `catalog.json` (`importCatalog` → `ModelPresetCatalog::parse` →
  `ModelPreset::fromJson`, no sanitization). `beginDownload()` then
  `mkpath(m_pending.dir)` and downloads files there — i.e. it can write outside
  `modelsDir`. The canonical-path check in `removalError` protects against
  deletion, not against the write.
- **Recommendation**: Reject/escape path components before building the dir
  (split on `/`, drop `.`/`..`, forbid absolute paths and `\`, sanitize each
  segment), and re-validate `p.dir` against `modelsDir()` (canonical) before
  `mkpath`/download.

### 1.9 [High] `estimateModelMemory()` reads the whole multi-GB GGUF on the GUI thread — **FIXED**

- **File**: `src/runtime/ModelMemoryEstimator.cpp:66`
- **Category**: Performance
- **Status**: [verified]
- **Description**: `GgufReader`'s constructor does `m_data = m_file.readAll()`,
  loading the entire model file (typically several GB) just to parse the few-KB
  header. `estimateModelMemory` is `Q_INVOKABLE` and called synchronously from
  QML (`Setup/StepLaunch.qml`, `Setup/StepModel.qml`), so this freezes the UI
  and can OOM on 8 GB-class OCR models.
- **Recommendation**: Read only a bounded header prefix with `seek()`/`read()`
  (GGUF metadata is at the start of the file), and/or run the estimate on a
  worker thread.

### 1.10 [High] Fixed-port launch leaves an orphaned argv token — **FIXED**

- **File**: `src/runtime/RuntimeController.cpp:541` + `src/runtime/ServerLaunchConfig.cpp:132-133` (+ `LlamaServerProcess.cpp:100-103`)
- **Category**: Bug
- **Status**: [verified]
- **Description**: `ServerLaunchConfig::toArguments()` emits `["--port", "<value>"]`
  whenever `launchPort > 0`. `startServer()` then does
  `args.removeAll("--port")`, which removes only the flag and leaves the numeric
  value as a stray positional argument. `LlamaServerProcess::spawn()` separately
  appends a correct `--port <port>`, so a user-selected fixed port produces a
  malformed argv (e.g. `… 8080 … --port 8080`). The default `launchPort == 0`
  hides it; it triggers only for the explicitly-supported fixed-port feature
  (§5 task 4, §7.5).
- **Recommendation**: Remove the flag together with its following value, or
  better: set `cfg.port = 0` before `toArguments()` so `--port` is emitted only
  once (by `LlamaServerProcess`).

---

## 2. Medium severity

### 2.1 [Medium] Ready-path resolve skips `/v1/models` alias verification — **FIXED**

- **File**: `src/runtime/RuntimeController.cpp:195-197`
- **Category**: Bug
- **Status**: [verified]
- **Description**: When `m_state == Ready`, `beginManagedResolve()` calls
  `completeResolve(buildManagedConnection())` directly, skipping the §4.2 alias
  verification and the "`--alias` unsupported → first model id" fallback.
- **Recommendation**: Route the Ready case through `fetchManagedModels()` too.

### 2.2 [Medium] Managed connections use `startupTimeoutMs` as the per-request timeout — **FIXED**

- **File**: `src/runtime/RuntimeController.cpp:260`
- **Category**: Bug
- **Status**: [verified]
- **Description**: `buildManagedConnection()` sets
  `conn.timeoutMs = m_settings.startupTimeoutMs()` (default 180 000 ms) instead
  of `connectionTimeoutMs()` (default 120 000 ms). §1.3 documents `timeoutMs` as
  the `ProviderConfig` request timeout; the External path uses
  `connectionTimeoutMs()`. Each OCR/self-test request to a Managed server is
  therefore allowed 3 minutes to time out.
- **Recommendation**: Use `connectionTimeoutMs()`; keep `startupTimeoutMs` for
  the health watchdog only.

### 2.3 [Medium] `canRecognize` NOTIFY is only wired to `modelNameChanged` — **FIXED**

- **File**: `src/app/AppController.cpp:57-59` (property in `AppController.h:44`)
- **Category**: Bug
- **Status**: [verified]
- **Description**: `configChanged` is emitted only on `modelNameChanged`. It is
  not emitted on document load/removal or on any `RuntimeController` change
  (`stateChanged`, `configValidChanged`, `busyStateChanged`, `lockedOutChanged`).
  Even after fixing §1.4, the QML `enabled` bindings in `Header.qml:36-44` would
  not refresh when the runtime becomes `Ready`/`Stopped`/`Failed`/`configValid`
  flips.
- **Recommendation**: Additionally connect `configChanged` to `documentChanged`
  and the relevant `RuntimeController` signals.

### 2.4 [Medium] `cancelPendingStart()` can leave `busyState == StartingRuntime` — **FIXED**

- **File**: `src/runtime/RuntimeController.cpp:686-703`
- **Category**: Bug
- **Description**: `setBusyState(Idle)` runs only inside the
  `m_server->state() == Starting` branch. If the server is already `Ready` (the
  `/v1/models` window, still `busyState == StartingRuntime`) or `Stopping`, the
  branch is skipped and `failResolve()` completes without resetting `busyState`,
  leaving the footer progress indicator stuck.
- **Recommendation**: Move `setBusyState(Idle)` out of the conditional so it
  always runs before `failResolve()`.

### 2.5 [Medium] `lastExternalBaseUrl` is never saved/restored (§4.1, §7.9) — **FIXED**

- **File**: `src/app/SettingsStore.cpp:328-334`
- **Category**: Architecture
- **Status**: [verified]
- **Description**: `setConnectionMode()` just writes `provider/mode`; nothing
  calls `setLastExternalBaseUrl()`. The dedicated key is dead and the
  save-on-enter-managed / restore-on-return-to-external invariant is not
  enforced (masked today because the controller computes the Managed `baseUrl`
  in memory without clobbering `provider/baseUrl`).
- **Recommendation**: Implement save/restore in `setConnectionMode()`, or
  explicitly document that `provider/baseUrl` is never clobbered and remove the
  unused key.
- **Resolution**: `setConnectionMode()` now saves the external `provider/baseUrl`
  into `lastExternalBaseUrl` on switching to Managed and restores it on returning
  to External (never clobbers — the Managed URL is computed in memory).

### 2.6 [Medium] `runSelfTestQml()` lacks the External-mode guard `runSelfTest()` has — **FIXED**

- **File**: `src/runtime/RuntimeController.cpp:447-507` (vs `378-408`)
- **Category**: Bug
- **Description**: In External mode `runSelfTestQml()` issues a real OCR request
  against the external server instead of reporting "available only in Managed
  mode", because `ensureConnectionReady()` resolves immediately. The two methods
  are documented as equivalent but this invariant is broken.
- **Recommendation**: Add the same External early-return guard at the top of
  `runSelfTestQml()`.

### 2.7 [Medium] `LlamaServerProcess::stop()` blocks the GUI thread up to ~7 s — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:344-347`
- **Category**: Performance
- **Status**: [verified]
- **Description**: `stop()` calls `waitForFinished(graceMs=5000)` and, on
  timeout, `kill()` + `waitForFinished(2000)`. It is invoked synchronously from
  `RuntimeController::stopServer()`/`restartServer()`/`cancelPendingStart()` on
  the main thread, freezing the UI. §5.5 reserves blocking only for
  `shutdownSync()` (aboutToQuit).
- **Recommendation**: Make `stop()` async (`terminate()` + a grace timer to
  `kill()`, finalize `Stopped` from `onProcessFinished()`); keep `waitForFinished`
  only in `shutdownSync()`.
- **Resolution**: `stop()` now `terminate()`s immediately and kills via a grace
  `QTimer`; the `Stopped` state is finalized from `onProcessFinished()`. Callers
  in `RuntimeController` adapted; `waitForFinished` remains only in
  `shutdownSync()`. Tests updated to `QTRY_` waits.

### 2.8 [Medium] `stop()`/`shutdownSync()` do not cancel a pending auto-restart — **FIXED**

- **File**: `src/runtime/LlamaServerProcess.cpp:314-317,329,355`
- **Category**: Bug
- **Status**: [verified]
- **Description**: A `QTimer::singleShot(500, spawn)` scheduled after a crash is
  never cancelled by `stop()`, and the lambda does not re-check `m_stopRequested`.
  Stopping during the 500 ms restart window resurrects the server. `shutdownSync()`
  also does not set `m_stopRequested`, so `onProcessFinished()` can take the
  failure/restart branch during shutdown.
- **Recommendation**: Track/cancel the single-shot and guard the lambda (and/or
  `spawn()`) with `if (m_stopRequested) return;`; set `m_stopRequested = true` at
  the top of `shutdownSync()`.

### 2.9 [Medium] Health check never falls back to `/v1/models` as §5 requires — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp` (`onHealthReply`)
- **Category**: Documentation
- **Description**: §5 Stage B task 3 requires a `/v1/models` fallback when
  `/health` is unavailable. The implementation only inspects the `/health` HTTP
  status; `/v1/models` is queried only after `Ready`. A build serving models but
  not `/health` is marked `Failed`.
- **Recommendation**: Implement the fallback in `onHealthReply`, or update the
  plan/ADR to document `/health`-only readiness.
- **Resolution**: `onHealthReply()` now falls back to a `GET /v1/models` probe
  (once per startup attempt) that counts a 200 + `data` body as healthy.

### 2.10 [Medium] `completeInstall()` ignores `ModelRegistry::save()` failure — **FIXED**

- **File**: `src/runtime/ModelInstaller.cpp:539` (commit at `543-552`)
- **Category**: Bug
- **Status**: [verified]
- **Description**: The save result is discarded; on failure the code still writes
  `launch/*`/parser/ctx settings, emits `installedChanged`, and reports
  "Installed". This violates §7.2 (settings committed last, only after a verified
  install) and can leave the durable registry record absent.
- **Recommendation**: Check the return value; on failure set `State::Error`, stop,
  surface `saveErr`, and do not commit settings.

### 2.11 [Medium] `importCatalog()` persists the full merged catalog (freezes built-ins) — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:665-673`
- **Category**: Architecture
- **Status**: [verified]
- **Description**: `merged` is seeded from `m_presets` (already built-in + user)
  and written wholesale to the user `catalog.json`. After one import the user
  file shadows all built-in presets, preventing future built-in-resource updates
  (ADR 42 intends the user catalog to hold only overrides).
- **Recommendation**: Persist only user overrides (merge `incoming` into the
  user catalog, filtering entries identical to built-ins); `exportCatalog` may
  still export the merged view.
- **Resolution**: Imports are merged into the existing user catalog and entries
  identical (id + JSON) to built-ins are dropped before persisting.

### 2.12 [Medium] Downloads use the leaf name, discarding the repo subdirectory — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:426-428,443,486` (+ `selectModelFiles` 46-64)
- **Category**: Bug
- **Status**: [verified]
- **Description**: `selectModelFiles` collects `f.name` (leaf) not `f.path`, and
  `enqueueFile` uses that leaf in `resolveUrl(repo, sha, name)`. Files stored in
  a subdirectory produce a 404, and same-named files in different subdirs
  collide. §Stage E task 1 requires encoding each `path` segment.
- **Recommendation**: Carry the full repo-relative `path` through
  `selectModelFiles` → `Pending` → `enqueueFile`, keeping the leaf only for
  display.
- **Resolution**: Full repo-relative paths are carried through and per-segment
  URL-encoded; leaf-name collisions fail fast with a clear error.

### 2.13 [Medium] Orphaned `staging/*` cleanup is never invoked at startup — **FIXED**

- **File**: `src/runtime/InstallTransaction.h:48-51` (no production caller)
- **Category**: Documentation
- **Description**: ADR 39 / Stage D task 5 require "orphaned `staging/*` cleaned
  at application start". `cleanupStaging` is referenced only by the test.
  Interrupted installs leave `staging/<uuid>` trees forever.
- **Recommendation**: Call `InstallTransaction::cleanupStaging(m_paths)` during
  startup (only after fixing §1.2) and add a test for the production path.

### 2.14 [Medium] Non-atomic replacement of an existing install directory — **FIXED**

- **File**: `src/runtime/InstallTransaction.cpp:142-153`
- **Category**: Architecture
- **Status**: [verified]
- **Description**: Before the final rename, the code unconditionally
  `removeRecursively()` the existing `finalDir`. If the subsequent `rename()`
  fails, the previous good install is already gone — violating ADR 39's
  "atomic rename" and §7.2 transactionality for the replace-existing-build case.
- **Recommendation**: Rename the old dir aside first, rename the new one into
  place, then delete the old one (roll back on failure); use an exchange-style
  rename where available.

### 2.15 [Medium] QML `FileDialog.selectedFile` (url) assigned straight to QString setters — **FIXED**

- **File**: `resources/qml/SettingsDialog.qml:876`, `resources/qml/Setup/StepRuntime.qml:196`, `resources/qml/Setup/StepModel.qml:350`
- **Category**: Bug
- **Description**: `selectedFile` is a `url`; assigning it to
  `Settings.serverPath` / `Settings.launchModelPath` (`QString`) does an implicit
  `QUrl::toString()` producing `"file:///…"`. Nothing normalizes it, so probe /
  launch / memory-estimate all fail. `AppController::openFiles` (`.cpp:177-181`)
  shows the correct pattern (`toLocalFile()`).
- **Recommendation**: Convert to a local path before assigning (C++ setter/helper
  accepting `QUrl`, or a `localPath(QUrl)` `Q_INVOKABLE`). Avoid
  `replace("file://","")` hacks in QML.

### 2.16 [Medium] Catalog import/export receives a `url` instead of a path — **FIXED**

- **File**: `resources/qml/ModelsTab.qml:354,365`
- **Category**: Bug
- **Description**: `importCatalog(selectedFile)` / `exportCatalog(selectedFile)`
  pass a `url` to `QString` parameters; the implicit `"file:///…"` breaks the
  `QFile`/JSON read/write in `ModelInstaller`, and the returned error is swallowed.
- **Recommendation**: Change these `Q_INVOKABLE` methods to accept `const QUrl&`
  and normalize with `toLocalFile()`, or pass a pre-converted local path.

### 2.17 [Medium] Installed-model list is stale after `Activate` — **FIXED**

- **File**: `resources/qml/ModelsTab.qml:61`
- **Category**: Bug
- **Description**: `property var info: ModelInstaller.installedInfo(index)` is a
  binding that depends only on `index`; `installedInfo()` is a `Q_INVOKABLE` that
  reads internal state directly, so it does not subscribe to `installedChanged`.
  `setActiveModel(index)` changes `active` without changing `installedCount`/`index`,
  so the "Active"/"Activate" state stays stale until a rebuild.
- **Recommendation**: Bind to a reactive source (e.g. a real
  `QAbstractListModel`, or `ModelInstaller.activeTitle === info.title` with
  `installedChanged` NOTIFY), or refresh in `Connections.onInstalledChanged`.

### 2.18 [Medium] §4.2 read-only model name / alias in Managed mode not implemented — **FIXED**
- **File**: `resources/qml/SettingsDialog.qml:348-355`
- **Category**: Documentation
- **Description**: The Model-tab `modelNameField` has no `readOnly` gate on
  `connectionMode === "managed"`, no alias display, and no "defined by the
  running server" caption; and there is no managed/external switch in Settings
  (mode is only settable via the wizard), which undercuts §7.9.
- **Recommendation**: Add the read-only alias display + caption, and a mode
  selector so users can return to External without re-running the wizard.
- **Resolution**: Model-name field is read-only in Managed, shows the running
  server's alias with a caption, and a Managed/External ComboBox was added.

### 2.19 [Medium] `ThumbPanel` empty-state reads a non-existent `count` property — **FIXED**

- **File**: `resources/qml/MainWindow/ThumbPanel.qml:10`
- **Category**: Bug
- **Status**: [verified]
- **Description**: `controller.pageModel.count === 0` accesses `count` on a
  `QObject*` property; `PageListModel` exposes only `rowCount()`, so the access
  is `undefined` and the §H.1 "No pages" hint can never appear.
- **Recommendation**: Use the existing `controller.pageCount` (and re-check the
  `hasImage` condition, which already implies `pageCount > 0`).

### 2.20 [Medium] Model row-action errors are written to an invisible label — **FIXED**

- **File**: `resources/qml/ModelsTab.qml:117,355,366`
- **Category**: UX
- **Description**: `removeModel`/`importCatalog`/`exportCatalog` errors are
  assigned to `statusMsg.text`, but `statusMsg` has `visible: false` and nothing
  ever shows it, so failures produce no feedback (§7.5/§7.9).
- **Recommendation**: Bind `statusMsg.visible` to `text.length > 0` or route
  errors into the existing status `Label`.

---

## 3. Low severity

### 3.1 [Low] Auto-restart transitions through `Failed` before restarting — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:304`
- **Category**: Bug
- **Description**: `markFailed()` is called before the restart decision, so the
  state flashes `Failed` between automatic restarts and
  `RuntimeController::onServerStateForResolve` may treat it as terminal.
- **Recommendation**: Skip `markFailed()` when auto-restart is eligible; enter
  `Failed` only when the 3/5-min budget is exhausted.

### 3.2 [Low] `classifyLine()` emits a status-signal storm for tensor/`print_info` lines — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:245`
- **Category**: Performance
- **Description**: Raw `print_info`/tensor lines are surfaced as status, emitting
  `statusMessageChanged` per distinct line during model load.
- **Recommendation**: Emit a fixed placeholder (or nothing) for non-milestone lines.

### 3.3 [Low] Log file reopened and rotation re-checked on every output line — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:218`
- **Category**: Performance
- **Description**: `appendLine()` opens the log in `Append` and stats it for
  rotation on every line; thousands of lines ⇒ thousands of open/close+stat calls.
- **Recommendation**: Keep a `QFile`/`QTextStream` open in append mode; check
  rotation less frequently.

### 3.4 [Low] `autoDiscover()` returns a PATH candidate without probing it — **FIXED**
- **File**: `src/runtime/RuntimeLocator.cpp:128`
- **Category**: Bug
- **Description**: The first branch returns `findExecutable("llama-server")`
  unprobed, contradicting §5 "validity = successful probe". A broken/incompatible
  PATH entry is returned and the fallback scan is skipped. Also calls
  `findExecutable` twice.
- **Recommendation**: Probe before returning; cache the `findExecutable` result.

### 3.5 [Low] IPv6 loopback host (`::1`) produces a malformed URL — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:84` (and `RuntimeController.cpp:255`)
- **Category**: Bug
- **Description**: `http://%1:%2` with `::1` yields `http://::1:8080/health` (not
  bracketed). §7.4 explicitly allows `::1`.
- **Recommendation**: Assemble via `QUrl::setHost/setPort` (or bracket IPv6).

### 3.6 [Low] `shutdownSync()` does not set `m_stopRequested` — **FIXED**
- **File**: `src/runtime/LlamaServerProcess.cpp:355`
- **Category**: Bug
- **Description**: Unlike `stop()`, shutdown can route `onProcessFinished()` down
  the failure/restart branch (see §2.8).
- **Recommendation**: Set `m_stopRequested = true` at the top.

### 3.7 [Low] `QObject::tr()` used in non-QObject classes — **FIXED**
- **File**: `src/runtime/RuntimeLocator.cpp:30`, `src/runtime/RuntimePaths.cpp:82`
- **Category**: Documentation / i18n
- **Description**: `RuntimeLocator`/`RuntimePaths` are plain classes; `QObject::tr`
  resolves to translation context `"QObject"`, so strings won't match `lupdate`.
- **Recommendation**: Use `Q_DECLARE_TR_FUNCTIONS(...)` + `tr(...)`, or
  `QCoreApplication::translate("Class", ...)`.

### 3.8 [Low] `configValid` checks only file existence, not "probe passed" — **FIXED**
- **File**: `src/runtime/RuntimeController.cpp:88-106`
- **Category**: Documentation
- **Description**: §1.4 defines `configValid` as "valid binary (probe passed) AND
  model file exists"; the code only checks `QFileInfo(...).isFile()`.
- **Recommendation**: Run `probeCached` in `recomputeConfigValid()`, or update
  the wording to match the weaker intent (with a comment).

### 3.9 [Low] `resetToDefaults()` does not clear `lastExternalBaseUrl` — **FIXED**
- **File**: `src/app/SettingsStore.cpp:34-89`
- **Category**: Bug
- **Description**: A stale `provider/lastExternalBaseUrl` persists after "Restore
  Defaults" (currently low impact because the key is unused — see §2.5).
- **Recommendation**: Add `setLastExternalBaseUrl(QString())`.

### 3.10 [Low] `probeRuntimePath()` doc comment references a non-existent property — **FIXED**
- **File**: `src/runtime/RuntimeController.h:126-127`
- **Category**: Documentation
- **Description**: The comment says the summary is stored in `probeResult`; the
  implementation writes it to `statusMessage` (`.cpp:607-613`).
- **Recommendation**: Reword the comment.

### 3.11 [Low] External models have no "hide from list" path despite the error text — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:279-290`
- **Category**: Documentation
- **Description**: `removalError` returns "can only be hidden from the list" for
  external entries, but no unregister-without-delete action exists.
- **Recommendation**: Add an "unregister" path that rewrites `index.json` without
  touching files.

### 3.12 [Low] Rescan-recovered entries use `org__repo` as repo id / license URL — **FIXED**
- **File**: `src/runtime/ModelRegistry.cpp:207-210`
- **Category**: Documentation
- **Description**: `e.repo = subdirInfo.fileName()` and
  `license = "https://huggingface.co/%1"` produce a broken repo id / license link
  on rescan.
- **Recommendation**: Persist the true `org/repo` id in the entry and reconstruct
  it on rescan, or omit the license link rather than emitting a broken URL.

### 3.13 [Low] Corrupt/missing index is rescanned but not persisted — **FIXED**
- **File**: `src/runtime/ModelRegistry.cpp:127-148`
- **Category**: Documentation
- **Description**: `load()` sets `rebuilt=true` and returns the scan result but
  never writes it back, so `index.json` stays corrupt and every launch re-scans
  ("restore the registry" per Stage E).
- **Recommendation**: Persist the recovered entries after a rebuild (respecting
  the lock from §1.7).

### 3.14 [Low] `selectModelFiles` largest-single selection is O(n²) — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:91-98`
- **Category**: Performance
- **Description**: For each single candidate it scans the whole tree. Runs on a
  worker thread, so not a UI block, but wasteful.
- **Recommendation**: Build a `QHash<QString,qint64>` name→size once.

### 3.15 [Low] Multi-part first part relies on undocumented tree sort order — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:74-86` (vs `ModelRegistry.cpp:228-240`)
- **Category**: Bug
- **Description**: `selectModelFiles` does not sort split parts by index, so
  `modelNames.first()` (used as `--model`) may not be `-00001-of-N`.
- **Recommendation**: Reuse the same split-index sort as `scanModelsDir`.

### 3.16 [Low] Redundant `fetchHeadSha` for pinned presets — **FIXED**
- **File**: `src/runtime/ModelInstaller.cpp:345-347`
- **Category**: Performance
- **Description**: `beginPrepare` always fetches the head SHA, then overwrites it
  with `preset.revision` when pinned — a wasted round-trip.
- **Recommendation**: Skip `fetchHeadSha` when `pin` is non-empty.

### 3.17 [Low] Whole ZIP archive loaded into memory with multiple copies — **FIXED**
- **File**: `src/runtime/ArchiveExtractor.cpp:475` (+ `626`, `643`, `673`)
- **Category**: Performance
- **Description**: `f.readAll()` plus `data.mid(...)`, a decoder `out` vector, and
  `packBytes(out)` can hold several times the archive size in RAM.
- **Recommendation**: Stream/memory-map the archive and write decompressed bytes
  incrementally.

### 3.18 [Low] DEFLATE `stored` block bypasses the declared-size output cap — **FIXED**
- **File**: `src/runtime/ArchiveExtractor.cpp:239-253`
- **Category**: Security
- **Description**: `m_cap` is enforced for fixed/dynamic blocks but not
  `storedBlock`, weakening the anti-bomb guarantee (consistency/defense-in-depth).
- **Recommendation**: Add the same `m_cap` check in `storedBlock`.

### 3.19 [Low] Synchronous main-download failure doesn't abort the cudart enqueue / releases the lock early — **FIXED**
- **File**: `src/runtime/RuntimeInstaller.cpp:357-383`
- **Category**: Bug
- **Description**: If the main download completes synchronously (e.g. non-https,
  mkpath failure), `enqueueDownload(main)` reaches `maybeFinishDownloads()`,
  transitions to `Error`, and releases the install lock; control then returns to
  `beginDownloads()` and still enqueues the cudart download with an inconsistent
  pending count.
- **Recommendation**: After enqueuing the main download, check task state/`m_state`
  and bail before enqueuing the companion.

### 3.20 [Low] `sanitizeFileName()` has no collision-suffix resolution — **FIXED**
- **File**: `src/runtime/DownloadTask.cpp:82-120`
- **Category**: Documentation
- **Description**: Stage C task 1 requires "collisions resolved by a suffix"; the
  function only strips/replaces dangerous chars and reserved names.
- **Recommendation**: Implement suffix resolution at the target-dir/name choice
  point, or document it as the caller's responsibility.

### 3.21 [Low] `detectPlatform()` misclassifies 32-bit x86 as `arm64` — **FIXED**
- **File**: `src/runtime/ReleaseCatalog.cpp:311-314`
- **Category**: Bug
- **Description**: `arch.contains("64") ? "x64" : "arm64"` reports `i386`/`i686`
  as `arm64`.
- **Recommendation**: Handle `x86`/`i386`/`i686`/`x86_64`/`amd64` explicitly.

### 3.22 [Low] `progressChanged()` emitted on every `readyRead` chunk — **FIXED**
- **File**: `src/runtime/DownloadTask.cpp:441`
- **Category**: Performance
- **Description**: The 200 ms throttle governs speed/eta only; the signal itself
  is emitted per chunk and propagates through `DownloadManager::recalcAggregate()`
  (O(n)) and QML.
- **Recommendation**: Throttle the signal emission (~100-200 ms) or only emit on a
  meaningful byte delta.

### 3.23 [Low] Final-file promotion is delete-then-rename, not atomic replace — **FIXED**
- **File**: `src/runtime/DownloadTask.cpp:558-559`
- **Category**: Architecture
- **Description**: `QFile::remove(m_finalPath)` immediately before
  `rename(m_partPath, m_finalPath)` destroys the previous valid file if the rename
  fails.
- **Recommendation**: Let `rename(2)` replace atomically; fall back to
  remove-then-rename only where required, restoring on failure.

### 3.24 [Low] File-dialog `nameFilters` not wrapped in `qsTr` — **FIXED**
- **File**: `resources/qml/ModelsTab.qml:352,362`, `resources/qml/Setup/StepModel.qml:347`
- **Category**: i18n
- **Description**: `"JSON files (*.json)"`, `"All files (*)"`, `"GGUF models (*.gguf)"`
  are hardcoded English (§7.10 / Stage F task 4).
- **Recommendation**: Wrap in `qsTr(...)`.

### 3.25 [Low] "License" affordance is dead code (plain text, not a link) — **FIXED**
- **File**: `resources/qml/ModelsTab.qml:338-340`, `resources/qml/Setup/StepModel.qml:327-330`
- **Category**: UX
- **Description**: The label sets `linkColor`/`onLinkActivated` but the text has no
  `<a href>` anchor, so it looks clickable but is not.
- **Recommendation**: Render an actual `<a href>` when a URL is available, or drop
  the link styling.

### 3.26 [Low] Misspelled identifiers — **FIXED**
- **File**: `resources/qml/SettingsDialog.qml:87` (`dryAllowedLenghField`), `:73` (`thtemeIdx`)
- **Category**: Bug (style)
- **Description**: Misspellings (`Lengh`, `thteme`) that are self-consistent but
  harm readability and violate the "English identifiers" rule.
- **Recommendation**: Rename to `dryAllowedLengthField` / `themeIdx`.

### 3.27 [Low] `flushToDisk()` reinterprets the CRT fd as a native Windows `HANDLE` — **FIXED**
- **File**: `src/runtime/DownloadTask.cpp:29-40`
- **Category**: Bug
- **Description**: `reinterpret_cast<HANDLE>(file.handle())` on a CRT `int` fd is a
  bogus handle; `FlushFileBuffers` fails silently (return ignored), so the
  "flush+fsync before rename" guarantee is not achieved on Windows.
- **Recommendation**: Use `reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()))`
  (`<io.h>`) and check `FlushFileBuffers`' return value.

### 3.28 [Low] Case-sensitive duplicate-path detection on case-insensitive filesystems — **FIXED**
- **File**: `src/runtime/ArchiveExtractor.cpp:577`
- **Category**: Security
- **Description**: The duplicate-normalized-path `QSet<QString>` is case-sensitive,
  so `Foo`/`foo` can overwrite each other on Windows/default macOS.
- **Recommendation**: Normalize the key to lowercase (or use case-insensitive
  comparison).

---

## 4. Tests & configuration

### 4.1 [Medium] Presets ship without a pinned revision and without digests — **FIXED**
- **File**: `resources/models/default-presets.json:8,17,23,32`
- **Category**: Configuration
- **Description**: Both presets set `"revision": ""` and `"sha256": {}`. This
  leaves the "precise compatible bundle" (ADR 43) unpinned (files can drift on
  `main`) and provides no preset-level digest fallback when `lfs.oid` is missing.
- **Recommendation**: Pin a real commit SHA and fill per-file digests, or document
  the always-pin-via-head-sha + lfs.oid + GGUF-magic fallback and add a schema test.

### 4.2 [Medium] Revision pinning and tree pagination are untested — **FIXED**
- **File**: `tests/test_model_catalog.cpp:66-194`
- **Category**: Coverage gap
- **Description**: Only offline helpers (`resolveUrl`, `nextPageUrl`) are tested.
  `fetchHeadSha` and the multi-page `fetchTree` loop (the core of ADR 29) are never
  driven even against a local fixture server.
- **Recommendation**: Add a loopback `QTcpServer` fixture serving a repo `sha` and a
  two-page tree with `Link: rel="next"`, and assert `fetchHeadSha`/`fetchTree`.

### 4.3 [Medium] ModelRegistry atomic-write recovery is untested — **FIXED**
- **File**: `tests/test_model_registry.cpp:86-109`
- **Category**: Coverage gap
- **Description**: `atomicWriteRoundtrip` only does a clean save/load. No test
  simulates a leftover QSaveFile temp / truncated index, and none asserts
  `.registry.lock` is actually acquired.
- **Recommendation**: Add recovery and lock-assertion tests.

### 4.4 [Medium] `InstallTransaction` "failure at each step" is incomplete — **FIXED**
- **File**: `tests/test_install_transaction.cpp:180-186`
- **Category**: Coverage gap
- **Description**: Covers size/sha/non-ZIP but not (a) an extraction failure
  inside the transaction (zip-slip/duplicate) or (b) a locate/probe failure where
  the extracted binary doesn't answer `--version`.
- **Recommendation**: Add cases asserting `!out.ok`, `!committed`, and no leftovers.

### 4.5 [Medium] `canonicalPath` symlink-escape protection is untested (and the test is vacuous) — **FIXED**
- **File**: `tests/test_model_registry.cpp:141-150`
- **Category**: Coverage gap
- **Description**: The test only asserts `canonicalPath()` returns a non-empty
  string; it never verifies that a symlink inside `modelsDir` pointing outside is
  refused by `removalError`.
- **Recommendation**: Create a real symlink target outside `modelsDir` and assert a
  non-empty removal reason.

### 4.6 [Low] Built-in `default-presets.json` and `ModelPresetCatalog` are untested — **FIXED**
- **File**: `tests/CMakeLists.txt` (no `test_model_preset_catalog` target)
- **Category**: Coverage gap
- **Description**: No test exercises merge-by-`id` precedence, import/export/reset,
  or a parse+`fromJson` roundtrip of the shipped resource.
- **Recommendation**: Add a `test_model_preset_catalog` target.

### 4.7 [Low] `LlamaServerProcess` instances leaked in `test_server_process.cpp` — **FIXED**
- **File**: `tests/test_server_process.cpp:49,65,79,93,110,124`
- **Category**: Test quality
- **Description**: `new LlamaServerProcess(opts)` with no parent and no `delete`;
  the destructor (which would tear down the child) never runs.
- **Recommendation**: Use `QScopedPointer` or a parent + `destroyed` teardown.

### 4.8 [Low] Mock server `readyRead` doesn't buffer across TCP segments — **FIXED**
- **File**: `tests/mock_llama_server.cpp:110-159`
- **Category**: Test quality
- **Description**: Routes on a single `readAll()` with no per-socket buffering; a
  split request would be misrouted to 404. (`TestServer` in
  `test_download_manager.cpp` does this correctly.)
- **Recommendation**: Buffer per socket until `\r\n\r\n`.

### 4.9 [Low] Misleading compression-ratio comment — **FIXED**
- **File**: `tests/test_archive_extractor.cpp:175-177`
- **Category**: Test quality
- **Description**: The "≈136:1" comment is inaccurate (a repeating 256-byte block
  compresses much better). The test still passes because the real ratio is under
  the 200:1 limit.
- **Recommendation**: Correct the comment / state "well below the 200:1 limit".

---

## 5. Recommended remediation order

Progress: §1.1–§1.10, §2.1–§2.20, §3.1–§3.28, and **§4.1–§4.9 (test/coverage
hardening)** are **fixed** (marked in the sections above). The remediation is
complete; only the informational section §6 and the pre-existing loopback-test
issues noted below remain.

1. ~~Onboarding regression (§1.1)~~ — done.
2. ~~Data-loss / security bugs (§1.2, §1.8)~~ — done.
3. ~~Managed recognition gating (§1.4, §2.3)~~ — done.
4. ~~Gated-repo + registry persistence (§1.5, §1.6, §1.7, §2.10)~~ — done.
5. ~~Install correctness (§1.3, §2.13, §2.14, §1.10)~~ — done.
6. ~~Performance (§1.9, §2.7, §3.2/§3.3, §3.14/§3.17/§3.22)~~ — done.
7. ~~QML path/state bugs (§2.15–§2.20, §3.24–§3.26)~~ — done.
8. ~~Settings/architecture (§2.5, §2.11, §2.12, §3.4–§3.23, §3.27–§3.28)~~ — done;
   remaining **§2.18** note: read-only alias + mode selector implemented.
9. ~~Test/coverage hardening~~ (§4.1–§4.9) — done: base-URL test hook for
   `fetchHeadSha`/`fetchTree` + loopback fixture (4.2); recovery/lock/symlink
   tests in `test_model_registry` (4.3, 4.5); extraction + probe-failure cases
   in `test_install_transaction` (4.4); new `test_model_preset_catalog` target
   incl. a shipped-resource schema test (4.1, 4.6); `QScopedPointer` ownership
   in `test_server_process` (4.7); per-socket buffering in the mock server
   (4.8); corrected the compression-ratio comment (4.9). Validate with
   `cmake --build build -j 8 && ctest --test-dir build`.

Note: `test_server_process`, `test_ensure_connection`, `test_download_manager`
and two cases in `test_model_catalog` require binding a loopback TCP port (the
mock server / `QTcpServer`). Inside the sandboxed terminal they fail at
`listen()` with "mock: listen failed" and cannot be exercised here; they pass
outside the sandbox. All of these now pass outside the sandbox. The two suites
that previously carried **pre-existing** failures (unmasked once the loopback
port is available) were fixed:

- `test_download_manager` — the 3 resume/range failures were caused by
  `resolveFileNameCollision()` treating an existing `.part` as a collision and
  renaming the resumed file to a `-1` suffix, so the task then looked for a
  different part path and never resumed ($§ Stage C). The resolver now only
  avoids collisions with the **final** target name; a `.part` is intentionally
  reused so resume keeps the exact same part path. Concurrent same-name
  downloads into one directory cannot occur in practice (model installs are
  serialized by the install lock and use distinct leaf names; runtime archives
  differ), so this does not reopen the parallel-collision case.
- `test_ensure_connection` — the suite hung after the first managed test because
  `LlamaServerProcess::~LlamaServerProcess()` only closed the log file and left
  a still-running child orphaned ("QProcess: Destroyed while process is still
  running"), leaking live mock servers / ports that made successive tests flaky.
  The destructor now terminates (and, if needed, kills + waits on) a still-running
  child, which also guarantees no server is orphaned on teardown.
  `healthTimeoutSurfacesError` was additionally corrected to pass
  `--never-healthy --no-models` (the `--no-models` flag keeps the §2.9 `/v1/models`
  fallback from rescuing readiness, so the test actually exercises the watchdog
  timeout) and `autoRestart=false` so the failed start cannot be resurrected.

Verified green outside the sandbox: all 19 ctest targets pass with no orphaned
mock processes left behind (see §4.1–§4.9 and the fixes above). Specific
loopback tests were confirmed pre-existing (not caused by the §4 changes) by
A/B testing against the original code.

The documentation inconsistencies listed in §6 were also reconciled:
- `docs/09-local-runtime-plan.md` §1.4 now documents `configValid` as “file
  exists” (not “probe passed”), §8 gains `test_model_preset_catalog` + the
  hardened coverage (§4.2–§4.6), and §9’s ADR list was extended to 48 and now
  references `07-glossary.md` (the earlier `07-glossary-decisions.md` name was
  wrong).
- `docs/07-glossary.md` ADR table (26–48) already reflects the implemented
  behaviors: split locks (46), watchdog-helper not shipped (47), keychain
  deferred (48), no-orphan incl. the `~LlamaServerProcess` stop (30/47).
So every item in §6 is resolved in code **and** the supporting docs now match.

---

## 6. Documentation inconsistencies (summary)

The plan/ADR documents several behaviors that the code does not fully implement
or that had drifted. Highest-signal items, already detailed above — **all now
resolved in code and the supporting docs updated** (see §5):

- §4.4 wizard trigger — broken (§1.1).
- §4.2 read-only alias + `/v1/models` fallback — partial; Ready path skips it
  (§2.1, §2.18).
- §7.4 / §5 health `/v1/models` fallback — not implemented (§2.9).
- ADR 39 / §7.2 transactionality — orphan cleanup not wired (§2.13) and existing
  install replaced non-atomically (§2.14).
- H.6 / ADR 46 `.registry.lock` — not enforced (§1.7).
- §4.1/§7.9 `lastExternalBaseUrl` — not implemented (§2.5).
- §5 Stage C collision-suffix — not implemented (§3.20).
- §1.4 `configValid` = "probe passed" — actually "file exists" (§3.8).
- Stage E "restore the registry" on corruption — not persisted (§3.13).
- Stage E "external → hide from list" — no path (§3.11).
- `default-presets.json` schema (`revision`, `sha256`) — shipped empty (§4.1).
- §8 test coverage claims (revision pinning, pagination, atomic-write recovery,
  install failure-at-each-step, symlink escape) — under-tested (§4.2–§4.5).
