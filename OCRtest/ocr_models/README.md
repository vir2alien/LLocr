# ocr_models — block-OCR comparison stand

Compared candidate block-OCR models against baselines on the OCR_TEST
corpus: each model received its native prompt and the same per-block crops
the baselines saw; answers were stored as app-format `project.json` files
and scored per block against ground truth (CER, table-cell hit, equation
normalization). Full methodology and results: **`REPORT.md`** (17.10.2026).

State after the Oct-2026 cleanup:

- `REPORT.md` — the measurement summary (kept);
- `materials/compare.py` — the scoring tool (kept; it reads `gt.json` and
  per-model `projects/` from a corpus directory — supply a corpus to rerun);
- the corpus data (crops, ground truth `gt.py`/`gt.json`, page renders,
  per-model verdict projects, the seven `OCR_TEST_*.llocr`) was removed.

The page renders the unit test `test_page_orientation` consumes live in
`tests/fixtures/orientation-corpus/`. The eval driver lives in
`../tools/ocr_eval.py`.
