"""Corpus loading: .llocr project -> page rasters -> per-block PNG crops.

Replicates the app's rendering pipeline:
  - PDF pages render at 300 DPI (DocumentModel kDpi), image sources at native size
  - crop rect = (qRound(x*W), qRound(y*H), qRound(w*W), qRound(h*H)) intersected
    with the page image (AppController::croppedImage), qRound(v) = floor(v + 0.5)
"""

import dataclasses
import io
import json
import math
import re
import zipfile
from pathlib import Path

import pymupdf
from PIL import Image

DPI = 300


@dataclasses.dataclass
class Block:
    page: int
    index: int
    uid: str
    label: str
    text: str
    box: tuple  # (x, y, w, h) normalized
    crop: bytes  # PNG bytes, as-is policy
    crop_size: tuple  # (w, h) px


def qround(v: float) -> int:
    return math.floor(v + 0.5)


def load_project(llocr_path: Path, extract_dir: Path) -> dict:
    extract_dir.mkdir(parents=True, exist_ok=True)
    marker = extract_dir / "project.json"
    if not marker.exists() or marker.stat().st_mtime < llocr_path.stat().st_mtime:
        with zipfile.ZipFile(llocr_path) as zf:
            zf.extractall(extract_dir)
    return json.loads((extract_dir / "project.json").read_text(encoding="utf-8"))


def _render_source(source: dict, source_dir: Path, page_index: int) -> Image.Image:
    path = source_dir / source["file"]
    if source.get("type") == "image":
        return Image.open(path).convert("RGB")
    doc = pymupdf.open(path)
    page = doc[page_index if page_index >= 0 else 0]
    pix = page.get_pixmap(dpi=DPI)
    img = Image.frombytes("RGB", (pix.width, pix.height), pix.samples)
    doc.close()
    return img


def _crop(img: Image.Image, box: tuple) -> Image.Image:
    x, y, w, h = (qround(v * s) for v, s in zip(box, (img.width, img.height, img.width, img.height)))
    left, top = max(0, x), max(0, y)
    right, bottom = min(img.width, x + w), min(img.height, y + h)
    right, bottom = max(right, left + 1), max(bottom, top + 1)
    return img.crop((left, top, right, bottom))


def build_blocks(project: dict, extract_dir: Path, pages_dir: Path) -> list[Block]:
    pages_dir.mkdir(parents=True, exist_ok=True)
    sources = project["sources"]
    source_dir = extract_dir
    blocks = []
    for pno, page in enumerate(project["pages"]):
        cached = pages_dir / f"p{pno}.png"
        if cached.exists():
            img = Image.open(cached)
            img.load()
        else:
            img = _render_source(sources[page["sourceId"]], source_dir, page.get("sourcePageIndex", -1))
            img.save(cached)
        for idx, box in enumerate(page.get("boxes", [])):
            text = box.get("correctedText") or box.get("text", "")
            crop = _crop(img, (box["x"], box["y"], box["w"], box["h"]))
            buf = io.BytesIO()
            crop.save(buf, format="PNG")
            blocks.append(
                Block(
                    page=pno,
                    index=idx,
                    uid=f"p{pno}b{idx}",
                    label=box.get("label", "text"),
                    text=text,
                    box=(box["x"], box["y"], box["w"], box["h"]),
                    crop=buf.getvalue(),
                    crop_size=crop.size,
                )
            )
    return blocks


def apply_policy(crop_png: bytes, policy: str) -> tuple[bytes, tuple]:
    """Return (png bytes, size) for an image policy. Unchanged policies return input."""
    if policy == "as_is":
        img = Image.open(io.BytesIO(crop_png))
        return crop_png, img.size
    img = Image.open(io.BytesIO(crop_png))
    w, h = img.size
    if policy == "up2x_small":
        if min(w, h) >= 128:
            return crop_png, (w, h)
        img = img.resize((w * 2, h * 2), Image.LANCZOS)
    elif policy == "down1024":
        if max(w, h) <= 1024:
            return crop_png, (w, h)
        scale = 1024.0 / max(w, h)
        img = img.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    else:
        raise ValueError(f"unknown image policy {policy}")
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue(), img.size


def blank_image_like(crop_png: bytes) -> bytes:
    img = Image.open(io.BytesIO(crop_png))
    blank = Image.new("RGB", (max(64, img.width), max(32, img.height)), "white")
    buf = io.BytesIO()
    blank.save(buf, format="PNG")
    return buf.getvalue()


def render_latex(text: str) -> bytes | None:
    """Render a formula's LaTeX source to a PNG via matplotlib mathtext.

    Returns None when mathtext cannot parse the source (unsupported commands,
    broken markup); the caller skips such cases.
    """
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    tex = text.strip()
    for fence in ("$$", "$"):
        if tex.startswith(fence) and tex.endswith(fence) and len(tex) > 2 * len(fence):
            tex = tex[len(fence):-len(fence)]
            break
    tex = re.sub(r"\\qquad|\\quad", r"\\ ", tex)
    tex = re.sub(r"\\[cdt]frac", r"\\frac", tex)
    try:
        fig = plt.figure(figsize=(0.5, 0.5), dpi=300)
        fig.text(0.5, 0.5, f"${tex}$", fontsize=11, ha="center", va="center", color="black")
        buf = io.BytesIO()
        fig.savefig(buf, format="png", bbox_inches="tight", pad_inches=0.05,
                    facecolor="white", dpi=300)
        plt.close(fig)
        return buf.getvalue()
    except Exception:
        plt.close("all")
        return None
