#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Кривые FN/FP(t) и пороги для прогона Jev (visual diff).

Та же логика, что archive/decision-text/scripts/decision-calibrate.py и
decision-eval/pick-thresholds.py (колено кривой: последняя ступень, где одна
пойманная ошибка стоит не больше --rate FP; FP=0 — край чистой области; типы
меньше --min-blocks блоков не калибруются), но с путями после переноса стенда
в archive — там константы путей ещё старые. Истина — evaluate.py (те же
строки: norm + CER + clean_overrides; отпечатки truth.json совпадают 195/195,
устаревших блоков нет).
"""
import argparse
import importlib.util
import json
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEST2 = HERE.parent
REPO = TEST2.parent.parent.parent
MATERIALS = REPO / "OCRtest" / "ocr_models" / "materials"
PROFILES = REPO / "resources" / "profiles" / "models"

spec = importlib.util.spec_from_file_location("ev", TEST2 / "evaluate.py")
ev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ev)

sys.path.insert(0, str(MATERIALS))
import compare as cmp  # noqa: E402
import gt as gtmod  # noqa: E402

gtjson = json.loads((MATERIALS / "gt.json").read_text(encoding="utf-8"))
truth = json.loads((TEST2 / "truth.json").read_text(encoding="utf-8"))

ALIASES = {"formula": "equation"}
STD_GRID = tuple(x / 100 for x in range(40, 96, 5))   # сетка стенда 0.40..0.95
FINE_GRID = tuple(x / 100 for x in range(40, 99))     # + 0.96..0.98 для края чистой области


def read_project(path):
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(path) as z:
            z.extract("project.json", tmp)
        with open(Path(tmp) / "project.json", encoding="utf-8") as f:
            return json.load(f)


def rows_of(project):
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


def curve(rows, grid):
    return [(t,
             sum(1 for r in rows if r["truth"] == "err" and r["conf"] >= t),
             sum(1 for r in rows if r["truth"] == "clean" and r["conf"] < t))
            for t in grid]


def auc(rows):
    clean = sorted(r["conf"] for r in rows if r["truth"] == "clean")
    err = sorted(r["conf"] for r in rows if r["truth"] == "err")
    if not clean or not err:
        return None
    tot = 0.0
    for c in clean:
        tot += sum(1 for e in err if c > e) + 0.5 * sum(1 for e in err if c == e)
    return tot / (len(clean) * len(err))


def knee(points, rate):
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
    ap = argparse.ArgumentParser()
    ap.add_argument("run", nargs="?", default=str(TEST2 / "iteration8" / "OCR_TEST_minicpm-jev.llocr"))
    ap.add_argument("--rate", type=float, default=1.0)
    ap.add_argument("--min-blocks", type=int, default=3)
    ap.add_argument("--model", default="minicpm-jev")
    args = ap.parse_args()

    project = read_project(Path(args.run))
    rows = [r for r in rows_of(project) if r["st"] != 0 and r["conf"] >= 0]
    print(f"===== {Path(args.run).parent.name}/{Path(args.run).stem.replace('OCR_TEST_', '')}  ({len(rows)} блоков с P(match))\n")

    print("--- корзины evaluate.py (AUC + кривая, сетка 0.05) ---")
    for bname in ev.BUCKETS:
        sub = [r for r in rows if r["bucket"] == bname]
        if not sub:
            continue
        a = auc(sub)
        cv = curve(sub, STD_GRID)
        print(f"  {bname:20s} err={sum(1 for r in sub if r['truth'] == 'err'):3d} "
              f"clean={sum(1 for r in sub if r['truth'] == 'clean'):3d} "
              f"AUC={'n/a' if a is None else format(a, '.3f')}")
        print(f"      {'  '.join(f'{t:.2f}:{fn}/{fp}' for t, fn, fp in cv)}")

    print("\n--- по типам блоков (кривая, сетка 0.01) ---")
    for label in sorted({ALIASES.get(r["label"], r["label"]) for r in rows}):
        sub = [r for r in rows if ALIASES.get(r["label"], r["label"]) == label]
        errs = sum(1 for r in sub if r["truth"] == "err")
        cln = sum(1 for r in sub if r["truth"] == "clean")
        cv = curve(sub, FINE_GRID)
        print(f"  {label:16s} err={errs:3d} clean={cln:3d}  "
              + " ".join(f"{t:.2f}:{fn}/{fp}" for t, fn, fp in cv if fn > 0 or fp > 0 or t in (0.4, 0.95)))

    print(f"\n--- пороги по колену (rate={args.rate}, min-blocks={args.min_blocks}) ---")
    grid = FINE_GRID
    thresholds, totals = {}, [0, 0]
    for label in sorted({ALIASES.get(r["label"], r["label"]) for r in rows}):
        sub = [r for r in rows if ALIASES.get(r["label"], r["label"]) == label]
        errs = sum(1 for r in sub if r["truth"] == "err")
        points = curve(sub, grid)
        if len(sub) < args.min_blocks:
            print(f"  {label:16s} err={errs:3d} clean={len(sub) - errs:3d}   корпусом не калибруется ({len(sub)} блоков)")
            continue
        kt, kfn, kfp = knee(points, args.rate)
        t, fn, fp = threshold_for(points, (kfn, kfp), errs > 0)
        thresholds[label] = t
        totals[0] += fn
        totals[1] += fp
        print(f"  {label:16s} err={errs:3d} clean={len(sub) - errs:3d}   колено {kt:.2f} ({kfn}/{kfp}) → порог {t:.2f} ({fn}/{fp})")

    print(f"\n### пороги: {json.dumps(thresholds, ensure_ascii=False)}")
    print(f"    FN={totals[0]}  FP={totals[1]}")

    profile = json.loads((PROFILES / f"{args.model}.json").read_text(encoding="utf-8"))
    role = profile["roles"]["decision"]
    print("\n### текущий профиль:", role.get("matchThresholdByBlock"), "base", role.get("matchThreshold"))


if __name__ == "__main__":
    main()
