#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Порт гейта исправлений приложения (src/models/FixDiffGate.cpp) на питон.

Стенд должен считать не «что ответила модель», а «что приложение применит к
блоку»: без этого сравнение вариантов запроса бессмысленно — половина ответов
живёт в гейте (Ok / Fixed / Review).

Порядок повторяет C++:
  FixModel::parseResponse  -> разбор вердикта (OK / FIX / REVIEW, иначе Review);
  FixDiffGate::fixSpan     -> правки `old => new` (текст, формулы);
  FixDiffGate::fixFullBlock-> целый блок без EDITS: (формулы) / с EDITS: (таблицы);
  нормализация -> normalizedForMatch / mathNormalize.
"""

import re
import unicodedata

OK, FIXED, REVIEW = "Ok", "Fixed", "Review"

NO_CHANGES_WORDS = {
    "none", "nothing", "nothingtochange", "nochanges", "nochange",
    "ничего", "нечегоисправлять", "нетправок",
}

STRUCTURE_RE = re.compile(r"^structure\s*:\s*(.+)$")
LOCATION_RE = re.compile(r"^(\d*r?\d+c\d+|\d+r\d+|r?\d+)\s*:\s*(.*)$")
THINK_BLOCK_RE = re.compile(r" thinking[\s\S]*? response\s*")
QUOTES = "«»“”„‟"
APOSTROPHES = "‘’‚‛"


# ---------------------------------------------------------------- нормализация

def _norm_char(c: str) -> str:
    return unicodedata.normalize("NFC", c)


def normalized_for_match(text: str, is_code: bool, join_hyphens: bool = False, drop_spaces: bool = False):
    """Возвращает (текст, origins) — нормализованный вид и карта на исходные индексы."""
    out = []
    origins = []
    pending_space = False
    pending_hyphen = False
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if join_hyphens and pending_hyphen and c in " \n\t":
            i += 1
            continue
        pending_hyphen = c == "-"
        if not is_code and c == "\\" and i + 1 < n and text[i + 1] in "()[]":
            i += 2
            continue
        produced = ""
        if c == "\u00ad":
            i += 1
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
        elif not is_code and (c == "\u00a0" or "\u2000" <= c <= "\u200a" or c == "\u202f" or c == "\u205f"
                              or c == "\u3000" or c == " " or c == "\t"):
            if drop_spaces or pending_space or not out:
                i += 1
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
                    origins[-1] = i
                    i += 1
                    continue
        for ch in produced:
            out.append(ch)
            origins.append(i)
        i += 1
    while out and out[-1] == " ":
        out.pop()
        origins.pop()
    return "".join(out), origins


def normalize_for_equivalence(text: str, is_code: bool) -> str:
    return normalized_for_match(text, is_code)[0]


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


def parse_edit_line(raw_line: str):
    """Возвращает (old, new, location) или None, если строка битая."""
    line = raw_line.strip()
    structure = STRUCTURE_RE.match(line)
    if structure and "=>" not in raw_line:
        return structure.group(1).strip(), "", "structure"
    arrow = line.find("=>")
    if arrow < 0:
        return None
    left = line[:arrow].strip()
    new = line[arrow + 2:].strip()
    located = LOCATION_RE.match(left)
    location = ""
    if located:
        location = located.group(1)
        left = located.group(2).strip()
    left = left.replace("\\n", "\n")
    new = new.replace("\\n", "\n")
    return left, new, location


def parse_edit_lines(lines):
    """Возвращает (edits, noop_only, error)."""
    edits = []
    noop_only = True
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if no_changes_word(line):
            continue
        noop_only = False
        parsed = parse_edit_line(line)
        if parsed is None:
            continue
        edits.append(parsed)
    if not edits:
        return [], noop_only, "The FIX reply lists no edits."
    return edits, noop_only, ""


# ------------------------------------------------------------------------ гейт

def _source_span(hay_text, hay_origins, frm, count):
    if frm < 0 or frm + count > len(hay_text):
        return -1, -1
    return hay_origins[frm], hay_origins[frm + count - 1] + 1


def fix_span(candidate: str, body: str, block_type: str):
    is_code = block_type == "code"
    edits, noop_only, error = parse_edit_lines(body.split("\n"))
    if not edits:
        if noop_only:
            return REVIEW, None, "The model declared no change instead of a verdict."
        if "=>" not in body:
            return fix_full_block(candidate, body, block_type)
        return REVIEW, None, error

    updated = candidate
    view_text, view_origins = normalized_for_match(updated, is_code)
    applied = 0
    for old, new, _location in edits:
        if not old:
            return REVIEW, None, "An edit has no anchor text on the left of '=>'."
        if normalize_for_equivalence(old, is_code) == normalize_for_equivalence(new, is_code) or (
                ("\\" in old or "\\" in new) and math_equivalent(old, new)):
            continue
        found = updated.find(old)
        if found < 0 or updated.find(old, found + 1) >= 0:
            needle = normalize_for_equivalence(old, is_code)
            text_view, origins = view_text, view_origins
            if needle and needle not in text_view:
                text_view, origins = normalized_for_match(updated, is_code, not is_code)
                needle = normalize_for_equivalence(old.replace("- ", "-"), is_code)
            if needle and needle not in text_view and ("\\" in old or "\\" in new):
                text_view, origins = normalized_for_match(updated, is_code, False, True)
                needle = math_normalize(old)
            first = text_view.find(needle) if needle else -1
            if first < 0:
                return REVIEW, None, f"The edit anchor was not found in the block: {old[:80]}"
            if text_view.find(needle, first + 1) >= 0:
                return REVIEW, None, f"The edit anchor matches more than one place: {old[:80]}"
            span = _source_span(text_view, origins, first, len(needle))
            if span[0] < 0:
                return REVIEW, None, f"The edit anchor was not found in the block: {old[:80]}"
            found, length = span
            view_text, view_origins = text_view, origins
        else:
            length = len(old)
        updated = updated[:found] + new + updated[found + length:]
        view_text, view_origins = normalized_for_match(updated, is_code)
        applied += 1

    if applied == 0:
        return OK, candidate, "Every proposed fix was an equivalent no-op; the block text is kept."
    return FIXED, updated, ""


def fix_full_block(candidate: str, body: str, block_type: str):
    is_code = block_type == "code"
    lines = body.split("\n")
    edits_line = -1
    for i, line in enumerate(lines):
        if line.strip().upper() in ("EDITS:", "EDITS"):
            edits_line = i
            break
    corrected = "\n".join(lines[:edits_line] if edits_line >= 0 else lines).strip()
    if not corrected:
        return REVIEW, None, "The corrected block is empty."
    if no_changes_word(corrected):
        return REVIEW, None, "The model declared no change instead of a verdict."

    equivalent = normalize_for_equivalence(corrected, is_code) == normalize_for_equivalence(candidate, is_code) or (
        ("\\" in corrected or "\\" in candidate) and math_equivalent(corrected, candidate))
    if equivalent:
        return OK, candidate, "The corrected block is equivalent to the candidate; the block text is kept."
    if edits_line < 0:
        if block_type in ("equation", "formula"):
            return FIXED, corrected, ""
        if block_type == "table":
            return REVIEW, None, "The corrected block is missing its EDITS: section."
        return REVIEW, None, "The reply carries a whole block; this block type expects span edits."

    # EDITS:-секция: приложение требует, чтобы каждое изменение было объявлено.
    edits, noop_only, error = parse_edit_lines(lines[edits_line + 1:])
    if not edits:
        if noop_only:
            return REVIEW, None, "The reply declares no edits but the corrected block differs."
        return REVIEW, None, error
    for old, new, _location in edits:
        if old and normalize_for_equivalence(old, is_code) not in normalize_for_equivalence(candidate, is_code) \
                and old not in candidate:
            return REVIEW, None, f"A declared edit is not part of the block: {old[:80]}"
    return FIXED, corrected, ""


def parse_reply(content: str, candidate: str, block_type: str):
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
        if block_type == "table":
            return fix_full_block(candidate, body, block_type)
        return fix_span(candidate, body, block_type)
    return REVIEW, None, f"The reply does not follow the OK/FIX/REVIEW scheme: {trimmed[:120]}"
