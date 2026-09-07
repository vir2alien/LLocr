# 06. Development Environment and Distribution

## Tooling
| Area             | Tool                                        | Status |
| ---------------- | ------------------------------------------- | ------ |
| Build system     | CMake                                       | ✅ used |
| Version control  | Git + GitHub/GitLab                         | ✅ used |
| CI/CD            | GitHub Actions (builds for Win/macOS/Linux) | ⬜ todo |
| C++ dependencies | vcpkg (configured in `dev` preset, not used in the current build)   | 🟡 |
| Formatting       | clang-format                                          | ✅ used |
| Tests            | Qt Test (unit tests in `tests/`)                      | ✅ done |

> **Build note:** the `dev` preset targets vcpkg + Ninja, but that is not the
> setup in use. The active `build/` is configured with **Unix Makefiles**
> against an **external Qt 6.10.3** (`CMAKE_PREFIX_PATH=/Users/gladskih/Qt/6.10.3/macos`);
> `VCPKG_ROOT` is unset and vcpkg does not participate in the build. Do not try
> to re-configure from the preset — reuse the existing `build/`.

## Distribution — ⬜ not started
| OS      | Format             | Tool                            |
| ------- | ------------------ | ------------------------------- |
| Windows | .exe installer     | Inno Setup / NSIS + windeployqt |
| macOS   | .dmg (signed)      | macdeployqt                     |
| Linux   | AppImage / Flatpak | linuxdeployqt / flatpak-builder |

## Environment dependencies
- Qt6 (incl. **Qt PDF**, **Qt WebEngine**, and **Qt LinguistTools** modules),
  a C++ compiler (MSVC / Clang / GCC).
- Pandoc — for DOCX/PDF export (external dependency, optionally bundled).
- Python 3.x — only for the RAG service (later stage).
- **Local runtime & models need none of the above**: llama.cpp downloads
  (GitHub Releases), Hugging Face GGUF downloads, ZIP + `.tar.gz` extraction
  (zlib) and the HTTP client are all embedded in the app — no external
  Python and no extra native tools required.

## Install, tests, run
```sh
# from the repo root (see AGENTS.md for the exact Qt location):
cmake --build build -j 8            # app + tests
ctest --test-dir build              # all unit tests (base + runtime suite)
# run the app:
./build/bin/llocr                   # (exact binary name per platform)
```
Unit tests are registered in `tests/CMakeLists.txt` (4 base + 14 runtime =
18 ctest targets, plus the `mock_llama_server` helper binary; no test touches
the real network).

## Data directories (managed runtime & models)
The managed runtime, models, cache and logs live under the platform app-data
root (default `<AppData>/LLocr/`, overridable via `runtime/rootDir` and
`runtime/modelsDir`):

```
<AppData>/LLocr/
├── runtime/
│   ├── .install.lock                       # QLockFile (installs)
│   ├── staging/<uuid>/                     # temporary extraction
│   └── llama.cpp-<build>-<backend>-<os>-<arch>/  # installed build
│       └── llama-server[.exe]
├── models/
│   ├── .registry.lock
│   ├── index.json                          # ModelRegistry
│   ├── catalog.json                        # user preset catalog
│   └── <org>__<repo>/                      # downloaded GGUF(s) + .part
├── cache/
│   └── releases.json                       # GitHub releases cache (TTL 6 h)
└── logs/
    └── llama-server.log                    # rotating, 5 MB × 3
```

Notes:
- `runtime/` and `models/` may be relocated via Settings (`SettingsStore`
  `runtime/rootDir`, `runtime/modelsDir`); files are **not** moved when the
  path changes — the UI warns and offers a rescan of the model registry.
- Partial downloads (`.part` + `.part.meta`) always live **next to** the target
  file so the final rename stays atomic (ADR 40) — not in a shared downloads/.
- The single-instance (runtime-owner) lock is `<rootDir>/.instance.lock`; when
  another instance holds it, Managed **server** operations are blocked (External
  still works). Installs are guarded separately by `runtime/.install.lock` and
  model-index writes by `models/.registry.lock` (ADR 46), so a 2nd instance can
  install runtime/models while the 1st uses External.
- `owner.json` (written by `ProcessGuard` on macOS) records the managed server
  PID/port so an orphaned server can be detected at next start (ADR 30).
- Environment variables: none are required. The app follows the platform proxy
  settings (`QNetworkProxyFactory::useSystemConfiguration()`) for downloads.

## Repository structure (current)
```
LLocr/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/         # OcrResult, ProviderConfig (transport)
│   ├── providers/    # ILlmProvider (OcrRequest), OpenAiProvider
│   ├── parsers/      # IOutputParser, RawParser, DetTokensParser,
│   │                 #   ParserFactory, BlockStyle
│   ├── runtime/      # ConnectionMode, ResolvedConnection, RuntimeState,
│   │                 #   RuntimeController, RuntimePaths, RuntimeLocator,
│   │                 #   ServerCapabilities, ServerLaunchConfig,
│   │                 #   LlamaServerProcess, ProcessGuard_*, DownloadTask,
│   │                 #   DownloadManager, ReleaseCatalog, ArchiveExtractor,
│   │                 #   InstallTransaction, ModelCatalog, ModelPreset,
│   │                 #   ModelPresetCatalog, ModelRegistry, ModelInstaller,
│   │                 #   RuntimeInstaller, ModelMemoryEstimator, SingleInstanceGuard
│   └── app/          # AppController, RecognitionController, DocumentModel,
│                     #   PageListModel, BoxListModel, PageEditStore,
│                     #   OcrImageProvider, SettingsStore, UiController,
│                     #   I18n, Exporter, PageIndex.h
├── resources/
│   ├── qml/          # Main.qml, SettingsDialog.qml, ExportDialog.qml,
│   │   │             #   Theme.qml, WindowSettings.qml, SetupWizard.qml,
│   │   │             #   ServerLogWindow.qml, ModelsTab.qml
│   │   ├── MainWindow/  # Header, ThumbPanel, ThumbDelegate, ImagePanel,
│   │   │                #   ImagePreview, WorkPanel, MarkdownPreview, Footer
│   │   └── Setup/       # StepWelcome, StepRuntime, StepModel, StepLaunch, StepDone
│   ├── preview/      # marked + KaTeX + preview.html (Markdown preview)
│   ├── models/       # default-presets.json (built-in preset catalog)
│   ├── icons/         # app-icon.svg + llocr-*.png + llocr.ico / llocr.icns +
│   │                  #   hicolor/** (Linux) + llocr.rc (Win) + llocr.desktop.in
│   └── i18n/          # llocr_ru.ts (compiled/embedded by qt_add_translations)
├── rag-service/      # Python service (later stage) — empty for now
├── tests/            # base + runtime suites (18 ctest targets; mock_llama_server helper)
├── docs/
└── AGENTS.md
```

> **Note:** `OcrRequest` lives in `providers/ILlmProvider.h`;
> `ProviderConfig` lives in `core/ProviderConfig.h`; `ResolvedConnection` and
> the runtime layer live in `src/runtime/`. QML lives under
> `resources/qml/`.
