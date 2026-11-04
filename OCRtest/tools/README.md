# OCRtest/tools — eval scripts for candidate models

Reusable scripts moved here (Oct 2026) from the out-of-repo
`Documents/fix-eval-models` workdir. They evaluate candidate OCR models
against a block corpus: each candidate gets its native prompt, a crop per
block, and the answer is scored per block against ground truth.

| Script | What it does |
|---|---|
| `ocr_eval.py` | Block-level OCR eval: launch `llama-server` per candidate model, send crops, store answers as app-format `project.json`, score with `ocr_models/materials/compare.py` (`--model X` / `--all` / `--score`) |
| `page_eval.py` | Page-level probes (pageOcr / layout / rawPageOcr style) + llama.cpp log capture for memory profiles |
| `verify_all.py` | Verify downloaded GGUF files against the HF tree (sha256) |
| `gguf_info.py` | GGUF header reader (architecture, projector, context params) |

## Corpus layout expected by `ocr_eval.py`

The original OCR_TEST corpus data (crops, ground truth, baseline boxes) was
removed from the repo in the Oct-2026 cleanup. A corpus directory
(`OCRtest/ocr_models/materials/` by default, override in the script) needs:

- `box_crops/` — per-block crops `pXXbYY_<group>.png`;
- `gt.json` — ground-truth text per block (scoring);
- `results/baseline_boxes.json` — baseline model boxes (crop coordinates);
- `projects/OCR_TEST/project.json` — the app-format source of block rects;
- `compare.py` — scoring helpers (norm/cer/table-cell matching).

The scripts also import the chat client from `OCRtest/fix_models/client.py`.
Future large-corpus runs: build the corpus with the same layout and point the
constants at its directory.

The model-weight directories these scripts were written against stay outside
the repository (`Documents/fix-eval-models`, ~17 GB).
