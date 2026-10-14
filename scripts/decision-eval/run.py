#!/usr/bin/env python3
"""Decision-model evaluation harness (/v1/systemone: clef-flash and peers).

Stages:
  probe  — start the server, pin the exact response shapes for noul/choice/score
  1      — stratified block subsample x all variants
  2      — top-2 variants over the full corpus + image-resolution axis
  report — re-render the report from collected rows

Usage:
  ./run.py --stage all
  ./run.py --stage probe --limit 5
  ./run.py --stage report
"""

import argparse
import datetime as dt
import hashlib
import io
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import corpus
import corruption
import metrics
import variants as V
from client import SystemOneClient, SystemOneError

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent
STAGE1_CAPS = {"text": 15, "equation": 10}
STAGE2_RES_CAPS = {"text": 8, "equation": 8}
SEVERITIES = ("subtle", "moderate", "gross")


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--config", default="configs/clef-flash.json")
    ap.add_argument("--corpus", default=None, help="path to .llocr (default: OCRtest/TEST.llocr)")
    ap.add_argument("--work", default=None, help="work dir (default: <corpus dir>/work)")
    ap.add_argument("--labels", default=None, help="labels.json (default: <corpus dir>/labels.json)")
    ap.add_argument("--stage", default="all",
                    choices=["probe", "1", "2", "all", "report", "dry"])
    ap.add_argument("--variants", default=None, help="comma-separated variant ids to run")
    ap.add_argument("--block-types", default=None,
                    help="restrict cases to these block types (comma-separated)")
    ap.add_argument("--full", action="store_true",
                    help="stage 1 runs over the full corpus instead of the subsample")
    ap.add_argument("--limit", type=int, default=0, help="limit cases per variant (debug)")
    ap.add_argument("--base-url", default=None, help="reuse a running server, skip launch")
    ap.add_argument("--timeout", type=float, default=180.0, help="per-request timeout, s")
    ap.add_argument("--concurrency", type=int, default=1, help="parallel HTTP requests")
    ap.add_argument("--server-parallel", type=int, default=0,
                    help="llama-server slots (0 = config value); per-slot ctx stays ctx_size")
    ap.add_argument("--keep-server", action="store_true")
    return ap.parse_args()


def load_config(path: Path) -> dict:
    cfg = json.loads(path.read_text(encoding="utf-8"))
    cfg["profile_path"] = (REPO_ROOT / cfg.get("profile", "resources/profiles/models/clef-flash.json")).resolve()
    return cfg


def guard_no_conflicting_processes():
    try:
        out = subprocess.run(["pgrep", "-fl", "llama-server|LLM OCR.app"],
                             capture_output=True, text=True).stdout.strip()
    except FileNotFoundError:
        return
    if out and os.environ.get("LLOCR_EVAL_FORCE") != "1":
        sys.exit(f"Refusing to start: running processes conflict over the GPU:\n{out}\n"
                 "(close the app / stop the server, or set LLOCR_EVAL_FORCE=1)")


def find_free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Server:
    def __init__(self, cfg, work: Path, port: int):
        self.cfg, self.port = cfg, port
        self.log_path = work / "server.log"
        self.proc = None

    def start(self):
        parallel = int(self.cfg.get("parallel", 1))
        # llama-server splits --ctx-size across slots; keep the per-slot window
        # from the config fixed whatever the slot count is.
        ctx_total = int(self.cfg.get("ctx_size", 8192)) * parallel
        extra = list(self.cfg.get("extra_args", []))
        if parallel > 1:
            # a "--parallel 1" policy default from the app must not override the
            # slot count — the last flag occurrence wins
            extra = [a for i, a in enumerate(extra)
                     if not (a == "--parallel" or (i > 0 and extra[i - 1] == "--parallel"
                                                   and a.isdigit()))]
        args = [
            self.cfg["server_bin"],
            "--model", self.cfg["model"],
            "--mmproj", self.cfg["mmproj"],
            "--alias", self.cfg.get("alias", "llocr-decision"),
            "--host", "127.0.0.1",
            "--port", str(self.port),
            "--ctx-size", str(ctx_total),
            "--parallel", str(parallel),
        ] + extra
        self.log = open(self.log_path, "ab")
        self.log.write(f"\n===== {dt.datetime.now()} parallel={parallel} ctx={ctx_total}\n".encode())
        self.log.flush()
        print("starting:", " ".join(args[:8]), "…")
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        base = f"http://127.0.0.1:{self.port}"
        client = SystemOneClient(base, timeout_s=15)
        deadline = time.time() + 300
        while time.time() < deadline:
            if self.proc.poll() is not None:
                self.stop()
                sys.exit(f"server exited early (code {self.proc.returncode}); log: {self.log_path}")
            try:
                if client.health().get("status") == "ok":
                    print(f"server ready on {base} after {time.time() - (deadline - 300):.0f}s")
                    return base
            except Exception:
                time.sleep(0.5)
        self.stop()
        sys.exit(f"server did not become healthy in 300s; log: {self.log_path}")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if getattr(self, "log", None):
            self.log.close()


