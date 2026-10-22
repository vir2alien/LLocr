#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Отчёт по прогону fix-стенда: сводка + разбор по блокам.

    python3 report.py results/r1/rows.jsonl
    python3 report.py results/r1/rows.jsonl --detail img_2x
"""

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import metrics  # noqa: E402


def load_rows(path):
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def detail(rows, variant, corpus):
    blocks = {b["uid"]: b for b in corpus["blocks"]}
    for row in rows:
        if row["variant"] != variant:
            continue
        block = blocks.get(row["uid"])
        if not block:
            continue
        print(f"--- {row['uid']} {row['label']}  вердикт={row['verdict']}  {row['kind']}  "
              f"CER {row['cer_before']:.3f} -> {row['cer_after']:.3f}  "
              f"индексы {row['idx_before']:.2f} -> {row['idx_after']:.2f}")
        if row.get("reason"):
            print(f"    причина: {row['reason']}")
        print(f"    кандидат: {block['candidate_wire'][:300]}")
        print(f"    ответ   : {row['reply'][:400]}")
        print(f"    эталон  : {block['gt'][:300]}")
        want = metrics.indices(block["gt"])
        have = Counter(metrics.indices(row.get("corrected") or block["candidate_wire"]))
        print(f"    индексы эталона: {want}")
        print(f"    индексы в тексте: {list(have.elements())}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rows", nargs="?", default=str(HERE / "results" / "r1" / "rows.jsonl"))
    ap.add_argument("--detail", default=None, help="вариант для подробного разбора")
    ap.add_argument("--subset", default="", help="all | errors (только блоки с ошибками в кандидате)")
    ap.add_argument("--corpus", default=str(HERE / "corpus" / "corpus.json"))
    args = ap.parse_args()

    rows = load_rows(args.rows)
    metrics.print_table(rows, title=str(args.rows))
    if args.subset == "errors":
        broken = {r["uid"] for r in rows if r["cer_before"] > 0}
        subset = [r for r in rows if r["uid"] in broken]
        print(f"\n--- только блоки с ошибками в кандидате ({len(broken)} шт.) ---")
        metrics.print_table(subset)
    if args.detail:
        corpus = json.loads(Path(args.corpus).read_text(encoding="utf-8"))
        print()
        detail(rows, args.detail, corpus)


if __name__ == "__main__":
    main()
