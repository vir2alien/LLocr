#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A/B промпта fix-задачи qwen3.5-4b — провод приложения 1:1.

Отличия от run.py (старый стенд отстал от приложения):
  * промпты берутся из профиля МОДЕЛИ (resources/profiles/models/qwen3.5-4b.json),
    как это делает приложение с ADR 267 — systemPrompt и blockPrompts роли fix;
    профиль запросов fix-requests.json даёт алиасы типов и общий бюджет
    maxOutputByBlock;
  * глиф-стейдж (ADR 287): кроп формулы масштабируется до 32 px высоты буквы
    (метки equation/formula), остальные типы едут как есть
    (порт — prompt_ab_glyph.py);
  * мышление как в приложении сейчас: fixReasoningBudgetTokens = 256 из
    настроек пользователя -> reasoning_budget_tokens в теле + enable_thinking;
  * ретрай «мышление съело ответ» как FixModel::check (budget -> 0, max_tokens
    урезается на бюджет).

Два корпуса (--suite):
  equations  — 55 формул (24 корректных / 26 ошибочных) + 20 нечитаемых проб;
               варианты подменяют только промпт equation;
  alltypes   — 142 блока остальных типов из того же проекта; этап 1 = корректные
               кандидаты (хотим OK) + таблицы (сериализация кандидата и эталона
               несовместима, мерим только вердикты) + нечитаемые пробы текста;
               этап 2 = ошибочные (текст сэмплирован, редкие типы целиком).
               Варианты: current (длинные с дублем из профиля), min (одно
               предложение по типу), unified (одно предложение на всё).

Прогон:
    python3 prompt_ab.py --suite alltypes --stage 1
    python3 prompt_ab.py --suite alltypes --stage 2
    python3 prompt_ab.py --suite alltypes --report