def build_cases(blocks, labels, sanity_pool):
    cases = []
    for b in blocks:
        verdict = labels.get(b.uid, "")
        if verdict == "unsure":
            continue
        if verdict == "wrong":
            cases.append(dict(case_id=f"{b.uid}:real", uid=b.uid, page=b.page, label=b.label,
                              cls="real_ismatch", severity="", op="labeled_wrong",
                              text=b.text, base_crop=b.crop, expected="no"))
            continue
        cases.append(dict(case_id=f"{b.uid}:match", uid=b.uid, page=b.page, label=b.label,
                          cls="match", severity="", op="", text=b.text,
                          base_crop=b.crop, expected="yes"))
        for sev in SEVERITIES:
            res = corruption.corrupt(b.text, b.label, sev, b.uid)
            if res:
                new_text, op = res
                cases.append(dict(case_id=f"{b.uid}:{sev}", uid=b.uid, page=b.page, label=b.label,
                                  cls="mismatch", severity=sev, op=op, text=new_text,
                                  base_crop=b.crop, expected="no"))
        res = corruption.re_notate(b.text, b.label, b.uid)
        if res:
            new_text, op = res
            cases.append(dict(case_id=f"{b.uid}:notation", uid=b.uid, page=b.page, label=b.label,
                              cls="notation", severity="", op=op, text=new_text,
                              base_crop=b.crop, expected="yes"))
    with_text = [b for b in sanity_pool if b.text.strip()]
    for i, b in enumerate(with_text[:8]):
        cases.append(dict(case_id=f"{b.uid}:blank", uid=b.uid, page=b.page, label=b.label,
                          cls="sanity_blank", severity="", op="blank_image", text=b.text,
                          base_crop=corpus.blank_image_like(b.crop), expected="no"))
    by_page = {}
    for b in blocks:
        by_page.setdefault(b.page, []).append(b)
    made = 0
    for b in with_text:
        if made >= 8:
            break
        others = [o for o in by_page.get(b.page, []) if o.uid != b.uid and o.crop_size[0] > 8]
        if not others:
            continue
        other = others[b.index % len(others)]
        cases.append(dict(case_id=f"{b.uid}:cross", uid=b.uid, page=b.page, label=b.label,
                          cls="sanity_cross", severity="", op=f"img_of_{other.uid}", text=b.text,
                          base_crop=other.crop, expected="no"))
        made += 1
    return cases


def stratified(blocks, caps):
    by_label = {}
    for b in blocks:
        by_label.setdefault(b.label, []).append(b)
    out = []
    for label in sorted(by_label):
        bs = sorted(by_label[label], key=lambda b: (b.page, b.index))
        cap = caps.get(label, min(len(bs), 12))
        if len(bs) <= cap:
            out += bs
        else:
            picked, step = [], (len(bs) - 1) / (cap - 1)
            for i in range(cap):
                b = bs[round(i * step)]
                if b not in picked:
                    picked.append(b)
            out += picked
    return sorted(out, key=lambda b: (b.page, b.index))


def tiny_white_png():
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (96, 32), "white").save(buf, format="PNG")
    return buf.getvalue()


