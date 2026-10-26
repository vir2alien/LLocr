#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сводка по всем прогонам: CER и доля индексов, с поправкой на LaTeX-мусор.

Мусор — это то, что модель пишет «в обвязке», а не читает с картинки:
`\\nonumber`, `\\displaystyle`, `\\qquad` и прочие отступы, `\\\\` (перевод строки
внутри формулы), окружения `\\begin{aligned}` и т.п. Сравнение их с эталоном
считает их символами, которых в эталоне нет.
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import metrics  # noqa: E402

NOISE = [
    (re.compile(r"\\nonumber"), ""),
    (re.compile(r"\\displaystyle"), ""),
    (re.compile(r"\\limits"), ""),
    (re.compile(r"\\(?:qquad|quad|,|;|!)"), ""),
    (re.compile(r"\\begin\{[a-zA-Z*]+\}|\\end\{[a-zA-Z*]+\}"), ""),
]


def denoise(text):
    out = text or ""
    for pattern, replacement in NOISE:
        out = pattern.sub(replacement, out)
    return out.replace(chr(92) * 2, " ")


def main():
    corpus = {b["uid"]: b for b in metrics.load_corpus(HERE / "corpus" / "corpus_probe5.json")["blocks"]}
    runs = [
        ("gemma-4-e4b (4B)", "results/probe_gemma4e4b/scored.json"),
        ("qwen3.5-4b (4B)", "results/probe_qwen35_4b/scored.json"),
        ("minicpm-o-4.5 (8B)", "results/probe_minicpm_o45/scored.json"),
        ("qwen3.5-9b (9B)", "results/probe_qwen35_9b/scored.json"),
        ("gemma-4-12b (12B)", "results/probe_gemma4_12b/scored.json"),
    ]
    print(f"{'модель':19s} {'CER канд':>9s} {'CER fix':>8s} {'CER read':>9s} {'CER read чист':>14s} "
          f"{'инд read':>9s} {'инд чист':>9s}  вердикты fix")
    for name, rel in runs:
        path = HERE / rel
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        rows = data["scored"]
        n = len(rows)
        cand = fix = read = clean = 0.0
        idx = idx_clean = 0.0
        verdicts = {}
        for row in rows:
            block = corpus[row["uid"]]
            cand += metrics.cer(block, block["candidate_wire"])
            fix += metrics.cer(block, row["fix"]["result"])
            read += metrics.cer(block, row["read"]["result"])
            clean += metrics.cer(block, denoise(row["read"]["result"]))
            idx += row["read"]["idx"]
            idx_clean += metrics.index_hit_rate(block, denoise(row["read"]["result"]))[0]
            verdicts[row["fix"]["verdict"]] = verdicts.get(row["fix"]["verdict"], 0) + 1
        v = ", ".join(f"{k}×{c}" for k, c in sorted(verdicts.items()))
        print(f"{name:19s} {cand/n:9.3f} {fix/n:8.3f} {read/n:9.3f} {clean/n:14.3f} "
              f"{idx/n:9.2f} {idx_clean/n:9.2f}  {v}")


if __name__ == "__main__":
    main()