Сервер: configs/prompt_ab.json (llama-server.real напрямую, отдельный порт) —
MITM-обёртка рантайма не трогается.
"""

import argparse
import hashlib
import io
import json
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

# compare.py импортирует удалённый в cleanup модуль gt; метрикам он не нужен.
_gt_stub = types.ModuleType("gt")
_gt_stub.GT = {}
sys.modules["gt"] = _gt_stub

import gate  # noqa: E402
import metrics  # noqa: E402
from client import ChatClient, ServerError, data_url, reply_text  # noqa: E402
from prompt_ab_glyph import apply_glyph_stage  # noqa: E402
from run import Server, load_model_profile, wait_port_free  # noqa: E402

MODEL_PROFILE = REPO / "resources" / "profiles" / "models" / "qwen3.5-4b.json"
REQUEST_PROFILE = REPO / "resources" / "profiles" / "requests" / "fix-requests.json"
CONFIG = HERE / "configs" / "prompt_ab.json"
OUT = HERE / "results" / "prompt_ab"

# Настройки пользователя (Настройки -> Проверка): бюджет мышления fix.
FIX_REASONING_BUDGET = 256

#: Пробы «нечитаемый блок»: кроп портится детерминированно, кандидат остаётся.
PROBE_SOURCES = ["p03b03", "p04b35", "p05b09", "p08b08"]
PROBE_KINDS = ["noise", "blur", "tiny", "slice", "blank"]
#: Пробы для alltypes — два корректных текстовых блока (выбираются по uid).
ALLTYPES_PROBE_KINDS = PROBE_KINDS
PROBE_TEXT_COUNT = 2
#: Ошибочных текстовых блоков в этапе 2 alltypes (равномерно по uid).
WRONG_TEXT_SAMPLE = 16


# ------------------------------------------------------------------ варианты

def current_equation_prompt(role):
    return role["blockPrompts"]["equation"]


MIN0 = ("OK if the OCR candidate matches the image, FIX followed by the corrected "
        "LaTeX if not, REVIEW if the image is unreadable.")

MIN1 = ("Compare the formula image with the OCR candidate. Reply OK if the candidate "
        "matches the image, FIX followed by the corrected formula in LaTeX if it does "
        "not, REVIEW if the image is unreadable.")

MIN2 = ("Compare the formula image with the OCR candidate.\n"
        "OK — the candidate matches the image.\n"
        "FIX — output the complete corrected formula as LaTeX.\n"
        "REVIEW — the image is unreadable.")

MIN_GUARD = MIN1 + (" When you correct, change only what the image contradicts and "
                    "keep the candidate's LaTeX style.")


def variants_for(role):
    """name -> текст blockPrompts.equation; systemPrompt у всех общий (роль)."""
    return {
        "current": current_equation_prompt(role),
        "min0": MIN0,
        "min1": MIN1,
        "min2": MIN2,
        "min_guard": MIN_GUARD,
    }


VARIANT_ORDER = ["current", "min0", "min1", "min2", "min_guard"]


# ------------------------------------------------- alltypes: промпты по типам

def typed_minimal(noun):
    return (f"OK if the OCR candidate matches the image, FIX followed by the "
            f"corrected {noun} if not, REVIEW if the image is unreadable.")


MIN_TYPED = {
    "text": typed_minimal("text"),
    "title": typed_minimal("heading"),
    "abstract": typed_minimal("abstract"),
    "list": typed_minimal("list"),
    "code": typed_minimal("code"),
    "caption": typed_minimal("caption"),
    "footnote": typed_minimal("note"),
    "ref_text": typed_minimal("entry"),
    "header": typed_minimal("header"),
    "footer": typed_minimal("footer"),
    "page_number": typed_minimal("page number"),
    "seal": typed_minimal("seal text"),
    "key_value": typed_minimal("field"),
    "table": typed_minimal("table"),
}

MIN_UNIFIED = typed_minimal("block")

#: Таблицы: и длинный промпт не удерживает сериализацию кандидата (<fcel>…<nl>
#: модель конвертирует в HTML) — проверяем явное указание формата.
MIN_TABLE_KEEP = typed_minimal("table in the candidate's own format")

#: Служебные типы (page_number/header/footer): у минимального промпта FIX-нагрузка
#: совпала бы с кандидатом, модель отвечает голое FIX без блока -> Review. Проверяем
#: формулировку с OK-филиалом первым.
def svc_mid(noun):
    return (f"OK if the OCR candidate matches the image. If the image contradicts it, "
            f"FIX followed by the corrected {noun}. REVIEW if the image is unreadable.")

SVC_MID = {"page_number": svc_mid("page number"), "header": svc_mid("header"),
           "footer": svc_mid("footer")}

ALLTYPES_ORDER = ["current", "min", "unified"]


def make_resolvers(role, aliases):
    """suite -> name -> resolver(label) -> текст промпта.

    Резолюция как blockPromptForRole: прямой ключ, потом алиас; варианту
    можно подменить промпт по алиасу типа.
    """
    def profile_prompt(label):
        direct = role["blockPrompts"].get(label)
        if direct:
            return direct
        return role["blockPrompts"].get(aliases.get(label, label), "")

    equations = {name: (lambda label, text=text: text if aliases.get(label, label) == "equation"
                        else profile_prompt(label)) for name, text in variants_for(role).items()}
    alltypes = {
        "current": profile_prompt,
        "min": lambda label: MIN_TYPED.get(aliases.get(label, label)) or MIN_UNIFIED,
        "unified": lambda label: MIN_UNIFIED,
        "min_keep": lambda label: (MIN_TABLE_KEEP if aliases.get(label, label) == "table"
                                   else MIN_TYPED.get(aliases.get(label, label)) or MIN_UNIFIED),
        "svc_mid": lambda label: (SVC_MID.get(label) or MIN_TYPED.get(aliases.get(label, label)) or MIN_UNIFIED),
    }
    return {"equations": equations, "alltypes": alltypes}


# --------------------------------------------------------------------- блоки

def load_blocks():
    """Корпус формул (дедуп): candidate/эталон/кроп из corpus_equations.json."""
    corpus = json.loads((HERE / "corpus" / "corpus_equations.json").read_text(encoding="utf-8"))
    seen = set()
    blocks = []
    for block in corpus["blocks"]:
        if block["uid"] in seen or block["label"] != "equation":
            continue
        seen.add(block["uid"])
        blocks.append(block)
    correct, wrong = [], []
    for block in blocks:
        (correct if metrics.cer(block, block["candidate_wire"]) == 0 else wrong).append(block)
    return correct, wrong


def load_alltypes(aliases):
    """Корпус остальных типов; таблицы вне CER-классификации (сериализации
    кандидата и эталона несовместимы) — отдельное подмножество."""
    corpus = json.loads((HERE / "corpus" / "corpus_alltypes.json").read_text(encoding="utf-8"))
    correct, wrong, tables = [], [], []
    for block in corpus["blocks"]:
        block["alias"] = aliases.get(block["label"], block["label"])
        if block["label"] == "table":
            tables.append(block)
        elif metrics.cer(block, block["candidate_wire"]) == 0:
            correct.append(block)
        else:
            wrong.append(block)
    return correct, wrong, tables


def probe_image(source, kind):
    from PIL import Image, ImageFilter
    import numpy as np

    img = Image.open(HERE / source["crop"])
    img.load()
    w, h = img.size
    if kind == "noise":
        rng = np.random.default_rng(1234)
        arr = rng.integers(0, 256, size=(h, w), dtype=np.uint8)
        return Image.fromarray(arr, "L").convert("RGB")
    if kind == "blank":
        return Image.new("RGB", (w, h), "white")
    if kind == "blur":
        small = img.resize((max(1, w // 4), max(1, h // 4)), Image.BICUBIC)
        return small.filter(ImageFilter.GaussianBlur(3)).resize((w, h), Image.BICUBIC)
    if kind == "tiny":
        small = img.resize((max(1, w // 8), 10), Image.BICUBIC)
        return small.resize((w, h), Image.BILINEAR)
    if kind == "slice":
        out = Image.new("RGB", (w, h), "white")
        top, bottom = int(h * 0.325), int(h * 0.675)
        out.paste(img.crop((0, top, w, bottom)), (0, top))
        return out
    raise SystemExit(f"неизвестная проба: {kind}")


# -------------------------------------------------------------------- запрос

def block_image(block):
    """Кроп как его шлёт приложение: глиф-стейдж до 32 px только для формул
    (cropGlyphHeightByBlock роли), остальные типы едут как есть (ADR 287)."""
    from PIL import Image

    img = Image.open(HERE / block["crop"])
    img.load()
    if block["label"] in ("equation", "formula"):
        img = apply_glyph_stage(img, 32)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def request_parameters(model, alias):
    """Параметры запроса: профиль модели + общий бюджет блока (по алиасу, как
    ModelProfiles::maxOutputForBlock) + мышление."""
    params = {name: value for name, value in model["params"]}
    budgets = dict(model["shared_max_output_by_block"])
    budgets.update(model["max_output_by_block"])
    params["max_tokens"] = budgets.get(alias, model["max_output"])
    if FIX_REASONING_BUDGET > 0:
        params["reasoning_budget_tokens"] = FIX_REASONING_BUDGET
    return params


def build_payload(block, model, block_prompt, image_png, aliases):
    """Тело запроса как buildRequestBody/buildRoot (promptBeforeImage = false)."""
    params = request_parameters(model, aliases.get(block["label"], block["label"]))
    thinking = params.get("reasoning_budget_tokens", 0)
    messages = []
    if model["system_prompt"]:
        messages.append({"role": "system", "content": model["system_prompt"]})
    messages.append({"role": "user", "content": [
        {"type": "text", "text": block_prompt},
        {"type": "image_url", "image_url": {"url": data_url(image_png)}},
        {"type": "text", "text": "OCR candidate:\n<CANDIDATE>\n" + block["candidate_wire"] + "\n</CANDIDATE>"},
    ]})
    root = {
        "model": model["alias"],
        "messages": messages,
        "chat_template_kwargs": {"enable_thinking": thinking > 0},
    }
    for name, value in params.items():
        root[name] = value
    return root


def without_thinking(payload):
    """Ретрай приложения (FixModel::withoutThinking): бюджет мышления -> ответ."""
    params = {k: v for k, v in payload.items() if k not in ("model", "messages", "chat_template_kwargs")}
    thinking = params.get("reasoning_budget_tokens", 0)
    params["reasoning_budget_tokens"] = 0
    if thinking > 0 and "max_tokens" in params:
        params["max_tokens"] = max(1, params["max_tokens"] - thinking)
    root = dict(payload)
    root.update(params)
    root["chat_template_kwargs"] = {"enable_thinking": False}
    return root


def thinking_ate_the_answer(data):
    choices = data.get("choices") or []
    if not choices:
        return False
    message = choices[0].get("message") or {}
    return not (message.get("content") or "") and bool(message.get("reasoning_content"))


def finish_reason(data):
    choices = data.get("choices") or []
    return choices[0].get("finish_reason", "") if choices else ""


# --------------------------------------------------------------------- прогон

def cache_key(payload):
    blob = json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return hashlib.sha1(blob.encode("utf-8")).hexdigest()


def cached_chat(client, payload, cache_dir, timeout_s, refresh=False):
    key = cache_key(payload)
    cache_file = cache_dir / f"{key}.json"
    if cache_file.exists() and not refresh:
        return json.loads(cache_file.read_text(encoding="utf-8"))
    client.timeout_s = timeout_s
    data = client.chat(payload)
    cache_file.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    return data


def run_case(client, payload, cache_dir, timeout_s):
    """Запрос + ретрай приложения, когда мышление съело весь бюджет ответа."""
    data = cached_chat(client, payload, cache_dir, timeout_s)
    retried = False
    if data is not None and thinking_ate_the_answer(data):
        data = cached_chat(client, without_thinking(payload), cache_dir, timeout_s)
        retried = True
    return data, retried


def run_variant(client, model, variant_name, resolve_prompt, cases, cache_dir, timeout_s, refresh, aliases=None):
    rows = []
    for block, image_png, tag, subset in cases:
        payload = build_payload(block, model, resolve_prompt(block["label"]), image_png, aliases or {})
        try:
            data, retried = run_case(client, payload, cache_dir, timeout_s)
        except ServerError as e:
            print(f"  !! {block['uid']} [{tag}] {e}")
            continue
        if data is None:
            continue
        content = reply_text(data)
        verdict, text, reason = gate.parse_reply(content, block["candidate_wire"], block["label"],
                                                 finish_reason(data))
        corrected = text if verdict == gate.FIXED else block["candidate_wire"]
        kind, cer_before, cer_after = metrics.classify(block, corrected)
        idx_after, _, _ = metrics.index_hit_rate(block, corrected)
        rows.append({
            "variant": variant_name, "uid": block["uid"], "tag": tag, "subset": subset,
            "verdict": verdict, "reason": reason, "kind": kind,
            "cer_before": cer_before, "cer_after": cer_after,
            "idx_after": idx_after, "retried_no_think": retried,
            "reply": content, "corrected": corrected if verdict == gate.FIXED else None,
            "finish": finish_reason(data),
            "latency_s": data.get("_latency_s"),
            "completion_tokens": (data.get("usage") or {}).get("completion_tokens"),
        })
        flag = "" if verdict in (gate.OK, gate.REVIEW) or subset != "correct" else "  <-- ПОРЧА"
        if subset == "probes" and verdict != gate.REVIEW:
            flag = "  <-- НЕ РЕАГИРУЕТ"
        print(f"  {variant_name:10s} {block['uid']:16s} {verdict:6s} {kind:8s} "
              f"cer {cer_before:.3f}->{cer_after:.3f}{flag}")
    return rows


def build_cases(subset_blocks, subset, sources, probe_sources=()):
    """(block, image, tag, subset) по всем случаям прогона."""
    cases = []
    for block in subset_blocks:
        cases.append((block, block_image(block), block["uid"], subset))
    for uid in probe_sources:
        src = sources[uid]
        for kind in PROBE_KINDS:
            probe = dict(src)
            probe["uid"] = f"{uid}#probe_{kind}"
            img = probe_image(src, kind)
            if src["label"] in ("equation", "formula"):
                img = apply_glyph_stage(img, 32)
            buf = io.BytesIO()
            img.save(buf, format="PNG")
            cases.append((probe, buf.getvalue(), probe["uid"], "probes"))
    return cases


def sample_evenly(blocks, count):
    """Равномерная детерминированная выборка по uid."""
    ordered = sorted(blocks, key=lambda b: b["uid"])
    if len(ordered) <= count:
        return ordered
    return [ordered[round(i * (len(ordered) - 1) / (count - 1))] for i in range(count)]


# --------------------------------------------------------------------- отчёт

SUITE_CORPORA = {"equations": ["corpus/corpus_equations.json"], "alltypes": ["corpus/corpus_alltypes.json"]}


def suite_uids(suite):
    uids = set()
    for name in SUITE_CORPORA[suite]:
        for block in json.loads((HERE / name).read_text(encoding="utf-8"))["blocks"]:
            uids.add(block["uid"])
    return uids


def print_report(rows, suite="equations", aliases=None):
    from collections import defaultdict

    uids = suite_uids(suite)
    rows = [r for r in rows if r["uid"].split("#")[0] in uids]
    by = defaultdict(list)
    for row in rows:
        by[(row["variant"], row["subset"])].append(row)
    order = VARIANT_ORDER + ["min", "unified"]
    for subset in ("correct", "wrong", "probes", "table"):
        groups = {v: r for (v, s), r in by.items() if s == subset}
        if not groups:
            continue
        print(f"\n=== {subset}")
        print(f"{'вариант':12s} {'Ok':>3s} {'Fix':>4s} {'Rev':>4s} {'улучш':>6s} {'порч':>5s} "
              f"{'CER до':>7s} {'CER после':>9s} {'инд после':>9s} {'с':>5s}")
        for variant in sorted(groups, key=lambda v: order.index(v) if v in order else 99):
            rows_v = groups[variant]
            n = len(rows_v)
            ok = sum(r["verdict"] == gate.OK for r in rows_v)
            fixed = sum(r["verdict"] == gate.FIXED for r in rows_v)
            review = sum(r["verdict"] == gate.REVIEW for r in rows_v)
            improved = sum(r["kind"] == "improved" for r in rows_v)
            damaged = sum(r["kind"] == "damaged" for r in rows_v)
            cer0 = sum(r["cer_before"] for r in rows_v) / n
            cer1 = sum(r["cer_after"] for r in rows_v) / n
            idx = sum(r["idx_after"] for r in rows_v) / n
            lat = sum(r["latency_s"] or 0 for r in rows_v) / n
            print(f"{variant:12s} {ok:3d} {fixed:4d} {review:4d} {improved:6d} {damaged:5d} "
                  f"{cer0:7.3f} {cer1:9.3f} {idx:9.2f} {lat:5.1f}")

    if suite != "alltypes":
        return

    # alltypes: разбивка корректных по типам (оси жалобы) и сериализация таблиц.
    per_type = defaultdict(list)
    for row in rows:
        if row["subset"] in ("correct", "probes"):
            per_type[(row["variant"], row["subset"])].append(row)
    labels = {}
    corpus = json.loads((HERE / "corpus" / "corpus_alltypes.json").read_text(encoding="utf-8"))
    for block in corpus["blocks"]:
        labels[block["uid"]] = aliases.get(block["label"], block["label"])
    print("\n=== корректные по типам (Ok / всего)")
    for variant in ALLTYPES_ORDER:
        sub = per_type.get((variant, "correct"))
        if not sub:
            continue
        buckets = defaultdict(lambda: [0, 0])
        for row in sub:
            bucket = buckets[labels.get(row["uid"], row["uid"].split("#")[0])]
            bucket[1] += 1
            if row["verdict"] == gate.OK:
                bucket[0] += 1
        line = "  ".join(f"{k}:{a}/{b}" for k, (a, b) in sorted(buckets.items()))
        print(f"{variant:12s} {line}")
    print("\n=== таблицы: вердикты и сериализация Fixed-ответа")
    for variant in ALLTYPES_ORDER:
        sub = by.get((variant, "table"))
        if not sub:
            continue
        verdicts = defaultdict(int)
        serials = defaultdict(int)
        for row in sub:
            verdicts[row["verdict"]] += 1
            if row["verdict"] == gate.FIXED:
                reply = row["corrected"] or ""
                if "<fcel>" in reply or "<nl>" in reply:
                    serials["fcel(кандидат)"] += 1
                elif "<table" in reply or "<tr" in reply:
                    serials["html"] += 1
                elif "|" in reply:
                    serials["pipe"] += 1
                else:
                    serials["прочее"] += 1
        print(f"{variant:12s} {dict(verdicts)}  Fixed-сериализация: {dict(serials)}")


# ----------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--suite", choices=["equations", "alltypes"], default="equations")
    ap.add_argument("--stage", type=int, choices=[1, 2], help="1 = корректные + пробы, 2 = ошибочные")
    ap.add_argument("--variant", action="append", default=[], help="имя варианта (по умолчанию все)")
    ap.add_argument("--report", action="store_true", help="только отчёт по rows.jsonl")
    ap.add_argument("--refresh", action="store_true", help="не брать ответы из кеша")
    ap.add_argument("--base-url", default="", help="использовать уже запущенный сервер")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--limit", type=int, default=0, help="взять только N первых случаев (валидация)")
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    cache_dir = OUT / "cache"
    cache_dir.mkdir(exist_ok=True)
    rows_file = OUT / "rows.jsonl"

    profile = json.loads(MODEL_PROFILE.read_text(encoding="utf-8"))
    role = profile["roles"]["fix"]
    model = load_model_profile(MODEL_PROFILE)
    requests = json.loads(REQUEST_PROFILE.read_text(encoding="utf-8"))
    aliases = requests.get("blockAliases", {})
    model["shared_max_output_by_block"] = requests.get("maxOutputByBlock", {})
    model["max_output_by_block"] = {}
    model["system_prompt"] = role.get("systemPrompt", "")

    resolvers = make_resolvers(role, aliases)[args.suite]
    selected = args.variant or (VARIANT_ORDER if args.suite == "equations" else ALLTYPES_ORDER)
    for name in selected:
        if name not in resolvers:
            raise SystemExit(f"нет варианта {name}; есть: {', '.join(sorted(resolvers))}")

    if args.report:
        rows = [json.loads(line) for line in rows_file.read_text(encoding="utf-8").splitlines() if line]
        print_report(rows, suite=args.suite, aliases=aliases)
        return

    if args.suite == "equations":
        correct, wrong = load_blocks()
        sources = {b["uid"]: b for b in correct + wrong}
        if args.stage == 1:
            cases = build_cases(correct, "correct", sources, PROBE_SOURCES)
        else:
            cases = build_cases(wrong, "wrong", sources)
    else:
        correct, wrong, tables = load_alltypes(aliases)
        sources = {b["uid"]: b for b in correct + wrong + tables}
        if args.stage == 1:
            text_correct = [b for b in correct if b["alias"] == "text"]
            probe_sources = [text_correct[0]["uid"], text_correct[-1]["uid"]] if text_correct else []
            cases = build_cases(correct, "correct", sources, probe_sources)
            cases += [(b, block_image(b), b["uid"], "table") for b in tables]
        else:
            sampled = sample_evenly([b for b in wrong if b["alias"] == "text"], WRONG_TEXT_SAMPLE)
            rest = [b for b in wrong if b["alias"] != "text"]
            cases = build_cases(sampled + rest, "wrong", sources)
    if args.limit:
        cases = cases[:args.limit]
    print(f"случаев: {len(cases)} (этап {args.stage}, корпус {args.suite}), вариантов: {len(selected)}")

    rows = []
    if rows_file.exists():
        rows = [json.loads(line) for line in rows_file.read_text(encoding="utf-8").splitlines() if line]
    if not args.limit:
        done = {(r["variant"], r["tag"], r["subset"]) for r in rows}
        cases = [c for c in cases if any((v, c[2], c[3]) not in done for v in selected)]
        if not cases:
            print("всё уже прогнано (кеш rows.jsonl)")
            print_report(rows, suite=args.suite, aliases=aliases)
            return

    def go(client):
        for name in selected:
            done_v = {(r["tag"], r["subset"]) for r in rows if r["variant"] == name}
            cases_v = [c for c in cases if (c[2], c[3]) not in done_v]
            if not cases_v:
                print(f"-> {name}: всё уже прогнано")
                continue
            print(f"-> {name} ({len(cases_v)} случаев)")
            fresh = run_variant(client, model, name, resolvers[name], cases_v, cache_dir, args.timeout, args.refresh, aliases)
            rows.extend(fresh)
            rows_file.write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows), encoding="utf-8")
        print_report(rows, suite=args.suite, aliases=aliases)

    if args.base_url:
        go(ChatClient(args.base_url, timeout_s=args.timeout))
    else:
        config = json.loads(CONFIG.read_text(encoding="utf-8"))
        server = Server(config, model, {})
        try:
            client = server.start()
        except SystemExit as e:
            print(f"!! сервер не поднялся: {e}")
            return
        try:
            go(client)
        finally:
            server.stop()



if __name__ == "__main__":
    main()
