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
- **DjVuLibre development headers and native library** — optional; enables
  DjVu input. Without it the build succeeds and opening `.djvu` files reports
  an actionable error at runtime (ADR 69; see below).
- Pandoc — for DOCX/PDF export (external dependency, optionally bundled).
- Python 3.x — only for the RAG service (later stage).
- **Local runtime & models need none of the above**: llama.cpp downloads
  (GitHub Releases), Hugging Face GGUF downloads, ZIP + `.tar.gz` extraction
  (zlib) and the HTTP client are all embedded in the app — no external
  Python and no extra native tools required.

## DjVuLibre (optional, enables DjVu input)

DjVu input uses the **DjVuLibre C decoding API**, not a command-line converter.
LLocr CMake does **not** fetch, clone, build or install DjVuLibre. When the
dependency is absent (or incompletely configured), CMake prints a **warning**
and builds **without** DjVu support: `DjVuDocument.cpp` and the DjVu paths of
`DocumentModel` are compiled out, `test_djvu_document` and `test_app_import`
are skipped, and opening a `.djvu` file in the app reports an error advising
how to enable support (ADR 69; the ADR 65 hard requirement is superseded).
Pass `-DLLOCR_WITH_DJVU=OFF` to skip the discovery and the warning explicitly.

### macOS and Linux

Use your system's maintained development package. Examples:

```sh
# macOS / Homebrew
brew install djvulibre pkg-config

# Debian / Ubuntu
sudo apt install libdjvulibre-dev pkg-config

# Check the actual module name (not "djvulibre" or "libdjvu")
pkg-config --modversion ddjvuapi
pkg-config --cflags --libs ddjvuapi
```

On Unix, root CMake first tries `pkg_check_modules(... IMPORTED_TARGET
 ddjvuapi)` and exposes it as `DjVuLibre::DjVuLibre`. A custom prefix can be
made visible through `PKG_CONFIG_PATH` (the directory containing
`ddjvuapi.pc`). Alternatively set `DJVULIBRE_ROOT` or the explicit paths below
to bypass pkg-config. Keep the architecture consistent with Qt (notably
arm64 vs x86_64 Homebrew on macOS).

For a separate source build, upstream documents `./configure`, `make`,
`make install` for release archives; Git checkouts use `./autogen.sh` instead
of `./configure` and require Autotools. Set `--prefix` to a local dependency
installation directory and expose that installation to LLocr afterwards.
JPEG supports the uncommon JPEG-encoded DjVu files; TIFF is used by some
upstream utilities. Follow the prerequisites for the exact upstream release.
Prefer shared-library builds; the raw-library fallback does not infer static
archive dependencies or Windows static-link API definitions.

### Windows: prepare a native MSVC x64 dependency

**Do not run `vcpkg install djvulibre` or assume a `libdjvu` port.** On
2026-09-17 both the `djvulibre/portfile.cmake` URL reported for this task and
the fetched `libdjvu/portfile.cmake` / `libdjvu/vcpkg.json` paths under
`microsoft/vcpkg/master/ports` returned 404. vcpkg remains the documented
source of zlib, not a verified DjVuLibre installation route.

