#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Ручной стенд READ: читает конфиг read.txt, поднимает/переиспользует
llama-server, отправляет кроп и печатает расшифровку.

usage: py read.py [config.txt] [--restart]
Порядок частей запроса как в приложении для qwen: текст перед изображением.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stand import chat, ensure_server, image_data_url, parse_args, report, text_part, image_part


def main():
    cfg, restart = parse_args("read.txt")
    ensure_server(cfg, restart)

    content = []
    prompt = cfg.get("prompt")
    if prompt:
        content.append(text_part(prompt))
    content.append(image_part(image_data_url(cfg)))

    messages = []
    system = cfg.get("system")
    if system:
        messages.append({"role": "system", "content": system})
    messages.append({"role": "user", "content": content})

    report("read", chat(cfg, messages, "read"))


if __name__ == "__main__":
    main()
