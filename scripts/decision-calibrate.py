"""Threshold calibration for the decision profiles.

Reads the decision runs (the .llocr projects the app writes) and prints, per
block bucket and per block type, the FN/FP curve of the stored P(match) — the
curve the profile's thresholds are chosen on (ADR 173: the verdict is
p >= t, so a threshold sweep over the saved confidence re-derives the
operating point without a new run).

The numbers come from evaluate.py's own logic (the same truth, the same
buckets), so a report and this script never disagree.

Usage:
  python3 scripts/decision-calibrate.py                        # every run
  python3 scripts/decision-calibrate.py iteration5              # one iteration
  python3 scripts/decision-calibrate.py iteration5/D3-mini_02.llocr
  python3 scripts/decision-calibrate.py --policy d3-mini       # the shipped thresholds
"""

import importlib.util
import json
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OCRTEST = HERE.parent / "OCRtest"
TEST2 = OCRTEST / "decision_models" / "test 2"
MATERIALS = OCRTEST / "ocr_models" / "materials"
PROFILES = HERE.parent / "resources" / "profiles" / "models"

spec = importlib.util.spec_from_file_location("ev", TEST2 / "evaluate.py")
ev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ev)

sys.path.insert(0, str(MATERIALS))
import compare as cmp  # noqa: E402
import gt as gtmod  # noqa: E402

gtjson = json.loads((MATERIALS / "gt.json").read_text(encoding="utf-8"))
truth = json.loads((TEST2 / "truth.json").read_text(encoding="utf-8"))
THRESHOLDS = tuple(x / 100 for x in range(40, 96, 5))
ALIASES = {"formula": "equation", "reference": "ref_text"}


def rows_of(project):
    """The rows evaluate.py builds, plus the stored confidence."""
    rows = []
    for pi, page in enumerate(project["pages"]):
        for bi, b in enumerate(page["boxes"]):
            gtbox = gtjson["pages"][pi]["boxes"][bi]
            group = gtbox["group"]
            text = b["text"]
            if group in ("equation", "table"):
                ngt, nhyp = cmp.norm_eq(gtmod.GT[pi][bi]), cmp.norm_eq(text)
            else:
                ngt, nhyp = ev.norm_text_wrapper_aware(cmp, gtmod.GT[pi][bi]), ev.norm_text_wrapper_aware(cmp, text)
            cer = cmp.cer(ngt, nhyp)
            key = f"p{pi}b{bi}"
            t = "clean" if key in truth["clean_overrides"] else ("err" if cer > 0 else "clean")
            rows.append({"key": key, "label": b["label"], "group": group,
                         "bucket": ev.bucket_for(group, b["label"]), "st": b["checkStatus"],
                         "conf": b.get("confidence", -1.0), "truth": t})
    return rows


def curve(rows, thresholds=THRESHOLDS):
    """(threshold -> FN, FP) over the stored P(match)."""
    out = []
    for t in thresholds:
        fn = sum(1 for r in rows if r["truth"] == "err" and r["conf"] >= t)
        fp = sum(1 for r in rows if r["truth"] == "clean" and r["conf"] < t)
        out.append((t, fn, fp))
    return out


def auc(rows):
    """P(a clean block scores higher than an error block). 1.0 = perfect."""
    clean = sorted(r["conf"] for r in rows if r["truth"] == "clean")
    err = sorted(r["conf"] for r in rows if r["truth"] == "err")
    if not clean or not err:
        return None
    tot = 0.0
    for c in clean:
        tot += sum(1 for e in err if c > e) + 0.5 * sum(1 for e in err if c == e)
    return tot / (len(clean) * len(err))


def report(path):
    project = ev.read_project(path)
    rows = [r for r in rows_of(project) if r["st"] != 0 and r["conf"] >= 0]
    print(f"===== {path.parent.name}/{path.stem.replace('OCR_TEST_', '')}")

    for bname in ev.BUCKETS:
        sub = [r for r in rows if r["bucket"] == bname]
        if not sub:
            continue
        a = auc(sub)
        cv = curve(sub)
        print(f"  {bname:20s} err={sum(1 for r in sub if r['truth'] == 'err'):3d} "
              f"clean={sum(1 for r in sub if r['truth'] == 'clean'):3d} "
              f"AUC={'n/a' if a is None else f'{a:.3f}'}")
        print(f"      {'  '.join(f'{t:.2f}:{fn}/{fp}' for t, fn, fp in cv)}")

    print("  --- по типам блоков (порог, который пишется в профиль) ---")
    for label in sorted({r["label"] for r in rows}):
        sub = [r for r in rows if r["label"] == label]
        cv = curve(sub)
        print(f"    {label:18s} err={sum(1 for r in sub if r['truth'] == 'err'):3d} "
              f"clean={sum(1 for r in sub if r['truth'] == 'clean'):3d}  "
              + "  ".join(f"{t:.2f}:{fn}/{fp}" for t, fn, fp in cv))
    print()


def policy_report(path, model):
    """The shipped profile's thresholds scored on a run: FN, FP and the per-type share."""
    role = json.loads((PROFILES / f"{model}.json").read_text(encoding="utf-8"))["roles"]["decision"]
    by_block = role.get("matchThresholdByBlock", {})
    base = role.get("matchThreshold")
    project = ev.read_project(path)
    rows = [r for r in rows_of(project) if r["st"] != 0 and r["conf"] >= 0]
    fn = fp = 0
    per_type = {}
    for r in rows:
        label = ALIASES.get(r["label"], r["label"])
        t = by_block.get(label, base)
        flagged = r["conf"] < t
        slot = per_type.setdefault((r["label"], t), [0, 0])
        if flagged and r["truth"] == "clean":
            fp += 1
            slot[1] += 1
        elif not flagged and r["truth"] == "err":
            fn += 1
            slot[0] += 1
    print(f"===== {path.parent.name}/{path.stem.replace('OCR_TEST_', '')} — пороги профиля {model}")
    print(f"    FN={fn}  FP={fp}")
    for (label, t), (lfn, lfp) in sorted(per_type.items()):
        print(f"    {label:18s} t={t:.2f}  FN={lfn:3d}  FP={lfp:3d}")
    print()


def main():
    args = sys.argv[1:]
    policy = None
    if args and args[0] == "--policy":
        args.pop(0)
        policy = args.pop(0) if args else "d3-mini"
    if args:
        for arg in args:
            path = Path(arg)
            if not path.is_absolute():
                path = TEST2 / arg
            if path.is_dir():
                for f in sorted(path.glob("OCR_TEST_*.llocr")):
                    policy_report(f, policy) if policy else report(f)
            else:
                policy_report(path, policy) if policy else report(path)
        return
    for folder in sorted(p for p in TEST2.iterdir() if p.is_dir() and p.name.startswith("iteration")):
        for f in sorted(folder.glob("OCR_TEST_*.llocr")):
            policy_report(f, policy) if policy else report(f)


if __name__ == "__main__":
    main()
