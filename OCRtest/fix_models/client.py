#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HTTP-клиент llama-server (/v1/chat/completions) для fix-стенда."""

import base64
import json
import time
import urllib.error
import urllib.request


class ServerError(Exception):
    def __init__(self, kind, detail=""):
        super().__init__(f"{kind}: {detail}")
        self.kind = kind
        self.detail = detail


class ChatClient:
    def __init__(self, base_url, timeout_s=300.0):
        self.base_url = base_url.rstrip("/")
        self.timeout_s = timeout_s

    def _post(self, path, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(
            self.base_url + path, data=body,
            headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_s) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")[:4000]
            raise ServerError(f"http_{e.code}", detail) from None
        except urllib.error.URLError as e:
            raise ServerError("network", str(e.reason)) from None
        except (TimeoutError, OSError) as e:
            raise ServerError("timeout", str(e)) from None
        except json.JSONDecodeError as e:
            raise ServerError("bad_json", str(e)) from None

    def health(self):
        req = urllib.request.Request(self.base_url + "/health")
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def models(self):
        req = urllib.request.Request(self.base_url + "/v1/models")
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def chat(self, payload):
        started = time.perf_counter()
        data = self._post("/v1/chat/completions", payload)
        data["_latency_s"] = round(time.perf_counter() - started, 3)
        return data


def data_url(png_bytes: bytes) -> str:
    return "data:image/png;base64," + base64.b64encode(png_bytes).decode("ascii")


def reply_text(response: dict) -> str:
    choices = response.get("choices") or []
    if not choices:
        return ""
    return (choices[0].get("message") or {}).get("content") or ""
