# 05. Development Plan

Status legend: ✅ done · 🟡 partial · ⬜ todo

## Stage 0 — Preparation
- [x] Set up the project: CMake, Git repository, folder structure. ✅
- [ ] Build on all 3 OSes locally. 🟡
- [x] Code style (clang-format), basic CI. ✅

## Stage 1 — MVP: basic OCR  — ✅ COMPLETE
- [x] UI: image loading, Start/Stop buttons, text output.
- [x] `OpenAiProvider` (works with Ollama / LM Studio / llama.cpp server).
- [x] Flow: image → API → text → display.
- [x] Export to Markdown / TXT.

## Stage 2 — Model extensibility — ✅ COMPLETE (revised)
- [x] `ILlmProvider` abstraction.
- [x] Output parsers (`raw` / one parser per model, bbox) + `ParserFactory`.
- [x] Box rendering on the preview (`BoxListModel` + QML `Repeater`).
- [x] **Configuration moved to Settings** (URL, key, timeout, **model name,
      temperature, max tokens, DRY sampling params, parser**) + persistence.
      The recognition **prompt** is owned by the selected **OCR model
      adapter** (ADR 58).
- [x] ~~JSON model profiles~~ **removed** (ADR #15); replaced by Settings.

## Stage 3 — Formats and documents — ✅ COMPLETE
- [x] PDF input (via **Qt PDF**).
- [x] Batch processing ("Recognize all") + per-page navigation, thumbnail strip,
      cancellable Stop.
- [x] Export to HTML (in addition to TXT/MD).
- [x] Export to DOCX / PDF (Pandoc, with a built-in PDF fallback).
- [x] Markdown adopted as the single internal export source.
- [x] Editable recognized-text pane (per-page edits, exported, marked, revertable).
- [x] Selective export: **All / Current / page range** (only recognized pages in
      the selection are exported).
- [x] Page **deletion** and **drag-reorder** (thumbnail strip).
- [x] **Image/chart block editing** (move / resize / delete on the preview).
- [x] **Markdown preview** (Qt WebEngine + marked + KaTeX) toggle in the text pane.
- [x] **i18n** (System / English / Русский via Qt Linguist) and DRY sampling
      params in Settings.

## Stage 4 — RAG (when ready) — ⬜ DEFERRED (ADR 6), NOT STARTED
- [ ] Backend **interface stub** for RAG.
- [ ] Python indexing service + search (FastAPI + Chroma/Qdrant).
- [ ] LLocr ↔ RAG integration over HTTP.

## Local-runtime plan — stages A–H ✅ COMPLETE
The former plan document was removed after
completion; the decisions are recorded as ADRs in `docs/07-glossary.md`, the
runtime/model components in `docs/04-components.md` (§4.16, §4.17).
| Stage | Content | Status |
| ----- | ------- | ------ |
| A | skeleton, `ConnectionMode`, resolver, settings groups, `SingleInstanceGuard` | ✅ done |
| B | binary + process lifecycle, capabilities, no-orphan (`ProcessGuard`) | ✅ done |
| C | `DownloadTask` resume + `DownloadManager` | ✅ done |
| D | llama.cpp install (`ReleaseCatalog`, backend, `ArchiveExtractor`, `InstallTransaction`) | ✅ done |
| E | models from Hugging Face (`ModelCatalog`, presets, `ModelRegistry`) | ✅ done |
| G-core | `ensureConnectionReady()` for Managed (start→health→alias, dedup, self-test) | ✅ done |
| F | first-run wizard (`SetupWizard` + 5 steps) | ✅ done |
| G-UI | footer indicator, restart banner, loading progress, error surfacing | ✅ done |
| H | polish + documentation: H.1 UI polish ✅, H.2 memory estimate ✅, H.3 update-check opt-in ✅, H.6 separate locks ✅, H.7 process/perf ✅, H.8 docs ✅; **H.4 closed** without a watchdog (ADR 47), **H.5 done** (the secrets live in the OS secret store, ADR 227) | ✅ done |

## Stage 5 — Polish and distribution — 🟡 PARTIAL
- [x] Unit tests (Qt Test): base suite (`test_det_parser`, `test_pagemodel`,
      `test_settings_store`, `test_exporter`) + local-runtime suite
      (`test_launch_config`, `test_runtime_lifetime`, `test_runtime_locator`,
      `test_capabilities`, `test_server_process`, `test_ensure_connection`,
      `test_download_manager`, `test_release_catalog`, `test_archive_extractor`,
      `test_install_transaction`, `test_model_catalog`, `test_model_registry`,
      `test_model_memory_estimator`, `test_install_lock`) — all wired into the
      build and run via ctest; helper `mock_llama_server` (no real network in
      any test).
- [ ] Unit tests for provider (network) and remaining export paths.
- [ ] Installers: Windows, macOS (.dmg + signing), Linux (AppImage/Flatpak).
- [ ] CI/CD (GitHub Actions) — 🟡 exists as two tiers (clang-format gate on
      push, full matrix on `workflow_dispatch`, ADR 121); the full matrix is
      pending the Qt 6.10.3 provisioning step.

## Open items (live backlog)
1. **Setup wizard can stall on the final step** (download + self-test): an
   error appears first, then nothing happens — investigate.
2. **Common recognition settings**: do not enable service blocks by choice.

Beyond the backlog: the local-runtime plan (stages A–H) is complete, the
working state persists with the document (`.llocr`, ADR 131), and RAG stays
deferred (ADR 6). Unit tests for the provider/network path and the installers
remain under Stage 5.
