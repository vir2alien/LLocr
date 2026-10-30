#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Прогон MiniCPM-OCR Jev (visual diff, /v1/systemone) по корпусу test 2.

Вторая картинка запроса — рендер кандидата, снятый собственным
CandidateRenderer приложения (candidate_render_dump, ADR 184: порог
измеряется против запроса, который уходит на провод). Кропы — 300 dpi
страницы корпуса, как у приложения (corpus.py стенда).

Пишет scores.json (P(match) по каждому блоку) и .llocr с вердиктами
(порог из профиля), который evaluate.py считает обычным прогоном.

  py -I jev_score.py --base-url http://127.0.0.1:18999 [--threshold 0.5] [--limit N]
"""
import argparse
import json
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
STAND = HERE.parent.parent.parent / "archive" / "decision-text" / "scripts" / "decision-eval"
sys.path.insert(0, str(STAND))

import corpus  # noqa: E402
from client import SystemOneClient, SystemOneError  # noqa: E402

CORPUS = HERE.parent / "minicpm-ocr-jev.llocr"
RENDERS = HERE / "jev-runs" / "renders"


def noul_probability(answer):
    if isinstance(answer, (int, float)):
        return float(answer)
    if isinstance(answer, dict):
        for key in ("noul", "probability"):
            if isinstance(answer.get(key), (int, float)):
                return float(answer[key])
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", default="http://127.0.0.1:18999")
    ap.add_argument("--threshold", type=float, default=0.5)
    ap.add_argument("--limit", type=int, default=0, help="только первые N блоков (пробный прогон)")
    ap.add_argument("--out", default=str(HERE / "jev-runs" / "scores.json"))
    ap.add_argument("--project-out", default=str(HERE / "iteration8" / "OCR_TEST_minicpm-jev.llocr"))
    args = ap.parse_args()

    manifest = json.loads((RENDERS / "manifest.json").read_text(encoding="utf-8"))
    entries = manifest["blocks"]
    if args.limit:
        entries = entries[: args.limit]

    work = HERE / "jev-runs" / "work-corpus"
    project = corpus.load_project(CORPUS, work / "extract")
    corpus_blocks = {(b.page, b.index): b for b in corpus.build_blocks(project, work / "extract", work / "pages")}

    client = SystemOneClient(args.base_url, timeout_s=300.0)
    client.health()

    scores = {}
    started = time.time()
    for i, entry in enumerate(entries):
        pi, bi = entry["key"][1:].split("b")
        block = corpus_blocks[(int(pi), int(bi))]
        render_png = (RENDERS / entry["render"]).read_bytes()
        try:
            raw = client.ask_raw(entry["state"], [block.crop, render_png], {"match": {"type": "noul"}})
        except SystemOneError as exc:
            print(f"  {entry['key']} ({entry['label']}): {exc.kind}: {exc.detail[:200]}")
            continue
        p = noul_probability(raw.get("answers", {}).get("match"))
        if p is None:
            print(f"  {entry['key']}: no noul answer: {json.dumps(raw)[:200]}")
            continue
        scores[entry["key"]] = {"p": p, "label": entry["label"], "latency": raw.get("_latency_s")}
        if (i + 1) % 20 == 0:
            rate = (i + 1) / (time.time() - started)
            print(f"  {i + 1}/{len(entries)}  ({rate:.1f} блока/с)")

    Path(args.out).write_text(json.dumps(scores, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"scored {len(scores)}/{len(entries)} -> {args.out}")

    verdicts = json.loads(json.dumps(project))
    for pi, page in enumerate(verdicts["pages"]):
        for bi, box in enumerate(page.get("boxes", [])):
            key = f"p{pi}b{bi}"
            if key in scores:
                p = scores[key]["p"]
                box["confidence"] = p
                box["checkStatus"] = 1 if p >= args.threshold else 4
    out = Path(args.project_out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(CORPUS) as src, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in src.namelist():
            if name == "project.json":
                continue
            z.writestr(name, src.read(name))
        z.writestr("project.json", json.dumps(verdicts, ensure_ascii=False))
    print(f"verdicts (t={args.threshold}) -> {out}")


if __name__ == "__main__":
    main()
