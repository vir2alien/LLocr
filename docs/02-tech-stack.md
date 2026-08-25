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
  | PDF           | **Qt PDF (`QPdfDocument`)**     | ✅ used | Ships with Qt6; no extra native dep (see ADR #7)     |
  | Box rendering | QML `Repeater` over a list model| ✅ used | Overlay bboxes on the preview (normalized rects); image blocks are movable / resizable / deletable |
  | Export        | **Direct writer (TXT/MD/HTML)** + Pandoc for DOCX/PDF, PDF fallback | ✅ done | Pandoc discovered via `QStandardPaths`; built-in `QPdfWriter` fallback |
  | i18n          | Qt Linguist (`qsTr`/`tr` + `.ts`) | ✅ done | Runtime retranslate; language persisted in `SettingsStore` (`ui/language`) |
  | Markdown preview | Qt WebEngine + marked + KaTeX | ✅ done | Toggle in the right text pane; image refs resolved via data: URIs |
  | Tests         | Qt Test (unit tests in `tests/`) | ✅ done | 4 base + 13 runtime targets (see 4.12); helper `mock_llama_server` |
  | Local runtime (managed llama.cpp) | `RuntimeController` + `LlamaServerProcess` + `ReleaseCatalog` | ✅ done | Installs & launches a local `llama-server` (see Architecture §runtime layer); the *HTTP client* remains `QNetworkAccessManager` |
  | Runtime install | `DownloadTask`/`DownloadManager` + `ArchiveExtractor` (miniz) + `InstallTransaction` | ✅ done | Resumable downloads, ZIP-only extract, transactional install (staging → verify → rename) |
  | Models (Hugging Face) | `ModelCatalog` + `ModelRegistry` + `ModelInstaller` | ✅ done | GGUF install from HF with commit-SHA pinning + `sha256` (`lfs.oid`) |
  | Model presets | `default-presets.json` + user `catalog.json` | ✅ done | Pre-verified `model+mmproj+parser+prompt+ctx` pairs; merge-by-id |
  | No-orphan processes | `ProcessGuard` (Job Object / `PDEATHSIG` / best-effort macOS) | ✅ done | Strong on Win/Linux, best-effort on macOS (owner.json + next-start detection) |

  > **Note:** export is done via a **direct per-page writer** (TXT / Markdown /
  > HTML) plus **Pandoc for DOCX/PDF** (with a built-in `QPdfWriter` fallback
  > for PDF). Markdown remains the single internal source of truth.
  > PDF rendering uses the **Qt PDF module**
  > (`QPdfDocument`). MuPDF remains a fallback option if higher-fidelity or faster rendering is later required.
  >
  > The model prompt is **not** free-typed in `SettingsStore` — it is supplied
  > by the chosen **model preset** (see `04.16`/`04.17`), with a built-in default
  > (`AppController::m_prompt`, "document parsing.") when no preset is in use.
  > The bbox coordinate range is hardcoded (`DetTokensParser`, 1000).

  ## Managed local runtime (llama.cpp) — stages A–G ✅
  Starting with a llama.cpp binary or downloading it from GitHub Releases
  (`ggml-org/llama.cpp`), the app can run a **local `llama-server`** the user
  never has to manage:

  | Concern               | Choice                                                                 |
  | --------------------- | ---------------------------------------------------------------------- |
  | Managed server        | **llama.cpp `llama-server`** (min build `b4000`), launched on loopback  |
  | Runtime install       | GitHub Releases (asset `sha256` from the release body), ZIP via `miniz`|
  | Models                | **Hugging Face** GGUF, commit-`sha` pinned, `sha256` from `lfs.oid`     |
  | Health               | GET `/health` (fallback `/v1/models`) during startup                    |
  | Downloads            | `DownloadTask` (resume: `.part`+`.part.meta`, `Range`/`If-Range`, streaming sha256) in a ≤2-queue `DownloadManager` |
  | Process safety        | `QProcess` argv-only, `ProcessGuard` (Job Object / `PDEATHSIG`/best-effort) |
  | Reproducibility      | pinned build tags + pinned commit SHAs, `sha256` verified before use    |

  > Local runtime and models require **no external Python** and no extra native
  > tooling — everything is embedded (network via Qt, ZIP via vendored `miniz`).
  
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
  - For the **local runtime**, self-managed llama.cpp (instead of Ollama/LM Studio)
    keeps one predictable CLI and full control over launch args — and no external
    dependency to install (ADR 27).
