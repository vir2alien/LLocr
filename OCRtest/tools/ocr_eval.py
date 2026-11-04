#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OCR-оценка кандидатов LLocr на корпусе OCR_TEST (ocr_models/materials).

Каждому кандидату — его родной промпт (как модель обучалась), кроп блока из
box_crops (те же, что видели baseline-модели), ответ кладётся в project.json
в формате приложения, скоринг — функциями compare.py против gt.json.

    py ocr_eval.py --model ovisocr3            # один
    py ocr_eval.py --all                       # все по очереди
    py ocr_eval.py --score                     # только скоринг
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MATERIALS = HERE.parent / "ocr_models" / "materials"
FIX = HERE.parent / "fix_models"
sys.path.insert(0, str(FIX))
sys.path.insert(0, str(MATERIALS))

from client import ChatClient, ServerError, data_url, reply_text  # noqa: E402
import compare  # noqa: E402

MODELS_DIR = HERE
SERVER_BIN = "C:/Users/gladskih/AppData/Roaming/llocr/LLM OCR/runtime/llama.cpp-b11724-vulkan-win-x64/llama-server.exe"
D = "C:/Users/gladskih/Documents/fix-eval-models"

OVIS_PLAIN = open("/tmp/ovis_plain_ocr.txt", encoding="utf-8").read().strip() if Path("/tmp/ovis_plain_ocr.txt").exists() else "Read all the text in the image. Directly output the content."
OVIS_REGION = (Path("/tmp/ovis_region_ocr.txt").read_text(encoding="utf-8").strip().replace("{target_region}", "the whole image") if Path("/tmp/ovis_region_ocr.txt").exists() else "")
OVIS_FORMULA = Path("/tmp/ovis_formula_recognition.txt").read_text(encoding="utf-8").strip() if Path("/tmp/ovis_formula_recognition.txt").exists() else ""
OVIS_MD = Path("/tmp/ovis_document_markdown.txt").read_text(encoding="utf-8").strip() if Path("/tmp/ovis_document_markdown.txt").exists() else ""
PEPPER = ("You are an advanced hybrid OCR engine capable of processing multilingual text mixed with mathematical notation. "
          "Your goal is to transcribe the content with high fidelity.Strict Rules: 1. Multilingual Precision: Transcribe text exactly "
          "as it appears in the original language. Do not translate, summarize, or correct original spelling errors. 2. Math Formatting: "
          "Identify all mathematical expressions and convert them into LaTeX. 3. Use single dollar signs ($x$) for inline math (formulas "
          "within a sentence). 4. Use double dollar signs ($$x$$) for display math (standalone formulas on their own lines). "
          "5. Layout & Structure: Use Markdown to preserve the visual structure (headers, paragraphs, lists). 6. Output Only: Output the "
          "transcribed text directly without any conversational filler.")

HUN_TEXT = "提取图中的文字。"
HUN_FORMULA = "识别图片中的公式，用LaTeX格式表示。"
HUN_TABLE = "把图中的表格解析为HTML。"

MKY_TEXT = "Please output the text content from the image."
MKY_FORMULA = "Please write out the expression of the formula in the image using LaTeX format."
MKY_TABLE = "Please extract the table from the image and represent it in OTSL format."

DOTS_OCR = "Extract the text content from this image."