Use a trusted, matching MSVC x64 development build or prepare one separately
from [upstream DjVuLibre](https://github.com/DjVuLibre/djvulibre):

1. Select and record the upstream release/commit you build. Read its `README`
   Windows section, `win32/djvulibre/dirs.props`, and the prerequisite README
   files under `win32/`. The solution expects separate JPEG/zlib/TIFF source
   trees; an existing vcpkg zlib installation does not automatically populate
   those trees. The `libdjvulibre` project itself references `libjpeg`.
2. Open `win32/djvulibre/djvulibre.sln` in Visual Studio 2022. The upstream
   files inspected here define **only Debug/Release Win32**, with an explicit
   `MachineX86` linker setting. Retarget the toolset/SDK and create genuine
   **x64** configurations for the library and its dependency projects;
   update architecture-specific linker settings and inherited property
   sheets as needed. Merely selecting the x64 command prompt is insufficient.
3. Build the `libdjvulibre` **DLL** and its import library for Release (`/MD`)
   and, preferably, Debug (`/MDd`). Upstream uses the `libdjvulibre` basename
   for both configurations, in separate output directories. Retargeting this
   legacy solution with MSVC 2022 has **not been build-verified here**; these
   are preparation requirements, not a tested one-command upstream build.
4. Provide the headers and actual `.lib` paths to LLocr as below. A DLL alone
   is not a development package. Do not use a 32-bit or MinGW `.a` library
   with this application's MSVC x64 Qt kit.

### CMake discovery and overrides

The following cache variables are understood on every platform:

| Variable | Meaning |
| --- | --- |
| `LLOCR_WITH_DJVU` | Build option (default `ON`): look for DjVuLibre and enable DjVu input; automatic degradation with a warning when the dependency is absent. `OFF` skips discovery entirely |
| `DJVULIBRE_ROOT` | Optional dependency prefix; also bypasses Unix pkg-config |
| `DJVULIBRE_INCLUDE_DIR` | Directory **containing** `libdjvu/ddjvuapi.h`, not the `libdjvu` directory itself; the upstream source root is valid |
| `DJVULIBRE_LIBRARY_RELEASE` | Full Release library path (`.lib` import library on Windows) |
| `DJVULIBRE_LIBRARY_DEBUG` | Full Debug library path, in a separate directory if the filename is identical |
| `DJVULIBRE_DLL_RELEASE` / `DJVULIBRE_DLL_DEBUG` | Windows DLL matching the corresponding import library; override if automatic discovery fails |

Fallback discovery searches for `djvulibre` / `libdjvulibre` in `lib`,
`lib64`, `Release`, or `lib/Release`; Debug discovery looks under
`debug/lib`, `Debug`, or `lib/Debug` in the supplied root/prefixes and also
accepts a `d` suffix. For nonstandard layouts use the explicit variables.
Both configurations are preserved when present; RelWithDebInfo and MinSizeRel
use Release. If only one library is found it is used for all configurations
and CMake prints that choice. Supplying both matching builds is preferable.
Cached paths must be updated/cleared when switching dependency installations.

Example **staged layout** (create it from your own x64 build; these files
are not installed by LLocr or assumed to exist):

```text
C:/deps/djvulibre/include/libdjvu/ddjvuapi.h
C:/deps/djvulibre/lib/libdjvulibre.lib
C:/deps/djvulibre/debug/lib/libdjvulibre.lib
C:/deps/djvulibre/bin/libdjvulibre.dll
C:/deps/djvulibre/debug/bin/libdjvulibre.dll
```

With that layout, add `-DDJVULIBRE_ROOT=C:/deps/djvulibre` in Qt Creator's
CMake arguments and regenerate the existing tree. To specify each file
instead (from the repository root, after staging the files):

```bat
cmake -S . -B build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug ^
  -DDJVULIBRE_INCLUDE_DIR=C:/deps/djvulibre/include ^
  -DDJVULIBRE_LIBRARY_RELEASE=C:/deps/djvulibre/lib/libdjvulibre.lib ^
  -DDJVULIBRE_LIBRARY_DEBUG=C:/deps/djvulibre/debug/lib/libdjvulibre.lib
```

### Runtime deployment and validation

On Windows, building `llocr` copies the matching DjVuLibre DLL beside the
executable after linking (`copy_if_different`). Debug uses the Debug DLL;
Release/RelWithDebInfo/MinSizeRel use Release, with the same single-library
fallback as linking. DLL discovery checks the import-library directory and
nearby `bin` directories, plus `DJVULIBRE_ROOT` layouts `bin/Debug`,
`bin/Release`, `debug/bin`, `bin`, `Debug` and `Release`. Override
`DJVULIBRE_DLL_DEBUG` / `DJVULIBRE_DLL_RELEASE` for other layouts. A missing
DLL disables DjVu input support with a warning (the DLL copy step is then
omitted); it is no longer a configure error (ADR 69).

Qt DLLs and any additional dynamic dependencies of DjVuLibre still need to
be deployed or available on `PATH`. Tests built without building `llocr`
still need the DjVuLibre DLL on `PATH` or beside their executable. With the
example staged layout, for a Debug build use:

```bat
set PATH=C:\deps\djvulibre\debug\bin;C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
```

Use `C:\deps\djvulibre\bin` for Release (also for Debug if using only a Release
library). Do not mix Debug/Release DLLs with the same basename on `PATH`.
For packaged macOS/Linux builds, include the shared library and its runtime
closure and fix install names/RPATH as needed; do not assume `windeployqt` or
`macdeployqt` alone packages DjVuLibre. Include the notices/license and meet
GPL corresponding-source obligations for the exact shipped version and any
patches; see `THIRD_PARTY_NOTICES.md`.

After dependency preparation, regenerate the existing build, then build
`llocr`, `test_document_model`,
and `test_djvu_document`. The DjVu-dependent targets (`test_djvu_document`,
`test_app_import`) only exist when DjVu support is enabled. For example, in
the MSVC environment:

```bat
cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug --target llocr test_document_model test_djvu_document
ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug -R "^test_(djvu_document|document_model)$" --output-on-failure
```

Validated on this Windows machine with MSVC x64, Qt 6.10.3 and DjVuLibre
3.5.30: `llocr` builds and all 27 CTest targets pass, including 29 DjVu
checks. The local dependency prefix is `build/deps/djvulibre-msvc-x64`
(headers in `include`, import libraries in `lib/Debug` and `lib/Release`,
DLLs in `bin/Debug` and `bin/Release`). The Debug DLL was copied beside
`build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug/bin/llocr.exe`. Local build
recipes/provenance are in `build/deps/DJVULIBRE-HANDOFF.md` (untracked build
artifacts, not supplied by a fresh checkout). This dependency build uses a
local CMake wrapper, not the legacy upstream Visual Studio solution.
macOS/Linux and Release application builds still require validation.

Verified dependency references:
- [Upstream build README](https://github.com/DjVuLibre/djvulibre/blob/master/README)
- [pkg-config template](https://github.com/DjVuLibre/djvulibre/blob/master/libdjvu/ddjvuapi.pc.in)
- [Public API and license grant](https://github.com/DjVuLibre/djvulibre/blob/master/libdjvu/ddjvuapi.h)
- [MSVC library project](https://github.com/DjVuLibre/djvulibre/blob/master/win32/djvulibre/libdjvulibre/libdjvulibre.vcxproj)
- [Debian development package](https://packages.debian.org/stable/libdjvulibre-dev)

## Windows build (MSVC 2022 64-bit)
Qt ships separate Windows binaries for each toolchain. LLocr uses **Qt
WebEngine** (Markdown preview), and the module is only provided for the
**MSVC 2022 64-bit** package — there is no MinGW build of it. The Qt packages
live under the same version folder: `C:/Qt/6.10.3/msvc2022_64` (has
WebEngine) and `C:/Qt/6.10.3/mingw_64` (does **not** — Qt WebEngine is absent).

Prepare **DjVuLibre** first as described above, and keep its matching DLL
on `PATH` for the build/test commands below. These commands reuse the cached
`DJVULIBRE_*` arguments; a from-scratch configure also needs those arguments
(or a dependency prefix on `CMAKE_PREFIX_PATH`).

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
Unit tests are registered in `tests/CMakeLists.txt`, including
`test_document_model` and `test_djvu_document` (both compile `DocumentModel`
and `DjVuDocument` and link DjVuLibre), plus the runtime suite and the
`mock_llama_server` helper binary. Use `ctest --test-dir build -N` for the
current target list.

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
│   ├── core/         # OcrResult, ConnectionConfig (transport), OcrRequest,
│   │                 #   LlamaClient, RequestProfile, LaunchProfile
│   ├── models/       # OcrModel (abstract adapter), UnlimitedOcrModel,
│   │                 #   OcrModelFactory
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
│   └── app/          # AppController, RecognitionController, DocumentModel, DjVuDocument,
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
├── tests/            # document/DjVu + base + runtime suites; mock_llama_server helper
├── docs/
└── AGENTS.md
```

> **Note:** `OcrRequest` lives in `core/OcrRequest.h`;
> `ConnectionConfig` (transport, former `ProviderConfig`) and `LlamaClient`
> live in `src/core/`; OCR model adapters live in `src/models/`;
> `ResolvedConnection` and the runtime layer live in `src/runtime/`. QML lives
> under `resources/qml/`.