def probe(client, results_dir):
    out = {}
    png = tiny_white_png()
    probes = {
        "noul": ("Hello world", {"t": {"type": "noul",
                                       "instructions": "Does the image show the text 'Hello world'?"}}),
        "choice": ("Hello world", {"c": {"type": "choice",
                                         "instructions": "Which word does the state start with?",
                                         "criteria": {"hello": "the word hello", "world": "the word world"}}}),
        "score": ("Hello world", {"s": {"type": "score",
                                        "instructions": "How many words are in the state?",
                                        "criteria": ["1", "2", "3"]}}),
    }
    for name, (state, questions) in probes.items():
        try:
            out[name] = client.ask_raw(state, png, questions)
        except SystemOneError as e:
            out[name] = {"error": e.kind, "detail": e.detail[:2000]}
        print(f"probe {name}: {json.dumps(out[name], ensure_ascii=False)[:300]}")
    (results_dir / "probe.json").write_text(json.dumps(out, ensure_ascii=False, indent=2))
    return out


def parse_for_variant(variant, raw):
    answers = (raw or {}).get("answers") or {}
    val = answers.get(variant.score_question)
    p = V.noul_prob(val)
    extra = {}
    if p is None:
        cp = V.choice_probs(val)
        if cp:
            choice, probs = cp
            p = probs.get("same")
            extra["choice"] = choice
            extra["probs"] = probs
    leg = answers.get("legible")
    if leg is not None:
        extra["legible"] = V.noul_prob(leg)
    ek = answers.get("error_kind")
    if isinstance(ek, dict):
        extra["error_kind"] = ek.get("choice") or ek.get("selected")
    return p, extra


def _process_case(variant, case, policies, client, cache_dir, model_tag):
    import types
    block_proxy = types.SimpleNamespace(text=case["text"], label=case["label"])
    out = []
    for policy in policies:
        png, _size = corpus.apply_policy(case["base_crop"], policy)
        if policy != "as_is" and png == case["base_crop"]:
            continue  # policy did not change this crop
        if variant.images_fn:
            images = variant.images_fn(dict(case, base_crop=png))
            if not images:
                continue  # e.g. LaTeX render failed for this case
        else:
            images = [png]
        body = variant.build(block_proxy)
        key = hashlib.sha1(
            f"{model_tag}|{variant.vid}|{policy}|{case['case_id']}|{json.dumps(body, ensure_ascii=False, sort_keys=True)}".encode()
        ).hexdigest()
        cache_file = cache_dir / f"{key}.json"
        if cache_file.exists():
            out.append(json.loads(cache_file.read_text())["row"])
            continue
        try:
            raw = client.ask_raw(body["state"], images, body["questions"])
            p, extra = parse_for_variant(variant, raw)
            if p is None:
                raise SystemOneError("no_prob", json.dumps(raw)[:500])
            row = dict(variant=variant.vid, policy=policy, case_id=case["case_id"],
                       uid=case["uid"], page=case["page"], label=case["label"],
                       cls=case["cls"], severity=case["severity"], op=case["op"],
                       expected=case["expected"], text=case["text"], p=p, extra=extra,
                       input_tokens=(raw.get("usage") or {}).get("input_tokens"),
                       latency_s=raw.get("_latency_s"), error=None)
        except SystemOneError as e:
            row = dict(variant=variant.vid, policy=policy, case_id=case["case_id"],
                       uid=case["uid"], page=case["page"], label=case["label"],
                       cls=case["cls"], severity=case["severity"], op=case["op"],
                       expected=case["expected"], text=case["text"], p=None, extra={},
                       input_tokens=None, latency_s=None,
                       error=f"{e.kind}: {e.detail[:200]}")
        cache_file.write_text(json.dumps({"row": row}, ensure_ascii=False))
        out.append(row)
    return out