CANDIDATES = {
    "xiaomi-ocr-0": {
        "model": f"{D}/xiaomi-ocr-0/Xiaomi-OCR-0.Q6_K.gguf",
        "mmproj": f"{D}/xiaomi-ocr-0/Xiaomi-OCR-0.mmproj-bf16.gguf",
        "prompts": {"text": "OCR:", "equation": "Formula Recognition:", "table": "Table Recognition:"},
        "server_extra": ["--image-min-tokens", "1024"],
    },
    "ovisocr3": {
        "model": f"{D}/ovisocr3/OvisOCR3-Q8_0.gguf",
        "mmproj": f"{D}/ovisocr3/mmproj-OvisOCR3-F16.gguf",
        "prompts": {"text": OVIS_PLAIN, "equation": OVIS_FORMULA, "table": OVIS_MD},
        "server_extra": ["--image-min-tokens", "1024"],
    },
    "hunyuanocr-1.5": {
        "model": f"{D}/hunyuanocr-15/HunyuanOCR.Q8_0.gguf",
        "mmproj": f"{D}/hunyuanocr-15/HunyuanOCR.mmproj-f16.gguf",
        "prompts": {"text": HUN_TEXT, "equation": HUN_FORMULA, "table": HUN_TABLE},
        "rep": 1.08,
    },
    "monkeyocrv2-b": {
        "model": f"{D}/monkeyocrv2-b/MonkeyOCRv2-B.q8_0.gguf",
        "mmproj": f"{D}/monkeyocrv2-b/MonkeyOCRv2-B.mmproj-f16.gguf",
        "prompts": {"text": MKY_TEXT, "equation": MKY_FORMULA, "table": MKY_TABLE},
    },
    "dots-mocr": {
        "model": f"{D}/dots-mocr/dotsmocr-1.8b-q8_0.gguf",
        "mmproj": f"{D}/dots-mocr/mmproj-dotsmocr-bf16.gguf",
        "prompts": {"text": DOTS_OCR, "equation": DOTS_OCR, "table": DOTS_OCR},
    },
    "deepseek-ocr-2": {
        "model": f"{D}/deepseek-ocr-2/DeepSeek-OCR-2-IQ4_XS.gguf",
        "mmproj": f"{D}/deepseek-ocr-2/mmproj-deepseek-ocr-2-bf16.gguf",
        "prompts": {"text": "Free OCR.", "equation": "Free OCR.", "table": "Free OCR."},
    },
    "pepperocr-vl": {
        "model": f"{D}/pepperocr-vl/PepperOCR-VL.Q8_0.gguf",
        "mmproj": f"{D}/pepperocr-vl/PepperOCR-VL.mmproj-f16.gguf",
        "prompts": {"text": PEPPER, "equation": PEPPER, "table": PEPPER},
        "rep": 1.05,
    },
}

# Подвыборка: все категории, ~44 блока из 195
SUBSET = [
    (0, 0), (0, 1), (0, 2), (0, 3), (0, 4),
    (1, 0), (1, 3), (1, 4), (1, 6), (1, 7), (1, 8), (1, 9), (1, 10), (1, 11), (1, 12), (1, 13), (1, 14), (1, 15),
    (2, 0), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5), (2, 7), (2, 9), (2, 11), (2, 13),
    (3, 0), (3, 3), (3, 5),
    (4, 0), (4, 4), (4, 5),
    (5, 2), (5, 3), (5, 4), (5, 5),
    (6, 3),
    (7, 2),
    (8, 5), (8, 7),
    (9, 0), (9, 3), (9, 11),
]


def prompt_for(cand, pi, bi, gt):
    label = gt["label"].lower()
    p = cand["prompts"]
    if label in ("table",) or "table" in label:
        return p["table"]
    if label in ("equation", "formula", "equation_block") or "formula" in label:
        return p["equation"]
    return p["text"]


def run_model(name, cand, gt):
    out_dir = HERE / "results" / name
    out_dir.mkdir(parents=True, exist_ok=True)
    args = [
        SERVER_BIN, "--model", cand["model"], "--mmproj", cand["mmproj"],
        "--alias", "ocr-eval", "--host", "127.0.0.1", "--port", "8129",
        "--parallel", "1", "--no-context-shift", "--no-warmup",
        "--n-gpu-layers", "99", "--flash-attn", "on", "--ctx-size", "8192",
        "--jinja",
    ] + list(cand.get("server_extra", []))
    log = open(out_dir / "server.log", "w", encoding="utf-8")
    proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
    client = ChatClient("http://127.0.0.1:8129", timeout_s=600)
    try:
        for _ in range(240):
            if proc.poll() is not None:
                print(f"  !! сервер упал ({name}), хвост лога:")
                log.flush()
                print("".join(log.readlines()[-12:]))
                return False
            try:
                client.health()
                break
            except Exception:
                time.sleep(1)
        for pi, bi in SUBSET:
            cache = out_dir / f"p{pi:02d}b{bi:02d}.json"
            if cache.exists():
                continue
            box = gt["pages"][pi]["boxes"][bi]
            png = (MATERIALS / box["crop"]).read_bytes()
            prompt = prompt_for(cand, pi, bi, box)
            payload = {
                "model": "ocr-eval",
                "messages": [{"role": "user", "content": [
                    {"type": "image_url", "image_url": {"url": data_url(png)}},
                    {"type": "text", "text": prompt},
                ]}],
                "chat_template_kwargs": {"enable_thinking": False},
                "temperature": 0.0,
                "repeat_penalty": cand.get("rep", 1.0),
                "max_tokens": 4096,
                "stream": False,
            }
            try:
                data = client.chat(payload)
                text = reply_text(data).strip()
                rec = {"pi": pi, "bi": bi, "latency_s": data.get("_latency_s"),
                       "tokens": (data.get("usage") or {}).get("completion_tokens"), "text": text}
            except ServerError as e:
                rec = {"pi": pi, "bi": bi, "error": str(e)[:300], "text": ""}
            cache.write_text(json.dumps(rec, ensure_ascii=False), encoding="utf-8")
            print(f"  [{name}] p{pi}b{bi} {rec.get('latency_s', '?')}s {len(rec['text'])} chars")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except Exception:
                proc.kill()
        log.close()
    return True


