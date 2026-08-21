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
- [x] Output parsers (`raw` / `det_tokens` bbox) + `ParserFactory`.
- [x] Box rendering on the preview (`BoxListModel` + QML `Repeater`).
- [x] **Configuration moved to Settings** (URL, key, timeout, **model name,
      temperature, max tokens, DRY sampling params, parser**) + persistence. The
      **prompt** remains hardcoded in `AppController` (not in Settings yet).
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

## Stage 4 — RAG (when ready) — ⬜ NOT STARTED
- [ ] Backend **interface stub** for RAG.
- [ ] Python indexing service + search (FastAPI + Chroma/Qdrant).
- [ ] LLocr ↔ RAG integration over HTTP.

## Stage 5 — Polish and distribution — 🟡 PARTIAL
- [x] Unit tests (Qt Test): `test_det_parser`, `test_pagemodel`,
      `test_settings_store`, `test_exporter` — all wired into the build.
- [ ] Unit tests for provider (network) and remaining export paths.
- [ ] Installers: Windows, macOS (.dmg + signing), Linux (AppImage/Flatpak).
- [ ] CI/CD (GitHub Actions).

## Immediate next steps (priority order)
1. **Persist edits with the document** across sessions (save / restore).
2. **Lay the RAG interface stub** in the backend (no Python yet).
3. Unit tests for the provider / network path.
4. Bug fixes.
5. Start **Stage 4 (RAG)**.