def run_matrix(client, variant_list, cases, policies_for, cache_dir, rows, model_tag,
               limit, concurrency=1):
    from concurrent.futures import ThreadPoolExecutor, as_completed
    done = fails = 0
    t0 = time.time()
    total = len(cases) * len(variant_list)
    for variant in variant_list:
        policies = policies_for(variant)
        sel = [c for c in cases if not variant.labels or c["label"] in variant.labels]
        if limit:
            sel = sel[:limit]
        v_fail = 0
        with ThreadPoolExecutor(max_workers=concurrency) as ex:
            futs = [ex.submit(_process_case, variant, c, policies, client, cache_dir,
                              model_tag) for c in sel]
            for fut in as_completed(futs):
                for row in fut.result():
                    rows.append(row)
                    done += 1
                    if row["error"]:
                        v_fail += 1
                        fails += 1
                        print(f"  FAIL {variant.vid} {row['case_id']}: {row['error'][:120]}")
                    if done % 25 == 0:
                        rate = done / (time.time() - t0)
                        eta = (total - done) / max(rate, 0.01)
                        print(f"  {done}/{total} done, {rate:.2f} req/s, "
                              f"eta {eta/60:.0f} min, fails {fails}")
        if v_fail > 0.3 * max(1, len(sel) * len(policies)):
            print(f"variant {variant.vid}: {v_fail} failures — too many, check server.log")
    return rows


def dedupe_rows(rows):
    seen, out = set(), []
    for r in rows:
        k = (r["variant"], r["policy"], r["case_id"])
        if k in seen:
            continue
        seen.add(k)
        out.append(r)
    return out


