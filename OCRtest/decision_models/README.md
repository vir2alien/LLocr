# decision_models — decision-task calibration stand

Measures the decision task (the visual diff `systemone_visual_diff_v1`,
MiniCPM-OCR Jev): P(match) distributions per block type and the thresholds
derived from them.

- **`test 1/`** — the first protocol study (source PDFs removed; the report
  `work/results/report.md` and `README.md` remain).
- **`test 2/`** — the corpus measurement behind ADR 258:
  - `evaluate.py` sends the app's exact request over the corpus and writes
    verdicts into a `.llocr` project (reproduces the app's numbers bit for
    bit);
  - `jev_score.py` scores the verdicts; `calibrate_jev.py` (moved out of
    `jev-runs/`) sweeps candidate thresholds;
  - **`iteration8/README.md`** — the cited result: thresholds text 0.92 /
    equation 0.87 / table 0.4 (base 0.5), FN 10 (13.7 %) / FP 41 (39.4 %)
    vs FN 29 / FP 15 at 0.5; tables rejected wholesale (all p < 0.02 — the
    markdown-grid render is out of distribution); typed scans carry a
    threshold-proof FP tail.
- The per-iteration verdict `.llocr` projects and the rendered candidates
  (`jev-runs/renders/`) were removed in the Oct-2026 cleanup; renders are
  regenerable with the `candidate_render_dump` helper target
  (`tests/candidate_render_dump.cpp`).

Thresholds ship in `resources/profiles/models/minicpm-jev.json`
(`matchThresholdByBlock`); the method is `docs/09-tasks-and-verification.md` §6.
