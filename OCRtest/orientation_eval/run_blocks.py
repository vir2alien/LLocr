"""Run blockRecognition (the app's CheckController request) over block crops
taken from rotated pages, and store the raw replies.

Crops use the layout reply of the SAME rotation — that is what the app does:
layout returns normalized boxes in the input image's frame, the app crops the
full page image by those fractions. Text blocks read "\nText Recognition:",
tables "\nTable Recognition:", equations "\nFormula Recognition:".
Sampling: temperature 0, top_p 0.01, repeat_penalty 1.0, presence_penalty 1.0,
frequency_penalty 0.05 (mineru.json blockRecognition role).
"""
import base64
import json
import re
import sys
import time
import urllib.request
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).parent
PAGES = ROOT / "pages"
LAYOUT = ROOT / "replies" / "layout"
OUT = ROOT / "replies" / "blocks"
URL = "http://127.0.0.1:8934/v1/chat/completions"

SYSTEM = "You are a helpful assistant."
PROMPTS = {"text": "\nText Recognition:", "table": "\nTable Recognition:",
           "equation": "\nFormula Recognition:"}
PARAMS = {
    "temperature": 0.0,
    "top_p": 0.01,
    "repeat_penalty": 1.0,
    "presence_penalty": 1.0,
    "frequency_penalty": 0.05,
    "cache_prompt": True,
}
MAX_TOKENS = 2048
BOX_RE = re.compile(r"<\|box_start\|>\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*<\|box_end\|>\s*<\|ref_start\|>(\w+?)<\|ref_end\|>")

def blocks_of(content: str):
    out = []
    for m in BOX_RE.finditer(content):
        vals = [int(m.group(i)) for i in range(1, 5)]
        out.append({"x1": vals[0], "y1": vals[1], "x2": vals[2], "y2": vals[3], "label": m.group(5)})
    return out

def crop_png(png: Path, box: dict) -> bytes:
    with Image.open(png) as im:
        w, h = im.size
        px = (round(box["x1"] / 1000 * w), round(box["y1"] / 1000 * h),
              round(box["x2"] / 1000 * w), round(box["y2"] / 1000 * h))
        px = (max(0, px[0]), max(0, px[1]), min(w, px[2]), min(h, px[3]))
        if px[2] - px[0] < 2 or px[3] - px[1] < 2:
            return b""
        im.load()
        crop = im.convert("RGB").crop(px)
        import io
        buf = io.BytesIO()
        crop.save(buf, "PNG")
        return buf.getvalue()

def request_block(png_bytes: bytes, prompt: str) -> dict:
    data = base64.b64encode(png_bytes).decode()
    body = {
        "messages": [
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": [
                {"type": "image_url", "image_url": {"url": f"data:image/png;base64,{data}"}},
                {"type": "text", "text": prompt},
            ]},
        ],
        "max_tokens": MAX_TOKENS,
        "stream": False,
        **PARAMS,
    }
    req = urllib.request.Request(URL, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    started = time.time()
    with urllib.request.urlopen(req, timeout=600) as resp:
        reply = json.loads(resp.read())
    reply["_wall_seconds"] = round(time.time() - started, 1)
    return reply

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    layout_files = sorted(LAYOUT.glob("*_1036.json"))
    only_rot = sys.argv[1] if len(sys.argv) > 1 else None
    per_page = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    page_filter = sys.argv[3] if len(sys.argv) > 3 else None
    for lf in layout_files:
        stem = lf.stem[: -len("_1036")]
        rot = stem.rsplit("_", 1)[1]             # orig / rot90cw / rot180 / rot90ccw
        if only_rot and rot != only_rot:
            continue
        page = stem.rsplit("_", 1)[0]
        if page_filter and page_filter not in page:
            continue
        reply = json.loads(lf.read_text(encoding="utf-8"))
        content = reply["choices"][0]["message"]["content"]
        boxes = blocks_of(content)
        # up to N text blocks (skip tiny slivers), plus the first table/equation
        texts = [b for b in boxes if b["label"] in ("text", "paragraph_title", "doc_title", "list_item", "footnote")
                 and (b["x2"] - b["x1"]) >= 50 and (b["y2"] - b["y1"]) >= 15]
        specials = [b for b in boxes if b["label"] in ("table", "equation", "equation_block")]
        picked = texts[:per_page] + specials[:1]
        for i, b in enumerate(picked):
            out = OUT / f"{page}_{rot}_b{i}_{b['label']}.json"
            if out.exists():
                continue
            crop = crop_png(PAGES / f"{page}_{rot}.png", b)
            if not crop:
                continue
            r = request_block(crop, PROMPTS.get(b["label"], PROMPTS["text"]))
            out.write_text(json.dumps({"box": b, **r}, ensure_ascii=False, indent=1), encoding="utf-8")
            text = r["choices"][0]["message"]["content"].replace("\n", "\\n")[:90]
            print(f"{page}_{rot}_b{i} [{b['label']}] -> {text}")

if __name__ == "__main__":
    main()
