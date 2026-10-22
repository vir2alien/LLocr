"""Pick the per-block thresholds of a decision model from one run.

The verdict is a pure threshold over the stored P(match) (ADR 173: Ok = p >= t),
so the operating point is read off the run's FN/FP(t) curve — no new run is
needed. Each block type has its own curve, and the types are independent, so
the policy is the sum of the per-type choices.

The threshold of a type is the knee of its curve: the last step that buys a
caught error for at most `--rate` false alarms. A pair that keeps FP at zero
takes the edge of the clean region, so the type stays checkable on documents
the corpus does not show; a pair that pays FP takes the cheapest catch. A type
the corpus barely covers (fewer than `--min-blocks` blocks) is not calibrated —
its profile value stays.

Usage:
  python3 scripts/decision-eval/pick-thresholds.py <run.llocr> [--model clef-flash]
  python3 scripts/decision-eval/pick-thresholds.py <run.llocr> --rate 1.5
"""

import argparse
import importlib.util
import json
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
TEST2 = REPO_ROOT / "OCRtest" / "decision_models" / "test 2"
PROFILES = REPO_ROOT / "resources" / "profiles" / "models"

spec = importlib.util.spec_from_file_location("cal", REPO_ROOT / "scripts" / "decision-calibrate.py")
cal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cal)

ALIASES = cal.ALIASES
THRESHOLDS = cal.THRESHOLDS


def label_of(row):
    return ALIASES.get(row["label"], row["label"])


def curve_of(rows, label):
    """The (threshold, FN, FP) curve of one block type."""
    sub = [r for r in rows if label_of(r) == label]
    points = []
    for t in THRESHOLDS:
        fn = sum(1 for r in sub if r["truth"] == "err" and r["conf"] >= t)
        fp = sum(1 for r in sub if r["truth"] == "clean" and r["conf"] < t)
        points.append((t, fn, fp))
    return points


def knee(points, rate):
    """The knee of one curve: the last step that buys an error at the given rate.

    The rate is the one of the step itself — the thresholds are a grid, and a
    step that buys nothing is skipped, not a stop: the curve can rise for a
    while before the errors start coming.
    """
    t, fn, fp = points[0]
    prev_fn, prev_fp = fn, fp
    for nt, nfn, nfp in points[1:]:
        bought = prev_fn - nfn
        paid = nfp - prev_fp
        prev_fn, prev_fp = nfn, nfp
        if bought <= 0:
            continue
        if paid / bought > rate:
            break
        t, fn, fp = nt, nfn, nfp
    return t, fn, fp


def threshold_for(points, knee_pair, has_errors):
    """The threshold of a type for the pair the knee found.

    A pair that keeps FP at zero takes the edge of the clean region — the
    highest threshold that still does not flag the corpus's clean blocks — so
    the type stays checkable on unseen documents. A pair that pays FP takes the
    cheapest catch: the lowest threshold with that pair, so the type is flagged
    as eagerly as the measurement allows. A type with no errors to catch has
    nothing to buy, so it also takes the edge of its clean region.
    """
    if not has_errors:
        best = min(fp for _, _, fp in points)
        same = [t for t, _, fp in points if fp == best]
        return max(same), 0, best
    if knee_pair[1] == 0:
        same = [t for t, fn, fp in points if (fn, fp) == knee_pair]
        return max(same), knee_pair[0], knee_pair[1]
    same = [t for t, fn, fp in points if (fn, fp) == knee_pair]
    return min(same), knee_pair[0], knee_pair[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run", help="the run to read (a .llocr project)")
    ap.add_argument("--model", default=None, help="the model profile (its values fill the unmeasured types)")
    ap.add_argument("--rate", type=float, default=1.0, help="the knee's marginal rate (FP per caught error)")
    ap.add_argument("--min-blocks", type=int, default=3, help="a type with fewer blocks is not calibrated")
    args = ap.parse_args()

    path = Path(args.run)
    if not path.is_absolute():
        path = TEST2 / path
    project = cal.ev.read_project(path)
    rows = [r for r in cal.rows_of(project) if r["st"] != 0 and r["conf"] >= 0]
    print(f"===== {path.parent.name}/{path.stem.replace('OCR_TEST_', '')}  ({len(rows)} блоков с P(match))")

    thresholds = {}
    totals = [0, 0]
    for label in sorted({label_of(r) for r in rows}):
        sub = [r for r in rows if label_of(r) == label]
        errs = sum(1 for r in sub if r["truth"] == "err")
        cln = sum(1 for r in sub if r["truth"] == "clean")
        points = curve_of(rows, label)
        kt, kfn, kfp = knee(points, args.rate)
        if len(sub) < args.min_blocks:
            print(f"    {label:18s} err={errs:3d} clean={cln:3d}   корпусом не калибруется ({len(sub)} блоков)")
            continue
        t, fn, fp = threshold_for(points, (kfn, kfp), errs > 0)
        thresholds[label] = t
        totals[0] += fn
        totals[1] += fp
        print(f"    {label:18s} err={errs:3d} clean={cln:3d}   колено {kt:.2f} ({kfn}/{kfp}) "
              f"→ порог {t:.2f} ({fn}/{fp})")

    print(f"\n### пороги: {json.dumps(thresholds, ensure_ascii=False)}")
    print(f"    FN={totals[0]}  FP={totals[1]}")

    if args.model:
        profile = json.loads((PROFILES / f"{args.model}.json").read_text(encoding="utf-8"))
        role = profile["roles"]["decision"]
        by_block = role.get("matchThresholdByBlock", {})
        base = role.get("matchThreshold")
        print("\n### профиль")
        for label in sorted(by_block):
            if label in thresholds:
                print(f"    {label:18s} {thresholds[label]:.2f}")
            else:
                print(f"    {label:18s} {by_block[label]:.2f}  ← корпусом не измерено, оставлено значение профиля")
        print(f"    matchThreshold (база): {base:.2f}")


if __name__ == "__main__":
    main()
