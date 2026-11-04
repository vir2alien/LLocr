# OCRtest — evaluation stands

Out-of-app evaluation stands for the OCR models, the verification pipeline
and the orientation pipeline. **Not part of the build** (no CMake target
reaches this directory); everything here is scripts, configs and measurement
summaries — the bulk data (crops, page images, ground-truth dumps, verdict
projects) was removed in the Oct-2026 cleanup, before the move to
large-corpus testing.

The shared corpus was **OCR_TEST** — a DjVu scan of technical literature
(10 pages, ~195 blocks). The unit-test fixture `test_page_orientation`
still reads its page renders from `tests/fixtures/orientation-corpus/`.

| Stand | Purpose | Key results |
|---|---|---|
| [`ocr_models/`](ocr_models/) | Block-OCR model comparison (17 candidates vs baselines) | see `ocr_models/REPORT.md` |
| [`decision_models/`](decision_models/) | Decision-model (visual diff) calibration | text 0.92 / equation 0.87 / table 0.4 thresholds — ADR 258, `decision_models/test 2/iteration8/README.md` |
| [`fix_models/`](fix_models/) | Fix-protocol measurements (OK/FIX/REVIEW, candidate experiments, read protocol) | `fix_models/README.md`, `READ_PROTOCOL.md` |
| [`orientation_eval/`](orientation_eval/) | Orientation detector calibration (axis, deskew, rotation tokens) | `docs/12-orientation-pipeline.md` §7 |
| [`tools/`](tools/) | Reusable eval scripts moved from the out-of-repo model-eval workdir | `tools/README.md` |

What was removed (Oct 2026): `ocr_models/materials/{gt_notes,box_crops,
overlays,projects,results}`, `gt.py`/`gt.json` (ground truth), the seven
`OCR_TEST_*.llocr` verdict projects, `decision_models` source PDFs and
iteration verdicts, `jev-runs/{renders,work-corpus,inspect}`,
`orientation_eval/{pages,replies}`, `fix_models/{results,corpus/crops}`.
Scripts, configs, prompt profiles and the summary documents were kept.
