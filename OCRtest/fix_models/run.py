#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Стенд fix-моделей: гоняет варианты запроса по корпусу и считает, что стало с блоком.

    python3 run.py --variant v0_app --variant img_2x
    python3 run.py --all --base-url http://127.0.0.1:8123
    python3 run.py --all                # сервер поднимается сам (configs/qwen3.5-4b.json)

Запрос собирается так же, как в приложении (FixModel::buildRequestBody +
VisionChatModel::buildRoot): systemPrompt и typePrompt из профиля запросов
(resources/profiles/requests/fix-requests.json), кандидат в <CANDIDATE>,
картинка кропа, параметры запроса из профиля модели, enable_thinking = false.
"""

import argparse
import hashlib
import io
import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import gate  # noqa: E402
import metrics  # noqa: E402
import variants as variants_mod  # noqa: E402
from client import ChatClient, ServerError, data_url, reply_text  # noqa: E402

FIX_REQUESTS = REPO / "resources" / "profiles" / "requests" / "fix-requests.json"
DOCUMENT_LANGUAGE = "Russian"  # VerificationQueueController::kDocumentLanguage


# ------------------------------------------------------------------ профили

def load_model_profile(path):
    profile = json.loads(Path(path).read_text(encoding="utf-8"))
    role = profile["roles"]["fix"]
    params = []
    for item in role.get("request", []):
        params.append((item["name"], item.get("value")))
    return {
        "id": profile["id"],
        "alias": role.get("alias", profile["id"]),
        "max_output": role.get("maxOutput", 2048),
        "max_output_by_block": role.get("maxOutputByBlock", {}),
        "params": params,
        "launch": role.get("launch", []),
    }


def request_params(model, block_label, overrides):
    """Параметры запроса ровно как их собирает приложение."""
    params = {}
    for name, value in model["params"]:
        params[name] = value
    budget = model["max_output_by_block"].get(block_label, model["max_output"])
    params["max_tokens"] = budget
    params.update(overrides or {})
    return params


def launch_arguments(config, model, server_overrides):
    """Аргументы llama-server: приложение собирает их из launch-профиля."""
    args = [config["server_bin"]]
    args += [
        "--model", config["model"],
        "--mmproj", config["mmproj"],
        "--alias", config.get("alias", model["alias"]),
        "--host", config.get("host", "127.0.0.1"),
        "--port", str(config["port"]),
    ]
    for item in model["launch"]:
        name = item["name"]
        if name in ("parallel", "cache-reuse", "no-context-shift", "no-warmup", "n-gpu-layers",
                    "flash-attn", "ctx-size"):
            continue  # политика и платформа — из конфига сервера
        if "value" in item:
            args += [f"--{name}", str(item["value"])]
        else:
            args.append(f"--{name}")
    args += ["--parallel", str(config.get("parallel", 1))]
    args += ["--cache-reuse", str(config.get("cache_reuse", 0))]
    args.append("--no-context-shift")
    args.append("--no-warmup")
    args += ["--n-gpu-layers", str(config.get("n_gpu_layers", 99))]
    args += ["--flash-attn", config.get("flash_attn", "on")]
    args += ["--ctx-size", str(config.get("ctx_size", 16384))]
    for key, value in (server_overrides or {}).items():
        args += [f"--{key.replace('_', '-')}", str(value)]
    return args


# ------------------------------------------------------------------- картинки

def prepare_image(path, spec):
    from PIL import Image, ImageOps

    img = Image.open(path)
    img.load()
    scale = float(spec.get("scale", 1.0))
    if scale != 1.0:
        resample = {
            "bicubic": Image.BICUBIC,
            "lanczos": Image.LANCZOS,
            "nearest": Image.NEAREST,
        }[spec.get("resample", "bicubic")]
        img = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), resample)
    if spec.get("gray"):
        img = img.convert("L").convert("RGB")
    if spec.get("autocontrast"):
        img = ImageOps.autocontrast(img.convert("L")).convert("RGB")
    if spec.get("binarize"):
        gray = img.convert("L")
        threshold = int(spec.get("threshold", 160))
        img = gray.point(lambda v: 255 if v > threshold else 0, mode="L").convert("RGB")
    pad = int(spec.get("pad_px", 0))
    if pad:
        canvas = Image.new("RGB", (img.width + 2 * pad, img.height + 2 * pad), "white")
        canvas.paste(img, (pad, pad))
        img = canvas
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def page_image(block, max_width=1400):
    """Страница целиком ( downscaled ) — контекст документа."""
    from PIL import Image

    path = HERE / "corpus" / "_pages" / f"p{block['page']}.png"
    img = Image.open(path)
    img.load()
    if img.width > max_width:
        scale = max_width / img.width
        img = img.resize((max_width, max(1, round(img.height * scale))), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


# ---------------------------------------------------------------------- запрос

TRANSCRIBE_SYSTEM = (
    "You transcribe one block of a technical document. The image is the only source of truth. "
    "Write exactly what the image shows: the same characters, the same script, the same case, "
    "the same position of every subscript and superscript. For a formula, write the complete "
    "LaTeX with its tag. Do not correct anything, do not complete anything, do not add what is "
    "not on the image. Reply with the transcription only."
)
TRANSCRIBE_USER = "Transcribe the block from the image."
R_TRANSCRIBE_SYSTEM = TRANSCRIBE_SYSTEM


def build_payload(block, model, variant, image_png, system_prompt, type_prompt, extra_parts=None,
                  candidate=None, with_candidate=True):
    """Собирает тело запроса ровно как приложение (ADR 180/181)."""
    prompt_part = {"type": "text", "text": type_prompt}
    image_part = {"type": "image_url", "image_url": {"url": data_url(image_png)}}
    text = block["candidate_wire"] if candidate is None else candidate
    ocr_part = {"type": "text", "text": "OCR candidate:\n<CANDIDATE>\n" + text + "\n</CANDIDATE>"}

    if variant["order"] == "candidate_first":
        parts = [ocr_part, image_part, prompt_part] + list(extra_parts or [])
    elif variant["order"] == "image_last":
        parts = [prompt_part, ocr_part, image_part] + list(extra_parts or [])
    else:
        parts = [prompt_part, image_part] + list(extra_parts or []) + ([ocr_part] if with_candidate else [])
    if variant.get("extra_image") == "page":
        parts.insert(1, {"type": "image_url", "image_url": {"url": data_url(page_image(block))}})

    repetition = variant["repetition"]
    if repetition == "x2":
        parts = parts + parts
    elif repetition == "x2_verbose":
        parts = parts + [{"type": "text", "text": "Let me repeat that:"}] + parts
    elif repetition == "x3":
        parts = parts + [{"type": "text", "text": "Let me repeat that:"}] + parts + \
            [{"type": "text", "text": "Let me repeat that one more time:"}] + parts
    if repetition != "none" and not variant["repeat_image"]:
        # только текст дублируется, картинка остаётся одна
        text_parts = [p for p in parts if p["type"] == "text"]
        image_parts = [p for p in parts if p["type"] == "image_url"]
        parts = text_parts + image_parts

    messages = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": parts})

    payload = {
        "model": model["alias"],
        "messages": messages,
        "chat_template_kwargs": {"enable_thinking": False},
    }
    payload.update(request_params(model, block["label"], variant["request"]))
    payload.update(variant.get("extra_body") or {})
    return payload


# ------------------------------------------------------------------------ сервер

class Server:
    def __init__(self, config, model, overrides):
        self.args = launch_arguments(config, model, overrides)
        self.port = int(self.args[self.args.index("--port") + 1])
        self.proc = None
        self.log = None

    def start(self, timeout=180):
        # Порт должен быть свободен: иначе новый сервер молча не поднимется,
        # а запросы уйдут к старому (с другими параметрами) — такой прогон
        # выглядит как «параметр не влияет».
        if not wait_port_free(self.port, timeout=60):
            raise SystemExit(f"порт {self.port} занят — старый сервер не умер")
        self.log = open(HERE / "results" / "server.log", "w", encoding="utf-8")
        self.proc = subprocess.Popen(self.args, stdout=self.log, stderr=subprocess.STDOUT)
        client = ChatClient(f"http://127.0.0.1:{self.port}")
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise SystemExit(f"сервер упал, смотрите results/server.log: {' '.join(self.args)}")
            try:
                client.health()
                return client
            except Exception:
                time.sleep(1.0)
        raise SystemExit("сервер не поднялся за отсчёт")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        if self.log:
            self.log.close()
        wait_port_free(self.port, timeout=60)


def wait_port_free(port, timeout=60):
    import socket

    deadline = time.time() + timeout
    while time.time() < deadline:
        sock = socket.socket()
        sock.settimeout(1.0)
        listening = sock.connect_ex(("127.0.0.1", port)) == 0
        sock.close()
        if not listening:
            return True
        time.sleep(0.5)
    return False


# ------------------------------------------------------------------------- прогон

def cache_key(payload):
    blob = json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return hashlib.sha1(blob.encode("utf-8")).hexdigest()


def run_variant(client, corpus, model, variant, prompts, cache_dir, timeout_s, refresh=False):
    rows = []
    for block in corpus["blocks"]:
        image = prepare_image(HERE / block["crop"], variant["image"])
        hint = variant["system"]
        if hint:
            hint = hint.replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE)
        system_prompt = (prompts["system"] + "\n\n" + hint) if hint else prompts["system"]
        type_prompt = variant["type_prompt"] or prompts["type"][block["label"]]
        extra_parts = None
        candidate = None

        if variant["task"] == "transcribe":
            transcribe_system = R_TRANSCRIBE_SYSTEM + (("\n\n" + hint) if hint else "")
            payload = build_payload(block, model, variant, image, transcribe_system, TRANSCRIBE_USER,
                                    with_candidate=False)
        elif variant["task"] == "self_check":
            # Шаг 1: модель читает формулу сама (без кандидата, который можно скопировать).
            transcribe_system = R_TRANSCRIBE_SYSTEM + (("\n\n" + hint) if hint else "")
            read_payload = build_payload(block, model, variant, image, transcribe_system, TRANSCRIBE_USER,
                                        with_candidate=False)
            reading = cached_chat(client, read_payload, cache_dir, timeout_s, refresh)
            if reading is None:
                continue
            reading_text = reply_text(reading).strip()
            extra_parts = [{"type": "text", "text": "Your own reading of the same image:\n" + reading_text}]
            payload = build_payload(block, model, variant, image, system_prompt, type_prompt,
                                    extra_parts=extra_parts)
        else:
            payload = build_payload(block, model, variant, image, system_prompt, type_prompt)

        data = cached_chat(client, payload, cache_dir, timeout_s, refresh)
        if data is None:
            continue
        content = reply_text(data)

        if variant["task"] == "transcribe":
            corrected = content.strip()
            verdict = "Read"
            reason = ""
            kind, cer_before, cer_after = metrics.classify(block, corrected)
            rows.append({
                "variant": variant["name"], "uid": block["uid"], "label": block["label"],
                "verdict": verdict, "reason": reason, "kind": kind,
                "cer_before": cer_before, "cer_after": cer_after,
                "idx_before": metrics.index_hit_rate(block, block["candidate_wire"])[0],
                "idx_after": metrics.index_hit_rate(block, corrected)[0],
                "reply": content, "corrected": corrected,
                "latency_s": data.get("_latency_s"),
                "completion_tokens": (data.get("usage") or {}).get("completion_tokens"),
            })
            continue

        verdict, text, reason = gate.parse_reply(content, block["candidate_wire"], block["label"])
        corrected = text if verdict == gate.FIXED else block["candidate_wire"]
        kind, cer_before, cer_after = metrics.classify(block, corrected)
        idx_before, _, _ = metrics.index_hit_rate(block, block["candidate_wire"])
        idx_after, _, _ = metrics.index_hit_rate(block, corrected)
        rows.append({
            "variant": variant["name"],
            "uid": block["uid"],
            "label": block["label"],
            "verdict": verdict,
            "reason": reason,
            "kind": kind,
            "cer_before": cer_before,
            "cer_after": cer_after,
            "idx_before": idx_before,
            "idx_after": idx_after,
            "reply": content,
            "corrected": corrected if verdict == gate.FIXED else None,
            "latency_s": data.get("_latency_s"),
            "completion_tokens": (data.get("usage") or {}).get("completion_tokens"),
        })
    return rows


def cached_chat(client, payload, cache_dir, timeout_s, refresh):
    key = cache_key(payload)
    cache_file = cache_dir / f"{key}.json"
    if cache_file.exists() and not refresh:
        return json.loads(cache_file.read_text(encoding="utf-8"))
    client.timeout_s = timeout_s
    try:
        data = client.chat(payload)
    except ServerError as e:
        print(f"  !! {e}")
        return None
    cache_file.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    return data


def write_rows(path, rows):
    with Path(path).open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", action="append", default=[], help="имя варианта (можно повторять)")
    ap.add_argument("--all", action="store_true", help="все варианты из variants.py")
    ap.add_argument("--blocks", default="", help="uid через запятую (по умолчанию весь корпус)")
    ap.add_argument("--config", default=str(HERE / "configs" / "qwen3.5-4b.json"))
    ap.add_argument("--model", default="qwen3.5-4b", help="id профиля модели (resources/profiles/models/<id>.json)")
    ap.add_argument("--model-profile", default="", help="путь к профилю модели (по умолчанию resources/profiles/models/<--model>.json)")
    ap.add_argument("--base-url", default="", help="использовать уже запущенный сервер")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--refresh", action="store_true", help="не брать ответы из кеша")
    ap.add_argument("--out", default=str(HERE / "results"))
    ap.add_argument("--corpus", default=str(HERE / "corpus" / "corpus.json"))
    args = ap.parse_args()

    if args.all:
        selected = variants_mod.VARIANTS
    elif args.variant:
        selected = [variants_mod.by_name(v) for v in args.variant]
    else:
        selected = [variants_mod.by_name("v0_app")]

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir = out_dir / "cache"
    cache_dir.mkdir(parents=True, exist_ok=True)

    corpus = metrics.load_corpus(args.corpus)
    if args.blocks:
        wanted = {u.strip() for u in args.blocks.split(",") if u.strip()}
        corpus["blocks"] = [b for b in corpus["blocks"] if b["uid"] in wanted]

    profile_path = Path(args.model_profile) if args.model_profile else \
        REPO / "resources" / "profiles" / "models" / (args.model + ".json")
    model = load_model_profile(profile_path)
    requests = json.loads(FIX_REQUESTS.read_text(encoding="utf-8"))
    prompts = {
        "system": requests["systemPrompt"].replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE),
        "type": dict(requests["blockPrompts"]),
    }

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    config.setdefault("port", 8123)

    rows = []
    rows_file = out_dir / "rows.jsonl"
    if args.base_url:
        client = ChatClient(args.base_url, timeout_s=args.timeout)
        for variant in selected:
            print(f"-> {variant['name']}")
            rows += run_variant(client, corpus, model, variant, prompts, cache_dir, args.timeout, args.refresh)
    else:
        # серверные параметры требуют перезапуска: группируем варианты по ним
        groups = {}
        for variant in selected:
            key = json.dumps(variant["server"], sort_keys=True)
            groups.setdefault(key, []).append(variant)
        for key, group in groups.items():
            overrides = json.loads(key)
            print(f"-> сервер {overrides or '(профиль приложения)'}")
            server = Server(config, model, overrides)
            try:
                client = server.start()
            except SystemExit as e:
                print(f"   !! группа пропущена: {e}")
                continue
            try:
                for variant in group:
                    print(f"   {variant['name']}")
                    rows += run_variant(client, corpus, model, variant, prompts, cache_dir, args.timeout, args.refresh)
                    write_rows(rows_file, rows)
            finally:
                server.stop()

    write_rows(rows_file, rows)

    metrics.print_table(rows, title=f"{model['id']} · {len(corpus['blocks'])} блоков")
    print(f"\nответы: {rows_file}")


if __name__ == "__main__":
    main()
