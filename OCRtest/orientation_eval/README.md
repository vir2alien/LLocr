# orientation_eval — orientation detector calibration stand

Calibrated the two-stage pixel detector and the layout-model rotation
tokens (`docs/12-orientation-pipeline.md`, ADR 243–246):

- `prepare_pages.py` — regenerates the rotated corpus pages (orig / rot90cw /
  rot90ccw / rot180 × full + 1036 px squash) from the corpus renders, now at
  `tests/fixtures/orientation-corpus/`;
- `run_layout.py` / `run_blocks.py` — run the layout model over the pages,
  collect the `<|rotate_*>` token votes (the direction measure: correct in
  14 of 20 cases);
- `analyze.py` — sweeps the detector constants (axis threshold, alternation
  depth, deskew sweep) against the labeled pages;
- `research/` — the MinerU reply-tree research scripts.

The regenerated pages (`pages/`) and the stored raw replies (`replies/`)
were removed in the Oct-2026 cleanup — both regenerate from the renders via
`prepare_pages.py` + `run_layout.py`. The measured conclusions live in
`docs/12-orientation-pipeline.md` §7 and the ADRs.
