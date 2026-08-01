# 06. Development Environment and Distribution

## Tooling
| Area             | Tool                                        | Status |
| ---------------- | ------------------------------------------- | ------ |
| Build system     | CMake                                       | ✅ used |
| Version control  | Git + GitHub/GitLab                         | ✅ used |
| CI/CD            | GitHub Actions (builds for Win/macOS/Linux) | ⬜ todo |
| C++ dependencies | vcpkg (or Conan)                            | ✅ used |
| Formatting       | clang-format                                | ⬜ todo |
| Tests            | Qt Test / Catch2                            | ⬜ todo |

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
│   ├── core/         # OcrResult, OcrRequest, ProviderConfig
│   ├── providers/    # ILlmProvider, OpenAiProvider
│   ├── parsers/      # IOutputParser, RawParser, DetTokensParser, ParserFactory
│   └── app/          # AppController, DocumentModel, PageListModel, BoxListModel,
│                     #   OcrImageProvider, SettingsStore, Exporter
├── resources/
│   └── qml/          # Main.qml, SettingsDialog.qml
├── rag-service/      # Python service (later stage) — not created yet
├── tests/            # to be added (Stage 5)
├── docs/
└── AGENTS.md
```

> **Concept change:** `core/` no longer contains `ModelProfile` or
> `ProfileRepository`, and there is **no `resources/profiles/` directory**.
> Model/connection/parser settings are held in `ProviderConfig` and stored via
> `QSettings` (see `SettingsStore`). QML lives under `resources/qml/`.
