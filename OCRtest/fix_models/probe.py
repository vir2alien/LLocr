#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проба: «распознать с нуля» против «исправить ранее распознанное».

Две руки на одном кропе и на одном сервере:

  fix  — запрос из профиля (resources/profiles/requests/fix-requests.json):
         кандидат в <CANDIDATE>, ответ — вердикт OK/FIX/REVIEW, при FIX — целый
         исправленный блок; результат проходит через порт гейта (gate.py);
  read — запрос на распознавание (RECOGNIZE_* ниже): кандидата нет, модель
         читает картинку сама и отдаёт транскрипцию.

Обе руки получают одно и то же знание об осях проверки (скрипт, регистр,
начертание, позиция, скобки, целостность обозначения) — различаются они только
наличием кандидата и формой ответа. Иначе сравнение измеряло бы не «с нуля
против починки», а «сколько правил в промпте».

Плюс реестр вариантов fix-запроса (FIX_VARIANTS): он отвечает на вопрос, можно
ли починить fix-запрос для моделей, которые хорошо читают, но плохо чинят.

Меряется тем же, что и в стенде: CER к ручному эталону и доля прочитанных
индексов (под- и надстрочные группы).

    python3 probe.py --model gemma-4-12b --config configs/gemma-4-12b-cpu.json
    python3 probe.py --model qwen3.5-4b --base-url http://127.0.0.1:8123 \
        --fix-variants v0_app --fix-variants no_cand
