#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Постраничные пробы кандидатов: pageOcr (координаты), layout, rawPageOcr.

Для каждой пробы: сервер → страница-рендер → родной промпт → ответ + строки
лога llama-server про KV cache / compute buffer (для memory-блока профиля).
"""
import json
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MATERIALS = HERE.parent / "ocr_models" / "materials"
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "fix_models"))
from client import ChatClient, data_url, reply_text  # noqa: E402

SERVER_BIN = "C:/Users/gladskih/AppData/Roaming/llocr/LLM OCR/runtime/llama.cpp-b11724-vulkan-win-x64/llama-server.exe"
D = str(HERE)

MKY_TEXT = "Please output the text content from the image."
MKY_FORMULA = "Please write out the expression of the formula in the image using LaTeX format."
MKY_LAYOUT = "Please output the categories and coordinates of the document elements in reading order."
MKY_E2E = "List the document elements in reading order, including their categories, coordinates, and the content of each element."
HUN_TEXT = "提取图中的文字。"
HUN_PARSE = "提取文档图片中正文的所有信息用markdown格式表示，其中页眉、页脚部分忽略，表格用html格式表达，文档中公式用latex格式表示，按照阅读顺序组织进行解析。"
HUN_LAYOUT = "按照阅读顺序解析图中的版式信息。"
PEPPER = Path("/tmp/pepper_prompt.txt").read_text(encoding="utf-8").strip() if Path("/tmp/pepper_prompt.txt").exists() else ""
if not PEPPER:
    import ocr_eval
    PEPPER = ocr_eval.PEPPER

MODELS = {
    "monkeyocrv2-b": dict(model=f"{D}/monkeyocrv2-b/MonkeyOCRv2-B.q8_0.gguf",
                          mmproj=f"{D}/monkeyocrv2-b/MonkeyOCRv2-B.mmproj-f16.gguf", extra=[]),
    "hunyuanocr-1.5": dict(model=f"{D}/hunyuanocr-15/HunyuanOCR.Q8_0.gguf",
                           mmproj=f"{D}/hunyuanocr-15/HunyuanOCR.mmproj-f16.gguf", extra=[]),
    "pepperocr-vl": dict(model=f"{D}/pepperocr-vl/PepperOCR-VL.Q8_0.gguf",
                         mmproj=f"{D}/pepperocr-vl/PepperOCR-VL.mmproj-f16.gguf", extra=["--image-min-tokens", "1024"]),
}

PROBES = [
    ("monkeyocrv2-b", "e2e", MKY_E2E, "3_3_eq_medium.png"),
    ("monkeyocrv2-b", "e2e", MKY_E2E, "7_7_eq_table_hard.png"),
    ("monkeyocrv2-b", "layout", MKY_LAYOUT, "7_7_eq_table_hard.png"),
    ("monkeyocrv2-b", "text_page", MKY_TEXT, "3_3_eq_medium.png"),
    ("hunyuanocr-1.5", "doc_parse", HUN_PARSE, "3_3_eq_medium.png"),
    ("hunyuanocr-1.5", "structured", HUN_TEXT, "3_3_eq_medium.png"),
    ("hunyuanocr-1.5", "layout", HUN_LAYOUT, "7_7_eq_table_hard.png"),
    ("pepperocr-vl", "verbatim", PEPPER, "3_3_eq_medium.png"),
]


def mem_lines(log_path):
    out = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if re.search(r"KV (?:cache|buffer)|compute buffer|sched_reserve|KV self size|graph buffers", line, re.I):
            out.append(line.strip())
    return out


def main():
    out_dir = HERE / "results" / "pages"
    out_dir.mkdir(parents=True, exist_ok=True)
    for model, probe, prompt, page in PROBES:
        out = out_dir / f"{model}__{probe}__{page.replace('.png', '')}.json"
        if out.exists():
            print("skip", out.name)
            continue
        cfg = MODELS[model]
        png = (MATERIALS / "renders" / page).read_bytes()
        args = [SERVER_BIN, "--model", cfg["model"], "--mmproj", cfg["mmproj"],
                "--alias", "page-eval", "--host", "127.0.0.1", "--port", "8131",
                "--parallel", "1", "--no-context-shift", "--no-warmup",
                "--n-gpu-layers", "99", "--flash-attn", "on", "--ctx-size", "32768", "--jinja"] + cfg["extra"]
        log = open(out_dir / f"server__{model}.log", "w", encoding="utf-8")
        proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        client = ChatClient("http://127.0.0.1:8131", timeout_s=900)
        try:
            for _ in range(300):
                if proc.poll() is not None:
                    log.flush()
                    print("!! server died", model, "".join(log.readlines()[-10:]))
                    break
                try:
                    client.health()
                    break
                except Exception:
                    time.sleep(1)
            else:
                print("!! server timeout", model)
                continue
            payload = {"model": "page-eval",
                       "messages": [{"role": "user", "content": [
                           {"type": "image_url", "image_url": {"url": data_url(png)}},
                           {"type": "text", "text": prompt}]}],
                       "chat_template_kwargs": {"enable_thinking": False},
                       "temperature": 0.0, "repeat_penalty": 1.08 if model == "hunyuanocr-1.5" else 1.0,
                       "max_tokens": 8192, "stream": False}
            t0 = time.time()
            try:
                data = client.chat(payload)
                text = reply_text(data).strip()
                rec = {"model": model, "probe": probe, "page": page,
                       "latency_s": round(time.time() - t0, 1),
                       "tokens": (data.get("usage") or {}).get("completion_tokens"),
                       "text": text}
            except Exception as e:
                rec = {"model": model, "probe": probe, "page": page, "error": str(e)[:300], "text": ""}
            rec["memory_lines"] = mem_lines(out_dir / f"server__{model}.log")
            out.write_text(json.dumps(rec, ensure_ascii=False, indent=1), encoding="utf-8")
            print("done", out.name, rec.get("latency_s"), "s,", rec.get("tokens"), "tok")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=15)
                except Exception:
                    proc.kill()
            log.close()


if __name__ == "__main__":
    main()
