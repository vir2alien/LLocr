#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Общий движок ручных стендов READ/FIX: разбор конфига, жизнь llama-server,
запрос к /v1/chat/completions.

Конфиг — текст, один параметр на строку: «ключ = значение». Значение можно
продолжить на следующих строках с отступом (склеиваются переводом строки).
Строки с # и пустые пропускаются. Ключи регистронезависимые, должны начинаться
с начала строки. Пути, заданные относительно, отсчитываются от папки конфига.
"""

import base64
import hashlib
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(HERE, ".server_state.txt")


class Config:
    def __init__(self, path):
        self.path = path
        self.dir = os.path.dirname(os.path.abspath(path))
        self.raw = {}
        current = None
        with open(path, encoding="utf-8-sig") as handle:
            for line in handle:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                if line[:1] in (" ", "\t") and current is not None:
                    self.raw[current].append(stripped)
                    continue
                if "=" not in line:
                    print(f"конфиг: строка без ключа пропущена: {stripped[:60]}")
                    continue
                key, _, value = line.partition("=")
                current = key.strip().lower()
                self.raw[current] = [value.strip()] if value.strip() else []

    def get(self, key, default=""):
        return "\n".join(self.raw.get(key.lower(), [])) or default

    def lines(self, key):
        return self.raw.get(key.lower(), [])

    def path_of(self, key):
        value = self.get(key)
        if not value:
            return ""
        return value if os.path.isabs(value) else os.path.join(self.dir, value)


def parse_args(script_default):
    """usage: py read.py [config.txt] [--restart]"""
    config_path = os.path.join(HERE, script_default)
    restart = False
    for arg in sys.argv[1:]:
        if arg == "--restart":
            restart = True
        else:
            config_path = arg if os.path.isabs(arg) else os.path.join(os.getcwd(), arg)
    if not os.path.exists(config_path):
        sys.exit(f"конфиг не найден: {config_path}")
    return Config(config_path), restart


def launch_hash(cfg):
    basis = "\n".join([
        cfg.get("runtime"), cfg.get("model"), cfg.get("mmproj"),
        cfg.get("host", "127.0.0.1"), cfg.get("port", "8123"),
        "\n".join(cfg.lines("launch")),
    ])
    return hashlib.sha1(basis.encode("utf-8")).hexdigest()[:12]


def server_args(cfg):
    args = [cfg.path_of("runtime"), "-m", cfg.path_of("model")]
    if cfg.get("mmproj"):
        args += ["--mmproj", cfg.path_of("mmproj")]
    args += ["--host", cfg.get("host", "127.0.0.1"), "--port", cfg.get("port", "8123")]
    for line in cfg.lines("launch"):
        args += line.split()
    return args


def listener_pid(port):
    # netstat пишет в OEM-кодировке: разбираем байты, нас интересуют только
    # ASCII-колонки.
    out = subprocess.run(["netstat", "-ano"], capture_output=True).stdout.decode("utf-8", "replace")
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[3] == "LISTENING" and parts[1].endswith(f":{port}"):
            return int(parts[4])
    return None


def healthy(host, port):
    try:
        with urllib.request.urlopen(f"http://{host}:{port}/health", timeout=4) as resp:
            return json.load(resp).get("status") == "ok"
    except urllib.error.HTTPError as error:
        try:
            return json.loads(error.read().decode("utf-8", "replace")).get("status") == "ok"
        except Exception:
            return False
    except Exception:
        return False


def read_state():
    if not os.path.exists(STATE_FILE):
        return None, None
    with open(STATE_FILE, encoding="ascii") as handle:
        pid, _, stamp = handle.read().strip().partition(" ")
        return (int(pid), stamp) if pid.isdigit() else (None, None)


def write_state(pid, stamp):
    with open(STATE_FILE, "w", encoding="ascii") as handle:
        handle.write(f"{pid} {stamp}\n")


def stop_server(pid, port):
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    for _ in range(30):
        if listener_pid(port) is None:
            return
        time.sleep(0.5)
    print(f"предупреждение: процесс {pid} ещё слушает порт {port}")


def ensure_server(cfg, restart):
    host = cfg.get("host", "127.0.0.1")
    port = int(cfg.get("port", "8123"))
    want = launch_hash(cfg)
    pid, saved = read_state()
    listening = listener_pid(port)

    if listening is not None:
        if not healthy(host, port):
            if pid == listening:
                print(f"на порту {port} завис сервер стенда (pid {pid}) — пересап")
                stop_server(pid, port)
                listening = None
            else:
                sys.exit(f"порт {port} занят чужим процессом (pid {listening}). "
                         f"Остановите его сами или смените порт в конфиге.")
        elif pid == listening and (restart or saved != want):
            reason = "флаг --restart" if restart else "изменились параметры запуска"
            print(f"пересап сервера ({reason}): pid {pid}")
            stop_server(pid, port)
            listening = None
        elif restart or saved != want:
            print(f"внимание: сервер на порту {port} (pid {listening}) запущен вне стенда, "
                  f"использую его как есть — новые параметры запуска не применены")
        else:
            print(f"сервер уже работает: pid {listening}, порт {port} — переиспользую")
            return

    args = server_args(cfg)
    missing = [path for path in (args[0], args[2]) if not os.path.exists(path)]
    if missing:
        sys.exit("не найдено: " + ", ".join(missing))
    print("запуск: " + " ".join(args))
    with open(os.path.join(HERE, "server.log"), "ab") as log:
        proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, cwd=HERE)
    write_state(proc.pid, want)

    limit = float(cfg.get("wait", "300"))
    started = time.time()
    while time.time() - started < limit:
        if proc.poll() is not None:
            sys.exit(f"llama-server завершился с кодом {proc.returncode}, см. server.log")
        if healthy(host, port):
            print(f"сервер готов за {time.time() - started:.0f} с (pid {proc.pid})")
            return
        time.sleep(1.0)
    sys.exit(f"сервер не поднялся за {limit:.0f} с, см. server.log")


def chat(cfg, messages):
    body = {
        "model": "manual",
        "messages": messages,
        "stream": False,
        "cache_prompt": True,
        "max_tokens": int(cfg.get("max_tokens", "4096")),
    }
    for line in cfg.lines("params"):
        name, _, value = line.partition("=")
        body[name.strip()] = json.loads(value.strip())
    payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        f"http://{cfg.get('host', '127.0.0.1')}:{cfg.get('port', '8123')}/v1/chat/completions",
        data=payload, headers={"Content-Type": "application/json"})
    started = time.time()
    with urllib.request.urlopen(request, timeout=float(cfg.get("timeout", "600"))) as resp:
        answer = json.load(resp)
    answer["seconds"] = round(time.time() - started, 1)
    return answer


def image_data_url(cfg):
    image = cfg.path_of("image")
    if not image or not os.path.exists(image):
        sys.exit(f"кроп не найден: {image or '(ключ image пуст)'}")
    with open(image, "rb") as handle:
        return "data:image/png;base64," + base64.b64encode(handle.read()).decode()


def text_part(text):
    return {"type": "text", "text": text}


def image_part(data_url):
    return {"type": "image_url", "image_url": {"url": data_url}}


def report(title, answer, extra=""):
    choice = answer["choices"][0]
    head = (f"[{title}] {answer.get('seconds', '?')} с, finish={choice.get('finish_reason')}, "
            f"usage={answer.get('usage', {})}")
    if extra:
        head += f"\n{extra}"
    text = choice["message"]["content"]
    print(head)
    print("─" * 72)
    print(text)
    print("─" * 72)
    out = os.path.join(HERE, f"last_{title}.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(head + "\n\n" + text + "\n")
    print(f"полный ответ: {out}")
