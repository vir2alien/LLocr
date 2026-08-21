- # LLocr

![Status](https://img.shields.io/badge/status-MVP%20in%20development-orange)
![License](https://img.shields.io/badge/license-GPLv3-blue)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

**A cross-platform application for OCR based on local LLMs.**

LLocr connects to a local LLM provider (llama.cpp / OpenAI-compatible API),
recognizes text from images and PDFs, and exports the result to Markdown, DOCX, and more —
all running locally, without sending your data to the cloud.

---

## Features

> ⚠️ The project is under active development. Items marked *(WIP)* are not yet complete.

- **Cross-platform**: Windows, macOS, Linux
- Support for different OCR models with their own input/output formats
- **Live Markdown preview** with rendered LaTeX formulas, tables and images
- Side-by-side source view and editable recognition results
- Export to **Markdown, TXT, DOCX, PDF, HTML**
- Multi-page documents: page reordering/deletion, per-page recognition, batch processing
- **Editable image/chart blocks** — move, resize, and delete detected regions
- Per-model generation settings incl. **DRY sampling parameters** (llama.cpp)
- Localizable UI (System / English / Русский) and theming (System / Light / Dark)
- Fully local processing — your data never leaves your machine
- **RAG integration** to grow a knowledge base from scans *(WIP)*

## Tech Stack

| Area             | Technologies                  |
| ---------------- | ----------------------------- |
| Core / UI        | Qt6 · C++ · QML               |
| Markdown preview | Qt WebEngine · marked · KaTeX |
| Build system     | CMake · vcpkg                 |
| RAG service      | Python                        |

## Getting Started

### Prerequisites
- Qt 6.5+ with the following modules:
  - **Qt PDF** — PDF page rasterization
  - **Qt WebEngine** — Markdown/LaTeX preview
  - **Qt LinguistTools** — runtime translation of the UI
- CMake ≥ 3.21
- A C++20-compatible compiler
- vcpkg
- Python 3.x (for the RAG service)
- Pandoc (optional, for DOCX/PDF export)
- A running local LLM provider (e.g. [llama.cpp](https://github.com/ggml-org/llama.cpp) or any OpenAI-compatible endpoint)

> 💡 **Qt WebEngine** and **Qt PDF** are optional Qt components and are *not*
> installed by default. Add them via the Qt Maintenance Tool:
> *Qt → 6.x.x → Additional Libraries → Qt WebEngine / Qt PDF*.
> Qt WebEngine also pulls in Qt WebChannel and Qt Positioning.

### Build

```bash
git clone https://github.com/<user>/llocr.git
cd llocr
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<platform>
cmake --build build --config Release

```

## License

LLocr is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

### Third-party components

This project bundles and links against third-party software:

Full license texts and attribution notices are collected in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [`licenses/`](licenses/) directory.

Qt libraries are linked **dynamically**, as required by the LGPL v3.
