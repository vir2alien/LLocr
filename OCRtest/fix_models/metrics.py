#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Метрики fix-стенда.

Считаем не «как ответила модель», а «что стало с блоком»:
  * вердикт приложения (Ok / Fixed / Review) — из порта гейта (gate.py);
  * CER кандидата и исправленного текста против ручного эталона;
  * «индексы»: доля индексов эталона (под- и надстрочные группы), присутствующих
    в тексте — именно они не читаются маленькой модели.
"""

import json
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
MATERIALS = REPO / "OCRtest" / "ocr_models" / "materials"

sys.path.insert(0, str(MATERIALS))
import compare as cmp  # noqa: E402


def load_corpus(path=None):
    path = Path(path) if path else HERE / "corpus" / "corpus.json"
    return json.loads(path.read_text(encoding="utf-8"))


def norm_for(block, text):
    if block["group"] in ("equation", "table"):
        return cmp.norm_eq(text)
    return cmp.norm_text(text)


def cer(block, text):
    return cmp.cer(norm_for(block, text), norm_for(block, block["gt"]))


BRACED_INDEX_RE = re.compile(r"[_^]\s*\{((?:[^{}]|\{[^{}]*\})*)\}")
BARE_INDEX_RE = re.compile(r"[_^]([A-Za-zА-Яа-яЁё0-9.]+)")


def indices(text):
    """Под- и надстрочные группы текста (содержимое скобок или «хвост» без скобок)."""
    found = []
    for m in BRACED_INDEX_RE.finditer(text or ""):
        inner = m.group(1)
        inner = re.sub(r"\\(?:text|mathrm|mathbf|mathit|bar|overline|hat|vec|tilde)\s*\{([^{}]*)\}", r"\1", inner)
        value = cmp.norm_eq(inner)
        if value:
            found.append(value)
    for m in BARE_INDEX_RE.finditer(text or ""):
        value = cmp.norm_eq(m.group(1))
        if value:
            found.append(value)
    return found


def index_hit_rate(block, text):
    """Доля индексов эталона, найденных в тексте (мультимножества, не подстроки)."""
    want = indices(block["gt"])
    if not want:
        return 1.0, 0, 0
    have = Counter(indices(text))
    hit = 0
    for index in want:
        if have[index] > 0:
            have[index] -= 1
            hit += 1
    return hit / len(want), hit, len(want)


def classify(block, corrected):
    """improved / damaged / same — по CER к эталону."""
    before = cer(block, block["candidate_wire"])
    after = cer(block, corrected) if corrected is not None else before
    if after < before - 1e-9:
        kind = "improved"
    elif after > before + 1e-9:
        kind = "damaged"
    else:
        kind = "same"
    return kind, before, after


def summarize(rows):
    """Сводка по вариантам: вердикты, улучшения, индексы."""
    variants = {}
    for row in rows:
        v = variants.setdefault(row["variant"], {"blocks": 0, "Ok": 0, "Fixed": 0, "Review": 0,
                                                 "improved": 0, "damaged": 0, "same": 0,
                                                 "cer_before": 0.0, "cer_after": 0.0,
                                                 "idx_before": 0.0, "idx_after": 0.0,
                                                 "idx_blocks": 0, "latency": 0.0, "tokens": 0})
        v["blocks"] += 1
        v[row["verdict"]] = v.get(row["verdict"], 0) + 1
        v[row["kind"]] = v.get(row["kind"], 0) + 1
        v["cer_before"] += row["cer_before"]
        v["cer_after"] += row["cer_after"]
        v["idx_before"] += row["idx_before"]
        v["idx_after"] += row["idx_after"]
        v["idx_blocks"] += 1
        v["latency"] += row.get("latency_s", 0.0)
        v["tokens"] += row.get("completion_tokens", 0) or 0
    for v in variants.values():
        n = max(1, v["blocks"])
        v["cer_before"] /= n
        v["cer_after"] /= n
        v["idx_before"] /= n
        v["idx_after"] /= n
        v["latency"] /= n
    return variants


def print_table(rows, title=""):
    variants = summarize(rows)
    if title:
        print(f"\n=== {title}")
    print(f"{'вариант':22s} {'Ok':>3s} {'Fix':>4s} {'Rev':>4s} {'улучш':>6s} {'порч':>5s} "
          f"{'CER до':>7s} {'CER после':>9s} {'инд до':>7s} {'инд после':>9s} {'с':>5s}")
    for name, v in sorted(variants.items(), key=lambda kv: (-kv[1]["improved"], kv[1]["cer_after"])):
        print(f"{name:22s} {v['Ok']:3d} {v['Fixed']:4d} {v['Review']:4d} {v['improved']:6d} {v['damaged']:5d} "
              f"{v['cer_before']:7.3f} {v['cer_after']:9.3f} {v['idx_before']:7.2f} {v['idx_after']:9.2f} {v['latency']:5.1f}")
    return variants
