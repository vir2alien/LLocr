#!/usr/bin/env python3
"""Capture the raw reply of an OCR model for one page.

The reply is what the model really said — before the app's parser reads it.
When a recognition looks wrong, the raw reply tells the two cases apart: the
model answered badly, or the app read a good answer badly.

The request is the one the app sends: the page rasterized at 300 DPI
(DocumentModel::kDpi), scaled so the longest edge fits the profile's
imageLongestEdge, PNG as a data URL, then the role's prompt from the model
profile. The reply is reproduced token for token.

    scripts/probe-ocr-reply.py --model chandra-ocr-2 --page 5 \\
        --pdf OCRtest/ocr_models/materials/pages/4_4_eq_hard2.pdf \\
        --server http://127.0.0.1:8080 --out tmp/reply.html
"""
import argparse
import base64
import io
import json
import pathlib
import sys
import time
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
PROFILE_DIR = REPO / "resources" / "profiles" / "models"


def load_profile(model_id):
    path = PROFILE_DIR / f"{model_id}.json"
    if not path.exists():
        sys.exit(f"No profile for {model_id}: {path} is missing")
    return json.loads(path.read_text())


def render_page(pdf_path, page_index, dpi):
    try:
        import fitz
    except ImportError:
        sys.exit("PyMuPDF is needed to rasterize the page: pip install pymupdf")
    from PIL import Image

    doc = fitz.open(pdf_path)
    pix = doc[page_index].get_pixmap(dpi=dpi)
    return Image.open(io.BytesIO(pix.tobytes("png"))).convert("RGB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True, help="model id, e.g. chandra-ocr-2")
    ap.add_argument("--role", default="pageOcr", help="role of the model (default: pageOcr)")
    ap.add_argument("--prompt", default="", help="id of the prompt (default: the role's only prompt)")
    ap.add_argument("--pdf", required=True, help="PDF to take the page from")
    ap.add_argument("--page", type=int, default=0, help="page index in the PDF (default: 0)")
    ap.add_argument("--server", default="http://127.0.0.1:8080", help="base URL of the server")
    ap.add_argument("--key", default="", help="API key of the server (sent as a bearer token)")
    ap.add_argument("--alias", default="", help="model name the server exposes (default: the role's alias)")
    ap.add_argument("--out", default="", help="file to write the reply to (default: stdout)")
    args = ap.parse_args()

    profile = load_profile(args.model)
    role = profile["roles"].get(args.role)
    if role is None:
        sys.exit(f"The model has no role {args.role}: {', '.join(profile['roles'])}")

    prompts = role.get("prompts") or []
    if args.prompt:
        chosen = next((p for p in prompts if p["id"] == args.prompt), None)
        if chosen is None:
            sys.exit(f"The role has no prompt {args.prompt}")
    elif len(prompts) == 1:
        chosen = prompts[0]
    else:
        ids = ", ".join(p["id"] for p in prompts)
        sys.exit(f"The role has several prompts — pick one with --prompt ({ids})")

    image = render_page(args.pdf, args.page, 300)
    longest = role.get("imageLongestEdge") or 0
    if longest and max(image.size) > longest:
        scale = longest / max(image.size)
        image = image.resize((round(image.width * scale), round(image.height * scale)))
    print(f"page image: {image.size}", file=sys.stderr)

    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    data_url = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()

    body = {
        "model": args.alias or role.get("alias") or args.model,
        "messages": [{"role": "user", "content": [{"type": "image_url", "image_url": {"url": data_url}},
                                                  {"type": "text", "text": chosen["text"]}]}],
    }
    # The sampling parameters are the role's own: the profile holds them, and
    # the app sends them as they are.
    for parameter in role.get("request") or []:
        body[parameter["name"]] = parameter["value"]
    body["max_tokens"] = role.get("maxOutput", 8192)

    headers = {"Content-Type": "application/json"}
    if args.key:
        headers["Authorization"] = "Bearer " + args.key
    request = urllib.request.Request(args.server.rstrip("/") + "/v1/chat/completions",
                                     data=json.dumps(body).encode(),
                                     headers=headers)
    started = time.time()
    with urllib.request.urlopen(request, timeout=3600) as response:
        payload = json.load(response)
    reply = payload["choices"][0]["message"]["content"]
    usage = payload.get("usage", {})
    print(f"reply: {len(reply)} chars in {time.time() - started:.1f}s, usage={usage}", file=sys.stderr)

    if args.out:
        pathlib.Path(args.out).write_text(reply)
        print(f"written to {args.out}", file=sys.stderr)
    else:
        print(reply)


if __name__ == "__main__":
    main()