def build_project(name, gt):
    base = json.loads((MATERIALS / "projects" / "OCR_TEST" / "project.json").read_text(encoding="utf-8"))
    out_dir = MATERIALS / "projects" / f"OCR_TEST_{name}"
    out_dir.mkdir(exist_ok=True)
    res_dir = HERE / "results" / name
    for page in base["pages"]:
        for box in page["boxes"]:
            pi, bi = page["sourcePageIndex"], box_index(page, box)
            cache = res_dir / f"p{pi:02d}b{bi:02d}.json"
            if cache.exists():
                box["text"] = json.loads(cache.read_text(encoding="utf-8"))["text"]
            else:
                box["text"] = ""
    (out_dir / "project.json").write_text(json.dumps(base, ensure_ascii=False), encoding="utf-8")
    print("project written:", out_dir)


def box_index(page, box):
    return page["boxes"].index(box)


def score():
    gt = json.loads((MATERIALS / "gt.json").read_text(encoding="utf-8"))
    base = json.loads((MATERIALS / "results" / "baseline_boxes.json").read_text(encoding="utf-8"))["models"]
    rows = {}
    for mname, mdata in base.items():
        per = {}
        for pi, bi in SUBSET:
            k = f"p{pi}_b{bi}"
            if k in mdata:
                box = gt["pages"][pi]["boxes"][bi]
                per[(pi, bi)] = (box["group"], mdata[k]["cer"])
        rows["[base] " + mname] = per
    for name in CANDIDATES:
        per = {}
        for pi, bi in SUBSET:
            cache = HERE / "results" / name / f"p{pi:02d}b{bi:02d}.json"
            if not cache.exists():
                continue
            rec = json.loads(cache.read_text(encoding="utf-8"))
            box = gt["pages"][pi]["boxes"][bi]
            gtxt = box["gt_text"]
            is_eq = box["group"] == "equation"
            if box["group"] == "table":
                from rapidfuzz import fuzz
                gcells = compare.gt_table_cells(gtxt)
                rows_ = compare.parse_table(rec["text"], name)
                cells = [c for r in (rows_ or []) for c in r]
                hit = sum(1 for r in gcells for c in r
                          if any(fuzz.partial_ratio(compare.strip_note(c.strip()), h) >= 0.85 for h in cells)) if cells else 0
                total = sum(len(r) for r in gcells)
                per[(pi, bi)] = ("table", hit / total if total else 0.0)
                continue
            ngt = compare.norm_eq(gtxt) if is_eq else compare.norm_text(gtxt)
            nhyp = compare.norm_eq(rec["text"]) if is_eq else compare.norm_text(rec["text"])
            per[(pi, bi)] = (box["group"], compare.cer(ngt, nhyp))
        rows[name] = per
    return rows


def report(rows):
    from collections import defaultdict
    cats_all = ["en_text", "ru_text", "ru_math_text", "equation", "table", "service"]
    print(f"\n{'model':18s} " + " ".join(f"{c:>14s}" for c in cats_all) + "      ALL")
    for name, per in rows.items():
        cats = defaultdict(list)
        for (pi, bi), (g, v) in per.items():
            cats[g].append(v)
        def avg(g):
            v = cats.get(g) or []
            return sum(v) / len(v) if v else float("nan")
        allc = [v for g in cats if g != "table" for v in cats[g]]
        cells = " ".join(f"{avg(c)*100:13.1f}%" for c in cats_all)
        all_s = f"{sum(allc)/len(allc)*100:.1f}%" if allc else "n/a"
        print(f"{name:18s} {cells}  {all_s}  (n={len(per)})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--score", action="store_true")
    args = ap.parse_args()
    gt = json.loads((MATERIALS / "gt.json").read_text(encoding="utf-8"))
    if args.score:
        report(score())
        return
    names = list(CANDIDATES) if args.all else args.model
    for name in names:
        print(f"== {name}")
        ok = run_model(name, CANDIDATES[name], gt)
        if ok:
            build_project(name, gt)
    report(score())


if __name__ == "__main__":
    main()
