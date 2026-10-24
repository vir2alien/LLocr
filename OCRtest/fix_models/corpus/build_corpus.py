#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собирает корпус для стенда fix-моделей (OCRtest/fix_models).

Источники:
  * .llocr-проект — разметка блоков, координаты и текст распознавания
    (кандидат на исправление);
  * ocr_models/materials/gt.json — ручной эталон каждого блока (истина);
  * исходник страницы внутри .llocr — рендер в 300 dpi (как приложение,
    DocumentModel kDpi) и кроп блока (AppController::croppedImage).

Выход:
  corpus/corpus.json     — описание блоков (кандидат, эталон, кроп);
  corpus/crops/<uid>.png — кропы блоков;
  corpus/_pages/*.png    — рендеры страниц (кэш, в git не нужен).

Ручные правки эталона живут в corpus/overrides.json: {"p03b02": {"gt": "...", "reason": "..."}}.

Запуск:
    python3 corpus/build_corpus.py
    python3 corpus/build_corpus.py --project PATH --gt PATH --dpi 300
"""

import argparse
import json
import math
import re
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]  # LLocr/

DEFAULT_PROJECT = REPO / "OCRtest" / "decision_models" / "test 2" / "OCR_TEST_MinerU2.5-Pro.llocr"
DEFAULT_GT = REPO / "OCRtest" / "ocr_models" / "materials" / "gt.json"

# Блоки страницы p03 (теплообменники, формулы 2.68-2.70): пять формул и два
# текста с формулами — ровно те, где у qwen3.5-4b не получаются индексы.
DEFAULT_BLOCKS = ["p03b02", "p03b03", "p03b05", "p03b09", "p03b11", "p03b12", "p03b14"]

# То же, что core/LatexText::canonical (ADR 180, 194): пробелы перед
# скобками и после открывающей скобки убираются, "\tag {" -> "\tag{".
SPACE_BEFORE_BRACE = re.compile(r"([\w}\)])\s+([{}\[\]()])")
SPACE_AFTER_OPEN = re.compile(r"([{\[(])\s+")


def normalize_latex_candidate(text: str) -> str:
    import unicodedata

    out = SPACE_BEFORE_BRACE.sub(r"\1\2", text)
    out = SPACE_AFTER_OPEN.sub(r"\1", out)
    out = out.replace("\\tag {", "\\tag{")
    return unicodedata.normalize("NFC", out)


def qround(v: float) -> int:
    return math.floor(v + 0.5)


def crop_rect(box: dict, width: int, height: int) -> tuple:
    x = qround(box["x"] * width)
    y = qround(box["y"] * height)
    w = qround(box["w"] * width)
    h = qround(box["h"] * height)
    left, top = max(0, x), max(0, y)
    right, bottom = min(width, x + w), min(height, y + h)
    right, bottom = max(right, left + 1), max(bottom, top + 1)
    return left, top, right, bottom


def render_page(pdf_path: Path, page_index: int, dpi: int, out_png: Path):
    import pymupdf
    from PIL import Image

    doc = pymupdf.open(pdf_path)
    page = doc[page_index if page_index >= 0 else 0]
    pix = page.get_pixmap(dpi=dpi)
    img = Image.frombytes("RGB", (pix.width, pix.height), pix.samples)
    doc.close()
    out_png.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_png)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", default=str(DEFAULT_PROJECT))
    ap.add_argument("--gt", default=str(DEFAULT_GT))
    ap.add_argument("--dpi", type=int, default=300)
    ap.add_argument("--blocks", default=",".join(DEFAULT_BLOCKS), help="uid через запятую")
    ap.add_argument("--groups", default="", help="категории из gt.json через запятую (equation,ru_math_text,...)")
    ap.add_argument("--out", default=str(HERE / "corpus.json"))
    args = ap.parse_args()

    project_path = Path(args.project).resolve()
    gt_path = Path(args.gt).resolve()
    overrides_path = HERE / "overrides.json"
    overrides = json.loads(overrides_path.read_text(encoding="utf-8")) if overrides_path.exists() else {}

    work = HERE / "_project"
    work.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(project_path) as zf:
        zf.extractall(work)
    project = json.loads((work / "project.json").read_text(encoding="utf-8"))
    gt = json.loads(gt_path.read_text(encoding="utf-8"))

    wanted = [u.strip() for u in args.blocks.split(",") if u.strip()]
    if args.groups:
        groups = {g.strip() for g in args.groups.split(",") if g.strip()}
        for page in gt["pages"]:
            for index, gt_box in enumerate(page["boxes"]):
                if gt_box.get("group") in groups:
                    wanted.append(f"p{page['index']:02d}b{index:02d}")
    pages_dir = HERE / "_pages"
    crops_dir = HERE / "crops"
    crops_dir.mkdir(parents=True, exist_ok=True)

    blocks = []
    for uid in wanted:
        match = re.fullmatch(r"p(\d+)b(\d+)", uid)
        if not match:
            raise SystemExit(f"плохой uid: {uid}")
        pi, bi = int(match.group(1)), int(match.group(2))

        page = project["pages"][pi]
        box = page["boxes"][bi]
        gt_box = gt["pages"][pi]["boxes"][bi]

        source = project["sources"][page["sourceId"]]
        src_path = work / source["file"]
        png = pages_dir / f"p{pi}.png"
        if png.exists():
            from PIL import Image

            img = Image.open(png)
            img.load()
        else:
            img = render_page(src_path, page.get("sourcePageIndex", -1), args.dpi, png)

        rect = crop_rect(box, img.width, img.height)
        crop = img.crop(rect)
        crop_path = crops_dir / f"{uid}_{box.get('label', 'text')}.png"
        crop.save(crop_path)

        raw = box.get("correctedText") or box.get("text", "")
        gt_text = gt_box.get("gt_text")
        note = ""
        if uid in overrides:
            gt_text = overrides[uid].get("gt", gt_text)
            note = overrides[uid].get("reason", "")

        blocks.append({
            "uid": uid,
            "page": pi,
            "index": bi,
            "label": box.get("label", "text"),
            "group": gt_box.get("group"),
            "candidate": raw,
            "candidate_wire": normalize_latex_candidate(raw) if box.get("label") in ("equation", "formula") else raw,
            "gt": gt_text,
            "gt_source": "override" if uid in overrides else "gt.json",
            "gt_note": note,
            "crop": str(crop_path.relative_to(HERE.parent)),
            "crop_size": list(crop.size),
        })
        print(f"{uid} {box.get('label'):8s} crop={crop.size} -> {crop_path.name}")

    out = Path(args.out)
    out.write_text(json.dumps({
        "description": "Корпус fix-стенда: кандидат — текст распознавания MinerU2.5-Pro, истина — ручной эталон (gt.json + overrides.json).",
        "project": str(project_path),
        "gt": str(gt_path),
        "dpi": args.dpi,
        "blocks": blocks,
    }, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\n{len(blocks)} блоков -> {out}")


if __name__ == "__main__":
    main()
