# 06. Development Environment and Distribution

## Tooling
| Area             | Tool                                        | Status |
| ---------------- | ------------------------------------------- | ------ |
| Build system     | CMake                                       | ✅ used |
| Version control  | Git + GitHub/GitLab                         | ✅ used |
| CI/CD            | GitHub Actions (builds for Win/macOS/Linux) | ⬜ todo |
| C++ dependencies | vcpkg — **Windows only**, provides ZLIB (`x64-windows`); not used on the macOS build | 🟡 |
| Formatting       | clang-format                                          | ✅ used |
| Tests            | Qt Test (unit tests in `tests/`)                      | ✅ done |

> **Build note:** the `dev` preset targets vcpkg + Ninja, but that is not the
> setup in use. The active `build/` is configured with **Unix Makefiles**
> against an **external Qt 6.10.3** (`CMAKE_PREFIX_PATH=/Users/gladskih/Qt/6.10.3/macos`);
> `VCPKG_ROOT` is unset and vcpkg does not participate in the build. Do not try
> to re-configure from the preset — reuse the existing `build/`.
>
> **On Windows** vcpkg **does** participate: the Qt Creator MSVC2022 kit sets
> the vcpkg toolchain (`C:/vcpkg`, triplet `x64-windows`), which provides
> **ZLIB** for `find_package(ZLIB REQUIRED)` — see the Windows build section
> below and `THIRD_PARTY_NOTICES.md`.

## Distribution — ⬜ not started
| OS      | Format             | Tool                            |
| ------- | ------------------ | ------------------------------- |
| Windows | .exe installer     | Inno Setup / NSIS + windeployqt |
| macOS   | .dmg (signed)      | macdeployqt                     |
| Linux   | AppImage / Flatpak | linuxdeployqt / flatpak-builder |

## Environment dependencies
- Qt6 (incl. **Qt PDF**, **Qt WebEngine**, **Qt Positioning**, **Qt WebChannel**,
  and **Qt LinguistTools** modules), a C++ compiler. **On Windows the compiler must be MSVC 2022 64-bit
  (kit “Desktop Qt 6.10.3 MSVC2022 64bit”); the MinGW Qt build does not ship
  the Qt WebEngine module** (see ADR 54). On macOS/Linux Clang/GCC work too.
- Pandoc — for DOCX/PDF export (external dependency, optionally bundled).
- Python 3.x — only for the RAG service (later stage).
- **Local runtime & models need none of the above**: llama.cpp downloads
  (GitHub Releases), Hugging Face GGUF downloads, ZIP + `.tar.gz` extraction
  (zlib) and the HTTP client are all embedded in the app — no external
  Python and no extra native tools required.

## Windows build (MSVC 2022 64-bit)
Qt ships separate Windows binaries for each toolchain. LLocr uses **Qt
WebEngine** (Markdown preview), and the module is only provided for the
**MSVC 2022 64-bit** package — there is no MinGW build of it. The Qt packages
live under the same version folder: `C:/Qt/6.10.3/msvc2022_64` (has
WebEngine) and `C:/Qt/6.10.3/mingw_64` (does **not** — Qt WebEngine is absent).

ZLIB (ZIP/gzip decompression in `ArchiveExtractor`) is resolved by
`find_package(ZLIB REQUIRED)`; on Windows the vcpkg toolchain provides it
(`C:/vcpkg/installed/x64-windows`, version 1.3.x) — see `THIRD_PARTY_NOTICES.md`.

### Windows: setting up vcpkg + zlib (first-time, in Qt Creator)
On Windows LLocr needs ZLIB at configure time (`find_package(ZLIB REQUIRED)`),
and it is **not** bundled with the source. On a fresh machine, set it up once
as follows:

1. **Install vcpkg** (if not present). Open PowerShell as Administrator (a
   normal prompt works too if you don't want to write under `Program Files`):
   ```cmd
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```
2. **Install zlib for MSVC 64-bit**:
   ```cmd
   C:\vcpkg\vcpkg.exe install zlib:x64-windows
   ```
3. **Point the project at vcpkg.** In Qt Creator: *Projects → Build & Run →
   your kit → Build Settings*, and in the **CMake arguments** field add:
   ```
   -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
   The command-line equivalent for a from-scratch configure is shown in the
   “From scratch” example below.

Ready-made MSVC trees exist at `build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug`
and `..._Release`; reuse them (run `cmake .` to re-generate), don't reconfigure
from scratch:

```bat
:: In an “MSVC … x64 Developer Command Prompt” (vcvars64.bat), Debug tree:
cd build\Desktop_Qt_6_10_3_MSVC2022_64bit_Debug
set PATH=C:\Qt\Tools\QtCreator\bin\jom;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
cmake.exe .
jom.exe -j 8          :: build llocr + tests
set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%   :: Qt DLLs for running tests
ctest.exe --test-dir . -j 4
```

From scratch (no existing tree), inside the MSVC 2022 environment with the
plain CMake from cmake.org:

```sh
cmake -S . -B build/win-msvc2022 -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build/win-msvc2022 -j 8
ctest --test-dir build/win-msvc2022
```

In Qt Creator the kit “Desktop Qt 6.10.3 MSVC2022 64bit” uses the bundled CMake
with the **NMake Makefiles JOM** generator (JOM is Qt's parallel make) and the
vcpkg toolchain from the kit environment; both paths resolve the same Qt
package and the same vcpkg ZLIB.

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