"""

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import gate  # noqa: E402
import metrics  # noqa: E402
import run as runner  # noqa: E402
import variants as variants_mod  # noqa: E402
from client import ChatClient, reply_text  # noqa: E402

FIX_REQUESTS = REPO / "resources" / "profiles" / "requests" / "fix-requests.json"
DOCUMENT_LANGUAGE = "Russian"  # VerificationQueueController::kDocumentLanguage

#: Общее знание об осях проверки. Обе руки (и все варианты fix) начинаются с
#: него, чтобы различались только наличием кандидата и формой ответа.
AXES = (
    "1. Script. Designations, indices and tags are written in the document's alphabet. "
    "Cyrillic read as Latin is an error even when the glyph looks right: judge by the "
    "designation, not by the glyph. Greek letters used as symbols are correct.\n"
    "2. Case. Uppercase and lowercase differ and carry meaning.\n"
    "3. Face. Upright, italic, bold and bold italic differ and carry meaning.\n"
    "4. Position. Baseline, subscript, superscript and nesting depth differ.\n"
    "5. Delimiters. Every opening bracket is paired; none is stray, duplicated or missing.\n"
    "6. Unit integrity. A designation, index or tag is one whole: read all of its "
    "characters, never mix scripts inside it.\n"
    "7. Small print. Subscripts and superscripts are the hardest part of the image: read "
    "them at the size they are printed, do not guess them from a familiar formula."
)

_HEADER = (
    "The image is the only source of truth: write what the image shows, not what the "
    "formula usually looks like.\n"
    "Document language: {{LANGUAGE}}.\n\n"
    "Write the block exactly as the image shows it: the same characters, the same script, "
    "the same case, the same face (upright, italic, bold), the same position of every "
    "subscript and superscript, the same nesting depth, the same delimiters. For a "
    "formula, write the complete LaTeX of the formula, including its tag.\n\n"
    "Read every axis on its own — a block can be right on one and wrong on another:\n"
    + AXES
    + "\n\n"
    "Do not add content that is not in the image. Do not complete a formula you cannot "
    "fully read. Do not simplify, factor, expand or restructure it."
)

#: Запрос на распознавание. Кандидата в нём нет — модель читает кроп сама.
RECOGNIZE_SYSTEM = (
    "You are an OCR engine reading one block of a technical document. "
    + _HEADER
    + "\n\n"
    "If a character is cut by the crop, covered, or blurred past recognition, reply with "
    "the single word REVIEW and nothing else. Otherwise reply with the transcription "
    "only, and nothing else."
)

RECOGNIZE_USER = "Transcribe the block from the image."

#: Тот же протокол, но в форме fix-задачи: модель не чинит чужой кандидат, а сама
#: пишет блок — и отдаёт его гейту как исправленный.
FIX_NO_CANDIDATE_SYSTEM = (
    "You produce the corrected text of one block of a technical document. "
    + _HEADER
    + "\n\n"
    "Reply with exactly one of these and nothing else:\n\n"
    "FIX\n"
    "<the complete corrected block>\n"
    "REVIEW\n\n"
    "Use REVIEW when a character is cut by the crop, covered, or blurred past recognition. "
    "No explanations, no code fences."
)

#: Тип-подсказка для протокола без кандидата: блока-эталона, на который можно
#: опереться, нет, поэтому формулировки «как в кандидате» заменены на «как на
#: картинке».
NO_CANDIDATE_TYPE = {
    "equation": (
        "Block type: mathematical formula.\n"
        "Mode after FIX: the complete formula as LaTeX, written by you from the image.\n\n"
        "Write the formula as the image shows it. Keep the author's notation, macro style "
        "and visible bold/italic distinctions; do not simplify, expand, factor or "
        "restructure it.\n\n"
        "Always get right: base variables; subscript vs superscript (R_1 vs R_i vs R^1); "
        "Greek vs Latin (\\alpha vs a, \\nu vs v, \\rho vs p, \\kappa vs k); italic vs "
        "upright vs bold; operators (+, -, \\pm, \\times, \\cdot, =, \\approx, \\neq); "
        "summation and integral limits; radicals; fractions; brackets; prime, hat, bar and "
        "vec accents; integral-sign class; a missing line, fragment, comma or period.\n\n"
        "Tags, indices and designations are content, not carrier: a Latin suffix where the "
        "document uses Cyrillic is an error. Inside a tag, index or designation, a "
        "character that looks like a digit may be a letter the image shows — decide by "
        "the image."
    ),
}

#: Разметка вокруг транскрипции, которую модель могла добавить сама.
LEAD_RE = re.compile(r"^\s*(?:transcription|latex|formula|the transcription)\s*[:\-—]\s*", re.I)

#: Модель, ответившая «OK», может следом дописать свой вариант блока («FIXed block:»).
#: Гейт такой ответ выбрасывает — а предложенный блок стоит посмотреть отдельно.
OWN_PAYLOAD_RE = re.compile(
    r"(?:fixed\s+block|fix\s*ed\s+block|corrected\s+block|исправленн\w*\s+блок)\s*[:：]?\s*",
    re.I)


def own_payload(content):
    """Блок, который модель предложила сама, после вердикта, который гейт отбросил."""
    match = OWN_PAYLOAD_RE.search(content or "")
    if not match:
        return ""
    tail = (content or "")[match.end():].strip()
    return gate.strip_outer_code_fence(tail).strip()


#: Реестр вариантов fix-запроса.
#:   hint          — добавка к системному промпту (None = как в приложении);
#:   candidate     — показывать ли кандидата в сообщении;
#:   system        — "app" (промпт приложения) или "nocand" (протокол без кандидата);
#:   type          — "app" (тип-подсказка приложения) или "nocand";
#:   extra_body    — добавка к телу запроса (например, включение мышления);
#:   server        — серверные параметры (требуют перезапуска сервера).
FIX_VARIANTS = {
    "v0_app": {"hint": None, "candidate": True, "system": "app", "type": "app",
               "extra_body": {}, "server": {}},
    "script_hint": {"hint": variants_mod.SCRIPT_HINT, "candidate": True, "system": "app",
                    "type": "app", "extra_body": {}, "server": {}},
    "no_cand_raw": {"hint": None, "candidate": False, "system": "app", "type": "app",
                    "extra_body": {}, "server": {}},
    "no_cand": {"hint": None, "candidate": False, "system": "nocand", "type": "nocand",
                "extra_body": {}, "server": {}},
    "no_cand_hint": {"hint": variants_mod.SCRIPT_HINT, "candidate": False, "system": "nocand",
                     "type": "nocand", "extra_body": {}, "server": {}},
    "no_cand_think": {"hint": None, "candidate": False, "system": "nocand", "type": "nocand",
                      "extra_body": {"reasoning_budget_tokens": 512,
                                     "chat_template_kwargs": {"enable_thinking": True}},
                      "server": {}},
    "hint_think": {"hint": variants_mod.SCRIPT_HINT, "candidate": True, "system": "app",
                   "type": "app",
                   "extra_body": {"reasoning_budget_tokens": 512,
                                  "chat_template_kwargs": {"enable_thinking": True}},
                   "server": {}},
}


def clean_read_reply(text):
    """Ответ руки read: снять обёртку ``` и возможную подпись, развернуть \\(...\\) обёртку."""
    out = gate.strip_outer_code_fence(text or "").strip()
    out = LEAD_RE.sub("", out)
    return out.strip()


def read_is_review(text):
    return text.strip().upper().startswith("REVIEW")


def run_arm(client, corpus, model, prompts, arm, cache_dir, timeout_s, refresh, image_spec):
    """Одна рука на всём корпусе. Возвращает список строк."""
    rows = []
    for block in corpus["blocks"]:
        image = runner.prepare_image(HERE / block["crop"], image_spec)
        variant = variants_mod.by_name("v0_app")
        label = model["block_aliases"].get(block["label"], block["label"])

        if arm == "read":
            system_prompt = RECOGNIZE_SYSTEM.replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE)
            type_prompt = RECOGNIZE_USER
            with_candidate = False
            extra_body = {}
        else:
            spec = FIX_VARIANTS["v0_app" if arm == "fix" else arm]
            if spec["system"] == "app":
                system_prompt = prompts["system"]
            else:
                system_prompt = FIX_NO_CANDIDATE_SYSTEM.replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE)
            hint = spec["hint"]
            if hint:
                hint = hint.replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE)
                system_prompt = system_prompt + "\n\n" + hint
            if spec["type"] == "app":
                type_prompt = prompts["type"][label]
            else:
                type_prompt = NO_CANDIDATE_TYPE.get(label, prompts["type"][label])
            with_candidate = spec["candidate"]
            extra_body = spec["extra_body"]

        payload = runner.build_payload(block, model, variant, image, system_prompt, type_prompt,
                                       with_candidate=with_candidate)
        if extra_body:
            payload.update(extra_body)

        data = runner.cached_chat(client, payload, cache_dir, timeout_s, refresh)
        if data is None:
            print(f"  !! {block['uid']}: нет ответа")
            continue
        content = reply_text(data)
        finish = runner.finish_reason(data)
        lat = data.get("_latency_s")
        tok = (data.get("usage") or {}).get("completion_tokens")

        if arm == "read":
            text = clean_read_reply(content)
            if read_is_review(text):
                row = {"arm": "read", "uid": block["uid"], "verdict": "Review",
                       "reason": "модель не смогла прочитать блок", "result": "",
                       "reply": content, "latency_s": lat, "completion_tokens": tok,
                       "truncated": finish == "length", "own_payload": ""}
            else:
                row = {"arm": "read", "uid": block["uid"], "verdict": "Read", "reason": "",
                       "result": text, "reply": content, "latency_s": lat,
                       "completion_tokens": tok, "truncated": finish == "length",
                       "own_payload": ""}
        else:
            verdict, text, reason = gate.parse_reply(content, block["candidate_wire"],
                                                     block["label"], finish)
            corrected = text if verdict == gate.FIXED else block["candidate_wire"]
            row = {"arm": arm, "uid": block["uid"], "verdict": verdict, "reason": reason,
                   "result": corrected, "reply": content, "latency_s": lat,
                   "completion_tokens": tok, "truncated": finish == "length",
                   "own_payload": own_payload(content)}
        rows.append(row)
    return rows


def score(corpus, rows, arms):
    """Считает метрики рук: CER и индексы к эталону."""
    blocks = {b["uid"]: b for b in corpus["blocks"]}
    out = []
    for uid, block in blocks.items():
        cand = block["candidate_wire"]
        row = {"uid": uid, "gt": block["gt"],
               "cer_cand": metrics.cer(block, cand),
               "idx_cand": metrics.index_hit_rate(block, cand)[0]}
        for arm in arms:
            hit = next((r for r in rows if r["uid"] == uid and r["arm"] == arm), None)
            if hit is None:
                row[arm] = None
                continue
            res = hit["result"]
            row[arm] = {
                "verdict": hit["verdict"], "reply": hit["reply"], "result": res,
                "cer": metrics.cer(block, res) if res else 1.0,
                "idx": metrics.index_hit_rate(block, res)[0] if res else 0.0,
                "latency_s": hit["latency_s"], "tokens": hit["completion_tokens"],
                "truncated": hit["truncated"],
                "changed": res != cand and bool(res),
                "own_payload": hit.get("own_payload") or "",
            }
            own = hit.get("own_payload") or ""
            if own:
                row[arm]["own_cer"] = metrics.cer(block, own)
                row[arm]["own_idx"] = metrics.index_hit_rate(block, own)[0]
            else:
                row[arm]["own_cer"] = None
                row[arm]["own_idx"] = None
        out.append(row)
    out.sort(key=lambda r: r["uid"])
    return out


def print_report(scored, arms, title):
    print(f"\n=== {title}")
    header = f"{'uid':9s} {'CER канд':>9s}"
    for arm in arms:
        header += f" {'CER ' + arm[:11]:>15s} {'инд ' + arm[:11]:>14s}"
    print(header)
    agg = {}
    for r in scored:
        line = f"{r['uid']:9s} {r['cer_cand']:9.3f}"
        for arm in arms:
            a = r.get(arm)
            if not a:
                line += f" {'-':>15s} {'-':>14s}"
                continue
            line += f" {a['cer']:15.3f} {a['idx']:14.2f}"
            d = agg.setdefault(arm, {"cer": 0.0, "idx": 0.0, "n": 0, "verdicts": {},
                                      "own_n": 0, "own_cer": 0.0, "own_idx": 0.0, "lat": 0.0})
            d["cer"] += a["cer"]
            d["idx"] += a["idx"]
            d["n"] += 1
            d["lat"] += a["latency_s"] or 0
            d["verdicts"][a["verdict"]] = d["verdicts"].get(a["verdict"], 0) + 1
            if a.get("own_payload"):
                d["own_n"] += 1
                d["own_cer"] += a["own_cer"]
                d["own_idx"] += a["own_idx"]
        print(line)

    n = max(1, len(scored))
    line = f"{'СРЕДНЕЕ':9s} {sum(r['cer_cand'] for r in scored)/n:9.3f}"
    for arm in arms:
        d = agg.get(arm)
        line += f" {d['cer']/d['n']:15.3f} {d['idx']/d['n']:14.2f}" if d else f" {'-':>15s} {'-':>14s}"
    print("-" * len(line))
    print(line)
    for arm in arms:
        d = agg.get(arm)
        if not d:
            continue
        v = ", ".join(f"{k}×{c}" for k, c in sorted(d["verdicts"].items()))
        extra = ""
        if d["own_n"]:
            extra = f"; модель предложила свой блок в {d['own_n']}: CER {d['own_cer']/d['own_n']:.3f}"
        print(f"{arm:16s} вердикты: {v}; с/запрос {d['lat']/d['n']:.1f}{extra}")
    return agg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default="gemma-4-e4b", help="id профиля модели")
    ap.add_argument("--model-profile", default="")
    ap.add_argument("--config", default=str(HERE / "configs" / "gemma-4-e4b.json"))
    ap.add_argument("--corpus", default=str(HERE / "corpus" / "corpus_probe5.json"))
    ap.add_argument("--out", default=str(HERE / "results" / "probe"))
    ap.add_argument("--base-url", default="", help="использовать уже запущенный сервер")
    ap.add_argument("--timeout", type=float, default=900.0)
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--image-scale", type=float, default=1.0)
    ap.add_argument("--server-override", action="append", default=[],
                    help="ключ=значение для аргумента сервера (--model-draft=...)")
    ap.add_argument("--arms", default="fix,read",
                    help="список рук: fix (запрос приложения), read (распознавание), "
                         "или имя варианта из FIX_VARIANTS")
    ap.add_argument("--fix-variants", action="append", default=[],
                    help="вариант fix-запроса из FIX_VARIANTS (можно повторять)")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir = out_dir / "cache"
    cache_dir.mkdir(parents=True, exist_ok=True)

    corpus = metrics.load_corpus(args.corpus)
    profile_path = Path(args.model_profile) if args.model_profile else \
        runner.model_profile_path(args.model)
    model = runner.load_model_profile(profile_path)
    requests = json.loads(FIX_REQUESTS.read_text(encoding="utf-8"))
    model["block_aliases"] = requests.get("blockAliases", {})
    model["shared_max_output_by_block"] = requests.get("maxOutputByBlock", {})
    prompts = {
        "system": requests["systemPrompt"].replace("{{LANGUAGE}}", DOCUMENT_LANGUAGE),
        "type": dict(requests["blockPrompts"]),
    }

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    config.setdefault("port", 8123)
    image_spec = {"scale": args.image_scale, "resample": "bicubic", "pad_px": 0,
                  "gray": False, "autocontrast": False, "binarize": False}

    overrides = {}
    for item in args.server_override:
        key, _, value = item.partition("=")
        overrides[key.replace("-", "_")] = value

    arms = [a.strip() for a in args.arms.split(",") if a.strip()]
    for name in args.fix_variants:
        if name not in FIX_VARIANTS:
            raise SystemExit(f"нет варианта {name}; есть: {', '.join(FIX_VARIANTS)}")
        if name not in arms:
            arms.append(name)

    rows = []
    if args.base_url:
        client = ChatClient(args.base_url, timeout_s=args.timeout)
        for arm in arms:
            print(f"-> {arm}")
            rows += run_arm(client, corpus, model, prompts, arm, cache_dir, args.timeout,
                            args.refresh, image_spec)
    else:
        server = runner.Server(config, model, overrides)
        try:
            client = server.start()
        except SystemExit as e:
            raise SystemExit(f"сервер не поднялся: {e}")
        try:
            for arm in arms:
                print(f"-> {arm}")
                rows += run_arm(client, corpus, model, prompts, arm, cache_dir, args.timeout,
                                args.refresh, image_spec)
                (out_dir / f"rows_{arm}.jsonl").write_text(
                    "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows), encoding="utf-8")
        finally:
            server.stop()

    scored = score(corpus, rows, arms)
    agg = print_report(scored, arms, f"{model['id']} · {len(corpus['blocks'])} блоков · "
                                     f"scale={args.image_scale}")
    (out_dir / "scored.json").write_text(json.dumps(
        {"model": model["id"], "arms": arms, "rows": rows, "scored": scored, "agg": agg},
        ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nответы: {out_dir}")


if __name__ == "__main__":
    main()
