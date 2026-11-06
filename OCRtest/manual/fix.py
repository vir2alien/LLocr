#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Ручной стенд FIX: читает конфиг fix.txt, поднимает/переиспользует
llama-server, отправляет кроп + кандидата и печатает вердикт OK/FIX/REVIEW.

usage: py fix.py [--имя-конфига.txt | config.txt] [--restart]
Части запроса как в FixModel для qwen: типовой промпт, изображение, кандидат.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stand import chat, ensure_server, image_data_url, parse_args, report, text_part, image_part


def main():
    cfg, restart = parse_args("fix.txt")
    ensure_server(cfg, restart)

    candidate = cfg.get("candidate")
    content = []
    prompt = cfg.get("prompt")
    if prompt:
        content.append(text_part(prompt))
    content.append(image_part(image_data_url(cfg)))
    if candidate:
        content.append(text_part("OCR candidate:\n<CANDIDATE>\n" + candidate + "\n</CANDIDATE>"))

    messages = []
    system = cfg.get("system")
    if system:
        messages.append({"role": "system", "content": system})
    messages.append({"role": "user", "content": content})

    report("fix", chat(cfg, messages, "fix"), extra="candidate: " + (candidate[:60] or "(пусто)"))


if __name__ == "__main__":
    main()
