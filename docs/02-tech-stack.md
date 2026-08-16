- # 02. Technology Stack

  ## Main application
  | Area          | Choice                          | Status | Rationale                                            |
  | ------------- | ------------------------------- | ------ | ---------------------------------------------------- |
  | Language      | C++                             | ✅ used | Developer's core skill, performance                  |
  | GUI           | Qt6 + QML                       | ✅ used | Cross-platform, convenient declarative UI for panels |
  | Build         | CMake                           | ✅ used | Standard for cross-platform C++                      |
  | C++ packages  | vcpkg (configured, not currently used) | 🟡 | Declared in the `dev` preset, but the active build/ does **not** use it (Qt is external; see `06-dev-setup.md`) |
  | HTTP          | QNetworkAccessManager           | ✅ used | Requests to the LLM API (async via QFuture/QPromise) |
  | Images        | QImage                          | ✅ used | Loading/displaying                                   |
  | PDF           | **Qt PDF (`QPdfDocument`)**     | ✅ used | Ships with Qt6; no extra native dep (see ADR #8)     |
  | Box rendering | QML `Repeater` over a list model| ✅ used | Overlay bboxes on the preview (normalized rects)     |
  | Export        | **Direct writer (TXT/MD/HTML)** + Pandoc for DOCX/PDF, PDF fallback | ✅ done | Pandoc discovered via `QStandardPaths`; built-in `QPdfWriter` fallback |
  | Tests         | Qt Test (unit tests in `tests/`)             | 🟡 partial | `test_det_parser` in build; `test_exporter` pending |

  > Note:** export is done via a **direct per-page writer** (TXT / Markdown /
  > HTML) plus **Pandoc for DOCX/PDF** (with a built-in `QPdfWriter` fallback
  > for PDF). Markdown remains the single internal source of truth.
  > PDF rendering uses the **Qt PDF module**
  > (`QPdfDocument`). MuPDF remains a fallback option if higher-fidelity or faster rendering is later required.
  
  ## RAG service (separate process) — not started
  | Area          | Choice           | Rationale                |
  | ------------- | ---------------- | ------------------------ |
  | Language      | Python           | Rich ML/RAG ecosystem    |
  | API           | FastAPI          | Lightweight HTTP service |
  | Vector DB     | Chroma / Qdrant  | Local embedding storage  |
  | Link to LLocr | HTTP (localhost) | Component decoupling     |
  
  > RAG alternative without Python: **sqlite-vec** (a vector extension for SQLite,
  > embeddable in Qt via QSqlDatabase). Consider it if you prefer not to pull in Python.

  ## Why this choice
  - Avoid re-learning for its own sake — Qt covers all GUI requirements.
  - Python is used only where its ecosystem is genuinely stronger (RAG).
  - Markdown as the internal format + Pandoc = cheap support for many export formats.
