"""Prepare rotated copies of the corpus renders for the orientation eval.

The app turns a page with a lossless quarter turn before any request goes
out, so the rotation here is the same lossless 90-degree step on the source
PNG. Layout additionally squashes the page hard to 1036x1036 (LayoutController
kLayoutImageSize, Qt::IgnoreAspectRatio) — the squashed copies are written
next to the full ones, because that is exactly what the model sees.
"""
from pathlib import Path
from PIL import Image

RENDERS = Path(__file__).parent.parent / "ocr_models" / "materials" / "renders"
OUT = Path(__file__).parent / "pages"
SQUASH = 1036

ROTATIONS = {
    "orig": lambda im: im,
    "rot90cw": lambda im: im.transpose(Image.ROTATE_270),
    "rot180": lambda im: im.transpose(Image.ROTATE_180),
    "rot90ccw": lambda im: im.transpose(Image.ROTATE_90),
}

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for src in sorted(RENDERS.glob("*.png")):
        with Image.open(src) as im:
            im.load()
            rgb = im.convert("RGB")
            for name, rot in ROTATIONS.items():
                turned = rot(rgb)
                full = OUT / f"{src.stem}_{name}.png"
                if not full.exists():
                    turned.save(full)
                squashed = turned.resize((SQUASH, SQUASH), Image.LANCZOS)
                sq = OUT / f"{src.stem}_{name}_1036.png"
                if not sq.exists():
                    squashed.save(sq)
        print("prepared", src.stem)
    print("done ->", OUT)

if __name__ == "__main__":
    main()
