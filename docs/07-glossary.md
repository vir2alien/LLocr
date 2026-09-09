# 07. Glossary and Adopted Decisions

## Terms
- **OCR** — recognizing text from images.
- **LLM provider** — the connection to the model; here always an
  **OpenAI-compatible** endpoint (llama.cpp server, Ollama, LM Studio, hosted API).
- **ProviderConfig** — the **transport** settings struct (base URL, API key,
  timeout). It is separate from the model settings: when a recognition starts,
  `RecognitionController` assembles an `OcrRequest` (model + prompt + generation
  params) from `SettingsStore` and a `ProviderConfig` (connection) for the
  provider.
- **OcrRequest** — the per-request payload for the provider: `image`, `prompt`,
  `modelId`, plus generation params (`temperature`, `maxTokens`, and the DRY
  sampling parameters). Defined in `providers/ILlmProvider.h`.
- **DRY sampling parameters** — llama.cpp's "Don't Repeat Yourself" sampler
  settings (`dry_multiplier`, `dry_base`, `dry_allowed_length`,
  `dry_penalty_last_n`), exposed in **Settings → Model** and sent in the request
  body. Tuned for the Unlimited-OCR model.
- **Output parser** — the strategy for parsing a model's response into a
  structured result (`raw`, `det_tokens`; default `det_tokens`), selected in
  Settings → Output.
- **BlockStyle** — mapping from model block labels (`title`, `image`, `chart`,
  `equation`, `table`, `ref_text`, captions, …) to Markdown rendering styles
  (`BlockStyle.h`).
- **bbox** — bounding box coordinates of a recognized fragment for overlay on
  the preview (produced by the `det_tokens` parser).
- **Image/chart block** — a `det_tokens` box labeled `image` or `chart`;
  editable on the preview (move / resize / delete) and exported as a cropped
  image.
- **Markdown preview** — client-side render of the current page's Markdown via
  Qt WebEngine + marked + KaTeX, toggled in the right text pane.
- **RAG** — Retrieval-Augmented Generation; here — indexing scans into a vector DB.
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
  `modelId` + `timeoutMs`. In `External` it mirrors settings; in `Managed` it
  is computed from the started server (loopback URL + `--alias`).
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
- **Model preset** — a pre-verified pair `model + mmproj + parser + prompt +
  ctx-size` (e.g. Unlimited-OCR Q4_K_M); built-in `default-presets.json` plus
  user `catalog.json`, merged by `id` (ADR 42/43).
- **ModelRegistry** — persists installed models in `<modelsDir>/index.json`;
  distinguishes managed vs external, recovers by rescan, guards removal.
- **GGUF** — llama.cpp's model format; vision models consist of a main
  `.gguf` (+ optional `mmproj` projector; multi-part `model-00001-of-N` files
  are supported).

