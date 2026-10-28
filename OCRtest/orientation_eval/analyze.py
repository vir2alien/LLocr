"""Analyze the layout replies across rotations.

Questions this answers:
1. Does the model find blocks on rotated pages (box counts vs upright)?
2. Which rotate_ tokens appear per rotation (per-block orientation signal)?
3. In WHICH frame does the model report coordinates on a rotated page —
   the input image's frame (boxes track the pixels) or a canonical upright
   frame (boxes keep their upright geometry)? Measured two ways:
   - ink alignment: ink fraction inside each box mapped onto the input image;
   - geometry match: overlap between the reply's boxes and the upright
     reply's boxes mapped into the input frame by each candidate rotation.
4. Reading order: where does the reply's FIRST box sit in the input frame
   (top-left of text space maps to a different corner per rotation).
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).parent
PAGES = ROOT / "pages"
LAYOUT = ROOT / "replies" / "layout"

BOX = re.compile(r"<\|box_start\|>\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*<\|box_end\|>\s*<\|ref_start\|>(\w+?)<\|ref_end\|>")
ROT = re.compile(r"<\|rotate_(\w+)\|>")
MAXC = 1000.0

def lines_of(content):
    out = []
    for m in BOX.finditer(content):
        x1, y1, x2, y2 = (int(m.group(i)) for i in range(1, 5))
        rot = ROT.search(content[m.end():m.end() + 30])
        out.append({"label": m.group(5), "box": (x1, y1, x2, y2),
                    "rot": rot.group(1) if rot else "-"})
    return out

def ink_fraction(img_gray, box):
    w, h = img_gray.shape[1], img_gray.shape[0]
    x1, y1, x2, y2 = box
    px = (max(0, int(x1 / MAXC * w)), max(0, int(y1 / MAXC * h)),
          min(w, int(x2 / MAXC * w)), min(h, int(y2 / MAXC * h)))
    if px[2] - px[0] < 2 or px[3] - px[1] < 2:
        return 0.0
    region = img_gray[px[1]:px[3], px[0]:px[2]]
    thr = region.mean() * 0.85
    return float((region < thr).mean())

def rotate_box(box, rot, w, h):
    """Map a box from the upright frame into the input frame for a page that
    was rotated by `rot` (orig/rot90cw/rot180/rot90ccw), coords normalized."""
    x1, y1, x2, y2 = box
    if rot == "orig":
        return (x1, y1, x2, y2)
    if rot == "rot180":
        return (MAXC - x2, MAXC - y2, MAXC - x1, MAXC - y1)
    if rot == "rot90cw":
        # upright point (x,y) lands at input (MAXC-y, x) for a 90° CW turn
        return (MAXC - y2, x1, MAXC - y1, x2)
    if rot == "rot90ccw":
        return (y1, MAXC - x2, y2, MAXC - x1)
    raise ValueError(rot)

def iou(a, b):
    ax1, ay1, ax2, ay2 = a; bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    if ix2 <= ix1 or iy2 <= iy1:
        return 0.0
    inter = (ix2 - ix1) * (iy2 - iy1)
    area_a = (ax2 - ax1) * (ay2 - ay1)
    area_b = (bx2 - bx1) * (by2 - by1)
    return inter / (area_a + area_b - inter)

def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    replies = {}
    for f in sorted(LAYOUT.glob("*_1036.json")):
        stem = f.stem[:-len("_1036")]
        page, rot = stem.rsplit("_", 1)
        replies[(page, rot)] = lines_of(json.loads(f.read_text(encoding="utf-8"))["choices"][0]["message"]["content"])

    pages = sorted({p for p, _ in replies})
    if only:
        pages = [p for p in pages if only in p]

    for page in pages:
        if (page, "orig") not in replies:
            continue
        orig = replies[(page, "orig")]
        orig_boxes = [b["box"] for b in orig]
        print(f"== {page} (upright reply: {len(orig)} boxes)")
        for rot in ("rot90cw", "rot180", "rot90ccw"):
            if (page, rot) not in replies:
                continue
            ls = replies[(page, rot)]
            png = PAGES / f"{page}_{rot}.png"
            with Image.open(png) as im:
                im.load()
                gray = np.asarray(im.convert("L"))
            img_w, img_h = gray.shape[1], gray.shape[0]
            toks = Counter(b["rot"] for b in ls)
            labels = Counter(b["label"] for b in ls)

            # ink alignment of the reply's boxes mapped straight onto the input
            ink_direct = np.mean([ink_fraction(gray, b["box"]) for b in ls]) if ls else 0.0
            # ink when the boxes are first mapped through the inverse rotation
            ink_mapped = np.mean([ink_fraction(gray, rotate_box(b["box"], rot, img_w, img_h)) for b in ls]) if ls else 0.0

            # geometry: how many of the reply's boxes match the upright reply
            # mapped into the input frame by this rotation (input-frame hypothesis)
            upright_mapped = [rotate_box(b, rot, img_w, img_h) for b in orig_boxes]
            match_input = 0
            for b in ls:
                if any(iou(b["box"], u) >= 0.6 for u in upright_mapped):
                    match_input += 1
            # ...or match the upright reply as-is (upright-frame hypothesis)
            match_upright = 0
            for b in ls:
                if any(iou(b["box"], u) >= 0.6 for u in orig_boxes):
                    match_upright += 1

            first = ls[0]["box"] if ls else None
            print(f"  {rot}: {len(ls)} boxes | rot toks {dict(toks)} | ink direct {ink_direct:.3f} mapped {ink_mapped:.3f} | match input-frame {match_input}/{len(ls)} upright-frame {match_upright}/{len(ls)} | first box {first}")
            print(f"      labels {dict(labels)}")

if __name__ == "__main__":
    main()
