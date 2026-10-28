"""Run the MinerU layout request over the rotated corpus pages and store the
raw replies.

The request replicates the app byte for byte (LayoutController + mineru.json):
image squashed to 1036x1036, content parts [image_url, text], user text
"\nLayout Detection:", the profile's sampling parameters, --special on the
server so the <|box_start|> markers survive.
"""
import base64
import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).parent
PAGES = ROOT / "pages"
OUT = ROOT / "replies" / "layout"
URL = "http://127.0.0.1:8934/v1/chat/completions"

SYSTEM = "You are a helpful assistant."
PROMPT = "\nLayout Detection:"
PARAMS = {
    "temperature": 0.0,
    "top_p": 0.01,
    "repeat_penalty": 1.0,
    "presence_penalty": 0.0,
    "frequency_penalty": 0.0,
    "cache_prompt": True,
}
MAX_TOKENS = 4096

def request_layout(png: Path) -> dict:
    data = base64.b64encode(png.read_bytes()).decode()
    body = {
        "messages": [
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": [
                {"type": "image_url", "image_url": {"url": f"data:image/png;base64,{data}"}},
                {"type": "text", "text": PROMPT},
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
    names = sorted(p.stem for p in PAGES.glob("*_1036.png"))
    only = sys.argv[1] if len(sys.argv) > 1 else None
    for name in names:
        if only and only not in name:
            continue
        out = OUT / f"{name}.json"
        if out.exists():
            print("skip", name)
            continue
        reply = request_layout(PAGES / f"{name}.png")
        out.write_text(json.dumps(reply, ensure_ascii=False, indent=1), encoding="utf-8")
        content = reply["choices"][0]["message"]["content"]
        n_lines = content.count("<|box_start|>")
        print(f"{name}: {n_lines} boxes, {reply['_wall_seconds']}s")

if __name__ == "__main__":
    main()
