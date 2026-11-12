# 19. Testing: the unit suite and the evaluation stands

> Russian mirror: `docs/19-testing.ru.md`. The stands live in `OCRtest/`
> (see also `OCRtest/README.md`).

## 1. The unit suite

All tests are Qt Test targets in `tests/`, wired in `tests/CMakeLists.txt`
and run through `ctest`. Every target gets a directory-wide `TIMEOUT 300`
and `QT_QPA_PLATFORM=offscreen` (ADR 99); per-test overrides win. Two
helper binaries are built but not registered: `mock_llama_server` (a local
llama-server stand-in) and `candidate_render_dump` (renders visual-diff
candidates for the decision stand).

One command runs everything — configure-if-needed → build → ctest →
clang-format → qmllint (ADR 100):

```sh
scripts/check.sh                 # reuse the configured build/ tree
scripts/check.sh --configure     # re-configure first
```

Windows: the `dev-win` preset tree (`build/win-msvc2022`, Ninja), built
inside an MSVC x64 prompt; Qt DLLs must be on `PATH` before `ctest`.
`test_export_renderer` runs headless Chromium with
`--disable-gpu --no-sandbox --single-process`; on some machines its GPU
initialization aborts (environmental, not a regression).

### The 74 targets, by domain

| Domain | Targets |
| --- | --- |
| Parsers | `test_det_parser` |
| Config layer | `test_profile_storage`, `test_secret_store` |
| App layer (pipeline, queue, availability) | `test_runtime_tab_external_qml`, `test_verification_prompts`, `test_auto_pipeline`, `test_verification_queue`, `test_availability_presenter`, `test_block_box_map`, `test_editor_bridge` |
| Models (adapters, decision, fix/read) | `test_ocr_models`, `test_crop_scaling`, `test_pagemodel`, `test_page_edit_store`, `test_box_model`, `test_decision_model`, `test_fix_protocol`, `test_fix_diff_gate`, `test_external_runtime` |
| Document, pages, layout, orientation | `test_page_status`, `test_layout`, `test_project_store`, `test_document_model`, `test_page_document`, `test_page_rotation`, `test_page_orientation`, `test_djvu_document`* , `test_app_import` |
| Editor (the WYSIWYG page, the pull, the wrapper) | `test_editor_page`, `test_editor_pull`, `test_editor_qml` |
| Import, settings, profiles | `test_settings_store`, `test_request_profile`, `test_request_budget`, `test_launch_profile`, `test_launch_config` |
| QML (launch tab, task table, wizard, switch) | `test_launch_tab_qml`, `test_task_models_table_qml`, `test_setup_wizard_qml`, `test_segmented_control_qml` |
| QML (the settings window) | `test_settings_nav_qml`, `test_settings_window_qml` |
| QML (the three panel menus) | `test_panel_menus_qml` |
| QML (the footer's diagnostic banner) | `test_footer_banner_qml` |
| QML (the block panel's header) | `test_block_panel_qml` |
| UI (the icon set) | `test_ui_icons` |
| UI (the icon-and-caption button) | `test_icon_tool_button_qml` |
| Runtime installer, lifetime | `test_runtime_installer`, `test_runtime_lifetime` |
| Export and rendering | `test_exporter`, `test_export_renderer`, `test_candidate_renderer` |
| Transport and managed server | `test_http_client`, `test_capabilities`, `test_runtime_locator`, `test_server_process`, `test_ensure_connection` |
| Downloads, catalogs, installs, locks | `test_model_memory_estimator`, `test_download_manager`, `test_release_catalog`, `test_archive_extractor`, `test_staged_install`, `test_install_transaction`, `test_model_catalog`, `test_model_registry`, `test_hardware_info`, `test_model_installer`, `test_model_install_transaction`, `test_model_preset_catalog`, `test_install_lock` |
| Core (client, transitions, diff, latex) | `test_llama_client`, `test_box_transitions`, `test_text_diff`, `test_latex_text`, `test_log_document_sync` |

\* `test_djvu_document` (and the DjVu part of `test_app_import`) is built
only when DjVuLibre was found at configure time.

Committed fixtures: `tests/fixtures/djvu/` (synthetic DjVu documents, with
the generator), `tests/fixtures/exporter-preview/` (fake KaTeX bundle),
`tests/fixtures/orientation-corpus/` (the OCR_TEST page renders consumed by
`test_page_orientation` via `LLOCR_CORPUS_RENDERS`; the test skips when
absent). A test class that constructs `SettingsStore` must declare
`TestSettingsIsolation` (see `AGENTS.md`) — the guard redirects QSettings
to a temp profile.

## 2. The evaluation stands (`OCRtest/`)

The stands measure what unit tests cannot: model quality. They are plain
Python + configs, run against a real llama-server; none of it is part of
the build. The bulk data (crops, page images, ground-truth dumps, verdict
projects) was removed in the Oct-2026 cleanup — what remains is scripts,
configs and the summary documents. The shared corpus was **OCR_TEST** (a
10-page DjVu scan of technical literature, ~195 blocks); future runs will
use larger corpora in the same layout (see `OCRtest/tools/README.md`).

| Stand | Measures | Summary document |
| --- | --- | --- |
| `ocr_models/` | Block-OCR quality of candidate models (CER, table cells, equations) vs baselines | `ocr_models/REPORT.md` |
| `decision_models/` | P(match) of the visual-diff decision model; thresholds per block type | `decision_models/test 2/iteration8/README.md` (ADR 258) |
| `fix_models/` | The fix protocol: OK/FIX/REVIEW behaviour, candidate/no-candidate experiments, order axis, thinking | `fix_models/README.md`, `FIX_REQUEST_EXPERIMENT.md`, `ORDER_AXIS.md`, `PROBE_SUMMARY.md`, `READ_PROTOCOL.md` |
| `orientation_eval/` | The two-stage orientation detector and the layout-model rotation tokens | `docs/12-orientation-pipeline.md` §7 |
| `tools/` | Reusable eval drivers moved from the out-of-repo model-eval workdir | `tools/README.md` |

### What the stands established (the numbers behind the ADRs)

- **Block OCR** (`ocr_models/REPORT.md`): the shipped default (MinerU2.5-Pro)
  won the comparison on the OCR_TEST corpus; per-block crops of one corpus
  give every model identical input, scored against the same ground truth.
- **Decision thresholds** (ADR 258): text 0.92, equation 0.87, table 0.4,
  base 0.5 — FN 10 (13.7 %) / FP 41 (39.4 %) vs FN 29 / FP 15 at 0.5. The
  model rejects tables wholesale (all p < 0.02 — the markdown-grid render is
  out of distribution) and typed scans carry a threshold-proof FP tail; the
  residual FN are print homoglyphs and integrals.
- **The read protocol** (`fix_models/READ_PROTOCOL.md`, ADR 259/266):
  reading without the recognized text as a sample measures CER 0.015 with
  index hit 1.00 (vs 0.040/0.84 with the sample) — the request carries only
  image + prompts.
- **Candidate framing** (`fix_models/FIX_REQUEST_EXPERIMENT.md`): the
  candidate in the fix request is the culprit of mismatches for some models —
  per-model no-candidate protocols remain an open direction; qwen3.5-9b must
  keep the candidate.
- **Orientation** (ADR 243–246): the axis is read correctly at the measured
  thresholds, 90°/270° direction from layout tokens is right in 14/20 cases
  (so auto-turn is off by default), and 180° stays unread — projections are
  half-turn invariant.
- **Memory calibration** (`docs/18-memory-estimate.md`): the estimator's
  formula was calibrated against measured servers for all nine shipped
  models (`scripts/measure-memory.sh`, `scripts/memory-from-log.py`).

### Running a stand

Each stand is a self-contained directory of scripts; see its README. The
model weights the tools were written against stay outside the repository
(`Documents/fix-eval-models`). To test on a new corpus, build the corpus
layout expected by `OCRtest/tools/ocr_eval.py` (crops + ground truth +
baseline boxes) and point the script constants at it.

## 3. Related documents

- `docs/09-tasks-and-verification.md` — what the pipeline under test does.
- `docs/07-glossary.md` — ADR 99 (test defaults), ADR 100/121 (check script, CI tiers).
- `docs/06-dev-setup.md` — build and developer helper scripts.
