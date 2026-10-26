#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка: принимает ли llama-server бюджет рассуждения per request.

Если да — режим мышления можно включать без перезапуска сервера, и все
варианты fix-запроса меряются на одном сервере. Если нет — нужен перезапуск
с `--reasoning-budget`.
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run as runner  # noqa: E402
import variants as variants_mod  # noqa: E402
from client import ChatClient, reply_text  # noqa: E402

CONFIG = HERE / "configs" / "gemma-4-12b-cpu.json"
BLOCK = HERE / "corpus" / "corpus_probe5.json"


def main():
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    model = runner.load_model_profile(
        HERE.parents[1] / "resources" / "profiles" / "models" / "gemma-4-12b.json")
    corpus = json.loads(BLOCK.read_text(encoding="utf-8"))
    block = next(b for b in corpus["blocks"] if b["uid"] == "p03b12")

    requests = json.loads(
        (HERE.parents[1] / "resources" / "profiles" / "requests" / "fix-requests.json"
         ).read_text(encoding="utf-8"))
    model["block_aliases"] = requests.get("blockAliases", {})
    model["shared_max_output_by_block"] = requests.get("maxOutputByBlock", {})
    system_prompt = requests["systemPrompt"].replace("{{LANGUAGE}}", "Russian")
    type_prompt = requests["blockPrompts"]["equation"]

    draft = ("/Users/gladskikh/Library/Application Support/llocr/LLM OCR/models/"
             "unsloth__gemma-4-12B-it-qat-GGUF/mtp-gemma-4-12B-it.gguf")
    server = runner.Server(config, model, {"device": "none", "model_draft": draft})
    try:
        client = server.start()
    except SystemExit as e:
        raise SystemExit(f"сервер не поднялся: {e}")
    try:
        image = runner.prepare_image(HERE / block["crop"],
                                     {"scale": 1.0, "resample": "bicubic", "pad_px": 0})
        variant = variants_mod.by_name("v0_app")
        payload = runner.build_payload(block, model, variant, image, system_prompt, type_prompt)
        for label, extra in (
            ("без мышления", {}),
            ("reasoning_budget_tokens=512 + enable_thinking",
             {"reasoning_budget_tokens": 512,
              "chat_template_kwargs": {"enable_thinking": True}}),
        ):
            probe = dict(payload)
            probe.update(extra)
            probe.pop("chat_template_kwargs", None)
            if extra:
                probe["chat_template_kwargs"] = extra["chat_template_kwargs"]
            client.timeout_s = 900
            data = client.chat(probe)
            choices = data.get("choices") or [{}]
            message = choices[0].get("message") or {}
            reasoning = message.get("reasoning_content") or ""
            print(f"--- {label}")
            print(f"    reasoning_content: {'ЕСТЬ, ' + str(len(reasoning)) + ' симв.' if reasoning else 'нет'}")
            print(f"    ответ: {reply_text(data)[:180]!r}")
            print(f"    usage: {data.get('usage')}")
    finally:
        server.stop()


if __name__ == "__main__":
    main()
