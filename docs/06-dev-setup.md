# 06. Development Environment and Distribution

## Tooling
| Area             | Tool                                        | Status |
| ---------------- | ------------------------------------------- | ------ |
| Build system     | CMake                                       | ✅ used |
| Version control  | Git + GitHub/GitLab                         | ✅ used |
| CI/CD            | GitHub Actions (builds for Win/macOS/Linux) | ⬜ todo |
| C++ dependencies | vcpkg (configured in `dev` preset, not used in the current build)   | 🟡 |
| Formatting       | clang-format                                          | ✅ used |
| Tests            | Qt Test (unit tests in `tests/`)                      | 🟡 partial |

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
- Qt6 (incl. **Qt PDF module** for PDF input), a C++ compiler (MSVC / Clang / GCC).
- Pandoc — for DOCX/PDF export (external dependency, optionally bundled).
- Python 3.x — only for the RAG service (later stage).

## Repository structure (current)
```
LLocr/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/         # OcrResult, ProviderConfig
│   ├── providers/    # ILlmProvider (OcrRequest), OpenAiProvider
│   ├── parsers/      # IOutputParser, RawParser, DetTokensParser, ParserFactory
│   └── app/          # AppController, DocumentModel, PageListModel, BoxListModel,
│                     #   OcrImageProvider, SettingsStore, UiController, Exporter
├── resources/
│   └── qml/          # Main.qml, SettingsDialog.qml, Theme.qml, WindowSettings.qml
├── rag-service/      # Python service (later stage) — empty for now
├── tests/            # test_det_parser (built); test_exporter (pending)
├── docs/
└── AGENTS.md
```

> **Note:** `OcrRequest` lives in `providers/ILlmProvider.h`. QML lives under
> `resources/qml/`.