## Adopted decisions (ADR-lite)
| #    | Decision                                                     | Reason                                                       |
| ---- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 1    | Qt6/C++/QML stack                                            | Existing skill, covers all GUI requirements                  |
| 2    | CMake instead of qmake                                       | Standard for cross-platform C++                              |
| 3    | RAG as a separate Python service                             | Python's ecosystem is stronger for ML/RAG                    |
| 4    | Build the OpenAI-compatible provider first                   | Supported by almost all local runners                        |
| 5    | Internal format — Markdown, export via Pandoc                | Cheap support for many formats                               |
| 6    | RAG deferred, only interface design sketched               | Working OCR first. Backend RAG interface stub not implemented yet — see Stage 4.
| 7    | PDF via Qt PDF (`QPdfDocument`), not MuPDF/Poppler           | No extra native dependency; bundled with Qt6; adequate at ~150 DPI. |
| 8    | Recognition is cancellable (`abort()`)                       | Users must be able to Stop long multi-page runs.             |
| 9    | Navigation stays live during recognition                     | Users browse other pages while a batch run proceeds.         |
| 10   | Export written directly (TXT/MD/HTML); Markdown-as-source + Pandoc for DOCX/PDF | Ship useful export now; converge on the single-source pipeline. |
| 11   | Thumbnails carry no bounding boxes                           | The strip only signals recognized/edited/current; boxes belong on the center preview only. |
| 12   | OpenAI-like(llama.cpp-like)-compatible only                  | One clear connection type; connection/model/parser/generation params live in `SettingsStore` (Settings dialog + `QSettings`). The recognition prompt is supplied by the chosen **model preset** (see ADR 43); a built-in default (`AppController::m_prompt`) applies when no preset is in use. |
| 13   | Window geometry persisted (`WindowSettings` + `SettingsStore` `ui/*`) | Persist window pos/size/visibility across sessions. |
| 14   | Unit tests live in `tests/` (Qt Test), gated by `LLOCR_BUILD_TESTS` | Early coverage for parsers; more to come. |
| 15   | JSON model-profile classes (`ModelProfile` / `ProfileRepository`) removed | Stage 2 replaced them with the **Settings dialog**; the leftover classes were dead code and were deleted. |
| 16   | i18n via Qt Linguist (`qsTr`/`tr` + `.ts`/`.qm`), language selectable in Settings (System/English/Русский), default **System**, runtime retranslate | Standard Qt i18n; persisted via `SettingsStore` (`ui/language`); embedded `.qm` loaded at startup and on change. |
| 17   | `det_tokens` parser consumes the `<|det|>…<|/det|>` block stream and JSON-unescapes the content (`\n` → newline, `\\` → `\`, …); legacy bare `label [x1,y1,x2,y2] text` stays as a fallback | The model streams one block per token with JSON-escaped text; the legacy form is kept for older responses. No `<PAGE>` splitting — one page is sent per request. |
| 18   | `det_tokens` also strips model control tokens from content — `<|end_of_sentence|>` (ASCII and full-width-pipe `｜ U+FF5C` + `▁ U+2581` variants) and any stray `<|…|>` wrappers | The model appends an EOS marker after the last block; it must not leak into the recognized text. |
| 19   | The `table` block style is recognized and rendered as a **GFM pipe table** (`\| a \| b \|`) built from the model's `<table>` HTML. `colspan`/`rowspan` are honored by laying cells into a dense grid (a spanned value is repeated across the occupied columns/rows, since GFM has no native spanning); cell math is converted via `convertMath`, and `|` / newlines inside cells are escaped. `table_caption`/`table_footnote` render italic like figure captions | The model emits table content as inline HTML (`<table><tr><td …`) inside the `table` det token; raw HTML must not leak into the markdown, so it is parsed into a pipe table. |
| 20   | The `ref_text` det-token (bibliography/reference list) parses like `text` and renders as a plain paragraph | The model labels reference entries (`[1] …`, `[2] …`) as `ref_text`; they are ordinary text runs (no heading/italic markers), keep their `ref_text` label, and get no extra Markdown styling. |
| 21   | **DRY sampling parameters** (`dry_multiplier`, `dry_base`, `dry_allowed_length`, `dry_penalty_last_n`) exposed in **Settings → Model** and sent in the request body | They materially affect recognition quality with llama.cpp for the Unlimited-OCR model; users can tune them without recompiling. |
| 22   | **Markdown preview** rendered client-side via **Qt WebEngine + bundled marked + KaTeX** (`resources/preview/`) | LaTeX/tables/images render locally with no network access; `image://ocr/crop/*` refs are converted to `data:` URIs before rendering. |
| 23   | Pages can be **deleted** and **drag-reordered** in the thumbnail strip; `PageEditStore` and `PageListModel` remap indices afterwards | Multi-page documents need page management; edits must follow their page across reorder/removal. |
| 24   | **Image/chart block editing** (move / resize / delete) directly on the preview; `rebuildPageText()` regenerates the page Markdown from the boxes | Editing regions is more convenient than re-running OCR; markdown image refs embed the box index, so removal forces a rebuild to keep `image://ocr/crop/<N>` indices consistent. |
| 25   | Detected **duplicate** bounding boxes are collapsed and flagged (`OcrPage::hasDuplicates`, `PageListModel` duplicate role → red marker) | The model can emit the same region twice; the parser dedups it and the UI surfaces it. |
| 26   | Two connection modes `External` / `Managed`; `OpenAiProvider` stays transport-only and knows nothing about `QProcess` | Don't break the existing external-server scenario; process management is a separate responsibility. |
| 27   | The managed server is **only llama.cpp** (`llama-server`), minimum build **b4000** | A single predictable CLI; Ollama/LM Studio have their own managers; below b4000 the CLI drifts too much. |
| 28   | Runtime installs come from **GitHub Releases** (`ggml-org/llama.cpp`) and are unpacked by our own code (vendored `miniz`), verifying `sha256` taken from the release body | Reproducibility, no external package manager; file size is *not* an integrity proof. |
| 29   | Models download **directly from Hugging Face**, with the **commit SHA pinned** and `sha256` verified against `lfs.oid` | No Python dependency; `main` is mutable between browsing and downloading. |
| 30   | No-orphan: **strong** on Windows (Job Object + `JOB_OBJECT_LIMIT_KILL_ON_CLOSE`) and Linux (`prctl(PR_SET_PDEATHSIG, SIGTERM)` via `ProcessGuard`), **best-effort** on macOS (`owner.json` + next-start detection) | macOS has no PDEATHSIG analog; honest formulation over a false promise (§5.4). |
| 31   | First-run wizard driven purely by `runtime/setupVersion`, **no network health probes**; pre-existing profiles are treated as configured (`setupVersion=1`), a clean profile gives `setupVersion=0` | A network probe is unreliable (VPN, powered-off server) and must not decide setup state; the version allows the wizard to evolve. |
| 32   | In `Managed`, `baseUrl` and `modelId` are **computed** from the running server (loopback + `--alias`); the `model/name` setting is never overwritten | The running process is the single source of truth; `External` settings must be preserved for switching back. |
| 33   | ⛔ No automatic attach to a process listening on the configured port | `/health==200` proves neither process identity nor the model; a foreign process cannot be owned — a fixed busy port is a hard error (§5 task 4). |
| 34   | ZIP **and** `.tar.gz` archives are supported; dispatch by extension in `ArchiveExtractor::extractArchive` | llama.cpp publishes `.zip` on Windows but `.tar.gz` on macOS and Linux (`llama-b<build>-bin-macos-<arch>.tar.gz`, `llama-b<build>-bin-ubuntu-*.tar.gz`); on macOS there is no ZIP asset at all. macOS tarballs contain symlinked `.dylib`s that the extractor recreates as relative links (ADR amended after the 2026 asset-format change). |
| 35   | ⛔ No automatic removal of `com.apple.quarantine` | Never bypass OS protection for downloaded executables; `QNetworkAccessManager` downloads are usually not quarantined anyway (§7.8). |
| 36   | `RuntimeController` is a singleton instance created in `main.cpp` before the QML engine loads; QML cannot instantiate it | Deterministic lifetime; no duplicate instances. |
| 37   | `RecognitionController` obtains the connection only through `ensureConnectionReady()` (External resolves immediately; Managed starts/waits later) | It must not know about modes, processes, or health checks. |
| 38   | The managed server binds **loopback only** (`127.0.0.1`/`::1`); other hosts require `runtime/allowNonLoopback=true` (advanced) with a clear warning | An unauthenticated server must not be reachable from the network; client `baseUrl` stays `127.0.0.1` even when bound elsewhere. |
| 39   | Runtime installation is **transactional**: staging → verify → probe → atomic rename → settings commit (ADR 28/34) | Eliminates partially-installed runtimes; any failure removes `staging/<uuid>` and leaves settings untouched. |
| 40   | `.part` downloads always live in the **target directory**, never in a shared `downloads/` | A rename across filesystems is not atomic — models are often kept on an external drive (§3.1). |
| 41   | Capability detection: build-allowlist + parsed `--help` refinement + one auto-retry without an offending flag on `unknown argument`; cached as `capabilities-<sha1(path+mtime)>.json` | llama.cpp's CLI drifts between builds (e.g. `--flash-attn` boolean → `on|off|auto`); blind argv building breaks compatibility. |
| 42   | Preset catalogs are split: built-in `:/models/default-presets.json` (read-only) + user `<AppData>/…/catalog.json` (read/write), merged by `id` (user wins) | Files inside Qt resources cannot be edited; the user must be able to add/customize presets. |
| 43   | A preset pins the exact pair `model + mmproj + parser + prompt + ctx-size` | `det_tokens` and the prompt are model-specific; an arbitrary vision GGUF yields unparseable output. |
| 44   | `autoStart` defaults to **off** | Otherwise the GUI would reserve several GB of RAM/VRAM on every launch, even when idle. |
| 45   | `Authorization` header is **dropped on any redirect to a different host** | Otherwise the HF token would leak to the CDN/host the redirect points at (§7.3). |
| 46   | Split locks instead of one global app lock: **`.install.lock`** (runtime installs/cleanup, in `RuntimeInstaller`), per-write **`.registry.lock`** (`ModelRegistry`), and **`.instance.lock`** as the **runtime-owner** gate (`SingleInstanceGuard`) | A second app instance can keep using External / browsing while runtime installs and server ownership stay exclusive; no single lock blocks everything (H.6). The lock is **re-checked when the Runtime settings tab opens** (`RuntimeController::refreshSingleInstanceLock()`): a window that started while another instance owned the runtime takes over as soon as the owner exits — no app restart needed. |
| 47   | Strong-quality no-orphan on macOS: the **watchdog-helper (kqueue/NOTE_EXIT) is closed — not shipped** in the MVP; the best-effort contract stays (`owner.json` + next-start detection, ADR 30) | Low value (only a hard-kill of the GUI on macOS, one residual server process, already detected at next start) vs high complexity (separate bundled binary, signing, poor testability) — unjustified for the MVP (H.4, §5.4). |
| 48   | Moving `provider/apiKey` and `hf/token` to the OS keychain is **deferred**; they stay in plaintext `QSettings` with an explicit UI warning | Existing behavior is functional and secret-free in logs/commands (§7.6); cross-platform keychain is non-trivial (3 OSes, Qt has no built-in API, async) — not worth the effort now (H.5). |
| 49   | Self-test and the server-log ring-buffer are **separate QML singletons** (`SelfTestController` / `RuntimeLog`), not `RuntimeController` roles | `RuntimeController` was a ~20-property "kitchen sink" facade; splitting removes the temptation to duplicate self-test/log logic (review 3.4). |
| 50   | `SettingsStore::resetToDefaults()` is **table-driven**: one `SettingDefault{kQSettingsKey, Q_PROPERTY name, QVariant default}` row per resettable key, written through `QMetaProperty::write()` (i.e. the setter — guards, validation, NOTIFY preserved) | A manually-maintained setter list drifted from the property declarations; the table is the single registry, `ui/window*` (UI state) is deliberately excluded, order keeps `baseUrl` before `connectionMode` and `lastExternalBaseUrl` last (review 3.5). |
| 51   | The `--version` + `--help` probe shares **one wall-clock budget**: `--version` gets the full `timeoutMs`, `--help` the remainder; if the budget is exhausted `--help` is skipped and flags fall back to the build allowlist | Previously each run got a fresh `timeoutMs`, so a hung binary could stall the UI for `2 × timeoutMs` (10 s worst case with the 5 s startServer timeout); the whole probe is now bounded by `timeoutMs` (§H.7 follow-up). |
| 52   | **Persistent capabilities cache**: successful probes are written to `<cacheDir>/capabilities-<sha1(path+mtime+size)>.json` and served by `probeCached(binary, cacheDir)` on the next app run | The in-memory probe cache only lasts a process; without a disk layer every app start re-spawned `--version`/`--help`. The key intentionally matches the in-memory `ProbeKey` strength (path+mtime+size) so any binary replacement invalidates. `probe()` (fresh “Check”) and the install path stay uncached by design (ADR 41). |
| 53   | Probing llama.cpp binaries uses a **long single-run timeout (120 s)** in the install flow AND in `RuntimeController` (Check, Start, Auto-detect), instead of the regular 5 s probe, and does **not** kill/retry | On macOS the FIRST run of a llama.cpp build past a cold Metal shader cache compiles kernel libraries (`ggml_metal_library_init` writes ~20–30 MB under `com.apple.metal`); until it finishes, `--version` blocks (observed ~17 s cold on an M-series Mac, longer under load; afterwards the cache is warm and probes answer in ~50 ms). A 5 s probe always fails on a cold cache; a retry loop that kills each attempt at 5 s repeatedly restarts the compile and never lets the cache finish. One patient run completes the cache; all later probes are fast. Warm-cache starts are unaffected (the 120 s budget is only a ceiling). |
| 54   | **Windows build toolchain = MSVC 2022 64-bit** (Qt Creator kit “Desktop Qt 6.10.3 MSVC2022 64bit”, generator `NMake Makefiles JOM`, Qt package `C:/Qt/6.10.3/msvc2022_64`); MinGW is **not** supported | The Qt **MinGW** package for Windows does **not** ship the **Qt WebEngine** module (Markdown preview, `resources/preview/`); the MSVC 2022 64-bit package does. Any MinGW-configured kit/project (e.g. `.qtcreator/CMakeLists.txt.user` with `win64_mingw_kit`) is switched to `win64_msvc2022_64_kit` and fails the build cleanly on WebEngine otherwise. macOS/Linux are unaffected (Clang/GCC). |
| 55   | Restored window position is **validated against the current screens** (`WindowSettings::visiblePosition`): the saved x/y is applied only while the window's title-bar band overlaps a connected screen, otherwise the window is centered on the **nearest** screen | A saved position from a previous session can point off-screen (monitor unplugged, resolution/arrangement changed, window closed while dragged past an edge) — the window would open with an unreachable title bar and couldn't be moved at all. Amendment to ADR 13. |

> When decisions change — add a row to the table and update the affected files.