def main():
    args = parse_args()
    cfg = load_config(SCRIPT_DIR / args.config)
    corpus_path = Path(args.corpus) if args.corpus else REPO_ROOT / "OCRtest" / "TEST.llocr"
    work = Path(args.work) if args.work else corpus_path.parent / "work"
    labels_path = Path(args.labels) if args.labels else corpus_path.parent / "labels.json"
    extract_dir = work / "extract"
    cache_dir = work / "cache"
    results_dir = work / "results"
    for d in (extract_dir, cache_dir, results_dir):
        d.mkdir(parents=True, exist_ok=True)

    print(f"corpus: {corpus_path}")
    if args.server_parallel:
        cfg["parallel"] = args.server_parallel
    project = corpus.load_project(corpus_path, extract_dir)
    blocks = corpus.build_blocks(project, extract_dir, work / "pages")
    by_label = {}
    for b in blocks:
        by_label[b.label] = by_label.get(b.label, 0) + 1
    print(f"blocks: {len(blocks)} by label: {by_label}")

    labels = {}
    if labels_path.exists():
        labels = {k: v for k, v in json.loads(labels_path.read_text()).get("blocks", {}).items() if v}
    elif args.stage not in ("report",):
        template = {"$comment": "uid -> 'wrong' (recognized text is actually incorrect), "
                                "'unsure' (exclude), '' (default: presumed correct)",
                    "blocks": {b.uid: "" for b in blocks}}
        labels_path.write_text(json.dumps(template, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"labels template written: {labels_path}")

    profile = V.load_app_profile(cfg["profile_path"])
    variant_list = V.make_variants(profile)
    if args.variants:
        wanted = set(args.variants.split(","))
        variant_list = [v for v in variant_list if v.vid in wanted] or variant_list
    model_tag = Path(cfg["model"]).name + "@" + str(Path(cfg["model"]).stat().st_size)

    stage1_blocks = stratified(blocks, STAGE1_CAPS)
    stage1_cases = build_cases(stage1_blocks, labels, sanity_pool=stage1_blocks)
    full_cases = build_cases(blocks, labels, sanity_pool=stage1_blocks)
    print(f"stage1: {len(stage1_blocks)} blocks -> {len(stage1_cases)} cases; "
          f"full: {len(full_cases)} cases")

    if args.stage == "dry":
        from collections import Counter
        print("stage1 case classes:", dict(Counter(c["cls"] for c in stage1_cases)))
        print("full case classes:", dict(Counter(c["cls"] for c in full_cases)))
        return

    rows = []
    if args.stage == "report":
        rows = [json.loads(l)["row"] for l in
                (results_dir / "rows.jsonl").read_text().splitlines() if l.strip()]
    else:
        guard_no_conflicting_processes()
        server = None
        base_url = args.base_url
        if not base_url:
            server = Server(cfg, work, find_free_port())
            base_url = server.start()
        client = SystemOneClient(base_url, timeout_s=args.timeout)
        try:
            if args.stage in ("probe", "all"):
                probe(client, results_dir)
                small = [c for c in stage1_cases if c["cls"] == "match"][:5]
                run_matrix(client, variant_list[:1], small,
                           lambda v: ["as_is"], cache_dir, rows, model_tag, limit=0,
                           concurrency=1)
            if args.stage in ("1", "all"):
                cases = full_cases if args.full else stage1_cases
                if args.block_types:
                    wanted_labels = set(args.block_types.split(","))
                    cases = [c for c in cases if c["label"] in wanted_labels]
                print(f"--- stage 1: {len(variant_list)} variants x {len(cases)} cases")
                run_matrix(client, variant_list, cases,
                           lambda v: ["as_is"], cache_dir, rows, model_tag, args.limit,
                           concurrency=args.concurrency)
            if args.stage in ("2", "all"):
                prior = rows[:]
                if (results_dir / "rows.jsonl").exists():
                    prior += [json.loads(l)["row"] for l in
                              (results_dir / "rows.jsonl").read_text().splitlines() if l.strip()]
                prior = dedupe_rows(prior)
                m = {v: metrics.variant_metrics(prior, v) for v in {r["variant"] for r in prior}}
                ranked = sorted((v for v in m if m[v]["auc"] is not None),
                                key=lambda v: -m[v]["auc"])
                top2 = ranked[:2]
                if not top2:
                    top2 = [v.vid for v in variant_list[:2]]
                print(f"--- stage 2: top variants {top2} on all {len(full_cases)} cases")
                v2list = [v for v in variant_list if v.vid in top2]
                run_matrix(client, v2list, full_cases,
                           lambda v: ["as_is"], cache_dir, rows, model_tag, 0,
                           concurrency=args.concurrency)
                res_blocks = stratified(blocks, STAGE2_RES_CAPS)
                res_cases = [c for c in full_cases if c["uid"] in {b.uid for b in res_blocks}
                             and c["cls"] in ("match", "mismatch")]
                res_variants = [v for v in variant_list
                                if v.vid in set(top2) | {"v0_app_current"}]
                print(f"--- stage 2: resolution axis on {len(res_variants)} variants x "
                      f"{len(res_cases)} cases")
                run_matrix(client, res_variants, res_cases,
                           lambda v: ["up2x_small", "down1024"], cache_dir, rows, model_tag, 0,
                           concurrency=args.concurrency)
        finally:
            prior = []
            if (results_dir / "rows.jsonl").exists():
                prior = [json.loads(l)["row"] for l in
                         (results_dir / "rows.jsonl").read_text().splitlines() if l.strip()]
            rows = dedupe_rows(prior + rows)
            with open(results_dir / "rows.jsonl", "w", encoding="utf-8") as f:
                for r in rows:
                    f.write(json.dumps({"row": r}, ensure_ascii=False) + "\n")
            if server and not args.keep_server:
                server.stop()
                print("server stopped")
            elif server:
                print(f"server kept running on port {server.port}")

    rows = dedupe_rows(rows)
    meta = {
        "корпус": str(corpus_path),
        "модель": f"{Path(cfg['model']).name} + {Path(cfg['mmproj']).name}, ctx {cfg.get('ctx_size', 8192)}",
        "дата": dt.datetime.now().strftime("%Y-%m-%d %H:%M"),
        "кейсов в отчёте": len(rows),
        "порог в приложении сейчас": "0.6 (decision/matchThreshold)",
    }
    all_metrics = metrics.render_report(rows, meta, results_dir / "report.md")
    metrics.write_csv(rows, results_dir / "cases.csv")
    print(f"report: {results_dir / 'report.md'}")
    print(f"csv:    {results_dir / 'cases.csv'}")
    for v, m in sorted(all_metrics.items(), key=lambda kv: -(kv[1]['auc'] or 0)):
        print(f"  {v:24s} AUC={m['auc'] if m['auc'] is None else round(m['auc'], 3)} "
              f"match={m['match'].get('mean') and round(m['match']['mean'], 3)} "
              f"mismatch={m['mismatch'].get('mean') and round(m['mismatch']['mean'], 3)}")


if __name__ == "__main__":
    signal.signal(signal.SIGINT, signal.default_int_handler)
    main()
