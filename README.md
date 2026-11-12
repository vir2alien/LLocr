# LLocr

![Status](https://img.shields.io/badge/status-MVP%20in%20development-orange)
![License](https://img.shields.io/badge/license-GPLv3-blue)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

**A cross-platform application for OCR based on local LLMs.**

LLocr runs a local llama.cpp server (or connects to any OpenAI-compatible
endpoint), recognizes text from images, PDFs and DjVu files, verifies the
result with a second model, fixes it with a third — and exports to Markdown,
TXT, HTML, DOCX and PDF. Everything runs locally; your data never leaves the
machine.

> Русская версия этого файла: [README.ru.md](README.ru.md). Вся документация
> двуязычна: у каждого документа есть зеркало `*.ru.md`.

---

## Features

- **Four model tasks** — page markup (`layout`), block OCR
  (`blockRecognition`), visual-diff verification (`decision`) and AI
  correction (`fix`), each with its own model, quantization and request
  profile
- **Managed runtime** — the app downloads, installs and starts llama.cpp
  itself (GitHub releases, sha256-verified, transactional install); models
  are downloaded from Hugging Face with pinned revisions
- **Verification pipeline** — every block is judged by a visual-diff model
  (crop vs rendered candidate), mismatches go to the fix model (OK / FIX /
  REVIEW), and a read task can re-transcribe a block from the image alone
- **Block inspector** — two text layers per block (OCR result and AI
  correction) as clickable version cards with in-place word-level diff
  tinting, approval and hand editing
- **Manual markup** — draw blocks by hand or run the layout model; a page
  can be recognized block by block
- **Orientation pipeline** — two-stage pixel check (deskew + axis), page
  auto-straightening and model-driven turn suggestions
- **Auto pipeline** — one button runs markup → recognition → verification →
  fix → verification of the corrected over a page range
- **PDF, DjVu and image input**; multi-page documents with reordering,
  deletion and a live thumbnail strip
- **Editable image/chart blocks** — move, resize and delete detected regions
- **Live Markdown preview** with rendered LaTeX formulas and tables
  (Qt WebEngine + marked + KaTeX)
- **Export** to Markdown / TXT / HTML / DOCX (Pandoc) / PDF (Pandoc or the
  built-in writer) with All / Current / page-range scope
- **Self-contained projects** — the whole working state (sources, blocks,
  verdicts, manual edits) saves to one `.llocr` file and opens again
- **Secrets in the OS store** — API keys live in the macOS Keychain, Windows
  Credential Manager or Linux Secret Service, never in the settings file
- Localizable UI (System / English / Русский) and theming (System / Light /
  Dark)

## Tech stack

| Area | Technologies |
| --- | --- |
| Core / UI | Qt6 · C++ · QML |
| Markdown preview / HTML+PDF export | Qt WebEngine · marked · KaTeX · DOMPurify |
| PDF input | Qt PDF (`QPdfDocument`) |
| DjVu input (optional) | DjVuLibre C API |
| OCR runtime | llama.cpp (`llama-server`) |
| Export | Pandoc (DOCX/PDF), built-in HTML writer |
| Build system | CMake (vcpkg participates on Windows only, for ZLIB) |

## Getting started

### Prerequisites

- Qt 6.10 with **Qt PDF**, **Qt WebEngine** (pulls in Qt WebChannel and Qt
  Positioning) and **Qt LinguistTools**
- CMake ≥ 3.21 and a C++20 compiler — **MSVC 2022 64-bit on Windows**
  (the MinGW Qt build has no Qt WebEngine)
- vcpkg (Windows only: ZLIB) · Pandoc (optional: DOCX/PDF export)
- DjVuLibre (optional: `.djvu` input)
- A llama.cpp release is downloaded by the app itself on first run

Concrete setup steps for all three platforms: [docs/06-dev-setup.md](docs/06-dev-setup.md).

### Build

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/<platform>
cmake --build build -j
ctest --test-dir build
```

> **Windows:** use the `msvc2022_64` Qt package and the MSVC 2022 64-bit
> compiler; the ready-made preset tree is `build/win-msvc2022`
> (`cmake --preset dev-win`). Put the Qt DLLs on `PATH` before running
> tests. See `docs/06-dev-setup.md`.

## Documentation

Everything is documented twice — English primary, `*.ru.md` mirror.

### The app

| # | Document | EN | RU |
| --- | --- | --- | --- |
| 01 | Idea, goals, requirements | [01-overview.md](docs/01-overview.md) | [ru](docs/01-overview.ru.md) |
| 02 | Technologies and rationale | [02-tech-stack.md](docs/02-tech-stack.md) | [ru](docs/02-tech-stack.ru.md) |
| 03 | Architecture, layers, key abstractions | [03-architecture.md](docs/03-architecture.md) | [ru](docs/03-architecture.ru.md) |
| 04 | Component catalog | [04-components.md](docs/04-components.md) | [ru](docs/04-components.ru.md) |
| 05 | Roadmap and status | [05-roadmap.md](docs/05-roadmap.md) | [ru](docs/05-roadmap.ru.md) |
| 06 | Tooling, build, CI/CD, distribution | [06-dev-setup.md](docs/06-dev-setup.md) | [ru](docs/06-dev-setup.ru.md) |
| 07 | Glossary + ADR log (all architectural decisions) | [07-glossary.md](docs/07-glossary.md) | [ru](docs/07-glossary.ru.md) |
| 08 | Cross-platform application icon | [08-app-icon.md](docs/08-app-icon.md) | [ru](docs/08-app-icon.ru.md) |
| 09 | The four tasks and the verification/fix pipeline | [09-tasks-and-verification.md](docs/09-tasks-and-verification.md) | [ru](docs/09-tasks-and-verification.ru.md) |
| 10 | External runtime: per-task endpoints and secrets | [10-external-runtime.md](docs/10-external-runtime.md) | [ru](docs/10-external-runtime.ru.md) |
| 11 | Auto pipeline (the five stages) | [11-auto-pipeline.md](docs/11-auto-pipeline.md) | [ru](docs/11-auto-pipeline.ru.md) |
| 12 | Page-orientation correction pipeline | [12-orientation-pipeline.md](docs/12-orientation-pipeline.md) | [ru](docs/12-orientation-pipeline.ru.md) |
| 19 | Test suite and evaluation stands | [19-testing.md](docs/19-testing.md) | [ru](docs/19-testing.ru.md) |

### Model manuals (launch command + request cookbook)

| # | Model | Task(s) | EN | RU |
| --- | --- | --- | --- | --- |
| 13 | MinerU2.5-Pro-2605-1.2B | layout, blockRecognition | [13-model-mineru.md](docs/13-model-mineru.md) | [ru](docs/13-model-mineru.ru.md) |
| 14 | GLM-OCR | blockRecognition | [14-model-glm-ocr.md](docs/14-model-glm-ocr.md) | [ru](docs/14-model-glm-ocr.ru.md) |
| 15 | Gemma 4 E4B / 12B / 26B-A4B QAT | fix | [15-model-gemma-4.md](docs/15-model-gemma-4.md) | [ru](docs/15-model-gemma-4.ru.md) |
| 16 | Qwen3.5-4B / 9B (the fix default) | fix | [16-model-qwen3.5.md](docs/16-model-qwen3.5.md) | [ru](docs/16-model-qwen3.5.ru.md) |
| 17 | MiniCPM-OCR Jev (visual diff) | decision | [17-model-minicpm-jev.md](docs/17-model-minicpm-jev.md) | [ru](docs/17-model-minicpm-jev.ru.md) |
| 18 | VRAM estimate: formula and calibration of all shipped models | — | [18-memory-estimate.md](docs/18-memory-estimate.md) | [ru](docs/18-memory-estimate.ru.md) |

### Archive

Models, parsers and manuals cut from the release live in
[`archive/`](archive/README.md) with a manifest and restore recipe.

## License

LLocr is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE). Third-party components are collected in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [`licenses/`](licenses/)
directory; Qt libraries are linked **dynamically**, as required by the LGPL v3.
