- # 02. Technology Stack

  ## Main application
  | Area          | Choice                          | Status | Rationale                                            |
  | ------------- | ------------------------------- | ------ | ---------------------------------------------------- |
  | Language      | C++                             | ✅ used | Developer's core skill, performance                  |
  | GUI           | Qt6 + QML                       | ✅ used | Cross-platform, convenient declarative UI for panels |
  | Build         | CMake                           | ✅ used | Standard for cross-platform C++                      |
  | C++ packages  | vcpkg                           | ✅ used | Simple dependency management                         |
  | HTTP          | QNetworkAccessManager           | ✅ used | Requests to the LLM API (async via QFuture/QPromise) |
  | Images        | QImage                          | ✅ used | Loading/displaying                                   |
  | PDF           | **Qt PDF (`QPdfDocument`)**     | ✅ used | Ships with Qt6; no extra native dep (see ADR #8)     |
  | Box rendering | QML `Repeater` over a list model| ✅ used | Overlay bboxes on the preview (normalized rects)     |
  | Export        | **Direct writer (TXT/MD/HTML)** | 🟡 partial | Pandoc pipeline for DOCX/PDF still to be added   |
  | Tests         | Qt Test / Catch2                | ⬜ todo | Unit tests                                           |

  > **Change vs. original plan:** PDF rendering now uses the **Qt PDF module**
  > (`QPdfDocument`) instead of MuPDF/Poppler. It removes an external native
  > dependency and is sufficient at ~150 DPI page rendering. MuPDF remains a
  > fallback option if higher-fidelity or faster rendering is later required.

  > **Change vs. original plan:** export is currently a **direct per-page
  > writer** (TXT / Markdown / HTML) rather than the "internal Markdown →
  > Pandoc" pipeline. The Markdown-as-source-of-truth + Pandoc approach is
  > still the target for DOCX/PDF (see roadmap Stage 3).

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
