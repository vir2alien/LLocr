"""HTTP client for the llama.cpp /v1/systemone endpoint."""

import base64
import json
import time
import urllib.error
import urllib.request


class SystemOneError(Exception):
    def __init__(self, kind, detail=""):
        super().__init__(f"{kind}: {detail}")
        self.kind = kind
        self.detail = detail


class SystemOneClient:
    def __init__(self, base_url, timeout_s=180.0):
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
            raise SystemOneError(f"http_{e.code}", detail) from None
        except urllib.error.URLError as e:
            raise SystemOneError("network", str(e.reason)) from None
        except (TimeoutError, OSError) as e:
            # read-phase timeouts surface as a raw TimeoutError, not URLError
            raise SystemOneError("timeout", str(e)) from None
        except json.JSONDecodeError as e:
            raise SystemOneError("bad_json", str(e)) from None

    def health(self):
        req = urllib.request.Request(self.base_url + "/health")
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def ask_raw(self, state, image_pngs, questions):
        """POST one request; returns the raw response dict. One retry on 5xx/network.

        image_pngs is a list of PNG byte strings (one image per element).
        """
        if isinstance(image_pngs, (bytes, bytearray)):
            image_pngs = [image_pngs]
        payload = {
            "state": state,
            "images": ["data:image/png;base64," + base64.b64encode(png).decode("ascii")
                       for png in image_pngs],
            "questions": questions,
        }
        started = time.perf_counter()
        for attempt in (1, 2):
            try:
                data = self._post("/v1/systemone", payload)
                data["_latency_s"] = round(time.perf_counter() - started, 3)
                return data
            except SystemOneError as e:
                transient = e.kind in ("network", "timeout", "bad_json") or e.kind.startswith("http_5")
                if attempt == 2 or not transient:
                    e.latency_s = round(time.perf_counter() - started, 3)
                    raise
                time.sleep(2.0)
