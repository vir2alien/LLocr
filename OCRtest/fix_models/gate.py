#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Порт гейта исправлений приложения (src/models/FixDiffGate.cpp) на питон.

Стенд должен считать не «что ответила модель», а «что приложение применит к
блоку»: без этого сравнение вариантов запроса бессмысленно — половина ответов
живёт в гейте (Ok / Fixed / Review).

Порядок повторяет C++ (ADR 192):
  FixModel::parseResponse -> разбор вердикта (OK / FIX / REVIEW, иначе Review);
  FixDiffGate::fixBlock    -> целый исправленный блок как единственный payload;
  нормализация -> normalized_for_match / math_normalize.
"""

import re
import unicodedata

OK, FIXED, REVIEW = "Ok", "Fixed", "Review"

NO_CHANGES_WORDS = {
    "none", "nothing", "nothingtochange", "nochanges", "nochange",
    "ничего", "нечегоисправлять", "нетправок",
}

THINK_BLOCK_RE = re.compile(r" thinking[\s\S]*? response\s*")
QUOTES = "«»“”„‟"
APOSTROPHES = "‘’‚‛"


# ---------------------------------------------------------------- нормализация

def _norm_char(c: str) -> str:
    return unicodedata.normalize("NFC", c)


def normalized_for_match(text: str, is_code: bool) -> str:
    """Нормализованный вид для сравнения: NFC, мягкий перенос, лигатуры,
    многоточие, пробелы и переводы строк схлопываются (кроме кода), кавычки
    приводятся к ASCII. Перенос строки после дефиса — перенос слова, а не
    содержимое: дефис остаётся, разрыв исчезает."""
    out = []
    pending_space = False
    pending_hyphen = False
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        is_space = c in " \t\n\r"
        if is_space and pending_hyphen and not is_code:
            pending_hyphen = False
            i += 1
            continue
        pending_hyphen = c == "-"
        if not is_code and c == "\\" and i + 1 < n and text[i + 1] in "()[]":
            i += 2
            continue
        produced = ""
        if c == "\u00ad":
            continue
        elif c == "\u2026":
            produced = "..."
        elif c == "\ufb00":
            produced = "ff"
        elif c == "\ufb01":
            produced = "fi"
        elif c == "\ufb02":
            produced = "fl"
        elif c == "\ufb03":
            produced = "ffi"
        elif c == "\ufb04":
            produced = "ffl"
        elif not is_code and (c == "\u00a0" or " " <= c <= " " or c == " " or c == " "
                              or c == "　" or is_space):
            if pending_space or not out:
                continue
            pending_space = True
            produced = " "
        elif not is_code and c in QUOTES:
            produced = '"'
        elif not is_code and c in APOSTROPHES:
            produced = "'"
        else:
            pending_space = False
            produced = _norm_char(c)
            if not is_code and len(produced) == 1 and out and unicodedata.combining(c):
                combined = unicodedata.normalize("NFC", out[-1] + produced)
                if len(combined) == 1:
                    out[-1] = combined
                    i += 1
                    continue
        out.append(produced)
        i += 1
    while out and out[-1] == " ":
        out.pop()
    return "".join(out)


def normalize_for_equivalence(text: str, is_code: bool) -> str:
    return normalized_for_match(text, is_code)


def math_normalize(text: str) -> str:
    """mathNormalize: \\tag{n} -> (n), x_{1} -> x_1, пробелы и обёртка — carrier."""
    out = re.sub(r"\\tag\s*\{([^}]*)\}", r"(\1)", text)
    out = re.sub(r"([_^])\s*\{([^{}\s]+)\}", r"\1\2", out)
    out = re.sub(r"\s+", "", out)
    out = re.sub(r"^\$\$([\s\S]*)\$\$$", r"\1", out)
    out = re.sub(r"^\\\[([\s\S]*)\\\]$", r"\1", out)
    return out


def math_equivalent(a: str, b: str) -> bool:
    return math_normalize(a) == math_normalize(b)


# ------------------------------------------------------------------ разбор ответа

def strip_control_tokens(text: str) -> str:
    out = THINK_BLOCK_RE.sub("", text)
    idx = out.find(" thinking")
    if idx >= 0:
        out = out[:idx]
    return out.strip()


def strip_outer_code_fence(text: str) -> str:
    trimmed = text.strip()
    if not trimmed.startswith("```"):
        return text
    open_end = trimmed.find("\n")
    if open_end < 0:
        return text
    close_start = trimmed.rfind("\n")
    if close_start <= open_end:
        return text
    if trimmed[close_start + 1:].strip() != "```":
        return text
    return trimmed[open_end + 1:close_start].strip()


def no_changes_word(line: str) -> bool:
    compact = re.sub(r"[.:!\s]", "", line[:24]).lower()
    return compact in NO_CHANGES_WORDS


# ------------------------------------------------------------------------ гейт

def fix_block(candidate: str, body: str, block_type: str):
    """Единственный payload FIX — целый исправленный блок (ADR 192)."""
    is_code = block_type == "code"
    corrected = body.strip()
    if not corrected:
        return REVIEW, None, "The corrected block is empty."
    if no_changes_word(corrected):
        return REVIEW, None, "The model declared no change instead of a verdict; the block needs a human look."

    equivalent = normalize_for_equivalence(corrected, is_code) == normalize_for_equivalence(candidate, is_code) or (
        ("\\" in corrected or "\\" in candidate) and math_equivalent(corrected, candidate))
    if equivalent:
        return OK, candidate, "The corrected block is equivalent to the candidate; the block text is kept."
    return FIXED, corrected, ""


def parse_reply(content: str, candidate: str, block_type: str, finish_reason: str = ""):
    """Вердикт приложения по ответу модели: (status, text, reason)."""
    cleaned = strip_outer_code_fence(strip_control_tokens(content))
    trimmed = cleaned.strip()
    upper = trimmed.upper()

    if upper.startswith("OK"):
        return OK, candidate, "OK"
    if upper.startswith("REVIEW"):
        return REVIEW, None, "REVIEW"
    if upper.startswith("FIX"):
        body = trimmed[3:].strip()
        if body.startswith(":"):
            body = body[1:].strip()
        if not body:
            return REVIEW, None, "The verdict is explicit but the payload is missing."
        if finish_reason == "length":
            return REVIEW, None, "The reply was cut by the token budget; the block needs a human look."
        return fix_block(candidate, body, block_type)
    return REVIEW, None, f"The reply does not follow the OK/FIX/REVIEW scheme: {trimmed[:120]}"
