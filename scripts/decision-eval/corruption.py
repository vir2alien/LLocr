"""Text corruption and notation-rewrite engines for building labeled cases.

Every operation returns (new_text, description) or None when not applicable;
`corrupt()` / `re_notate()` keep trying until one produces a changed text.
Both are deterministic: seeded from the block uid, so a rerun rebuilds the
exact same case set.
"""

import random
import re

HOMOGLYPHS = [
    ("O", "0"), ("0", "O"), ("l", "1"), ("1", "l"), ("I", "1"),
    ("а", "a"), ("a", "а"), ("о", "o"), ("o", "о"), ("е", "e"), ("e", "е"),
    ("с", "c"), ("c", "с"), ("р", "p"), ("p", "р"), ("В", "B"), ("К", "K"),
    ("М", "M"), ("Н", "H"), ("Т", "T"), ("х", "x"), ("у", "y"),
]

NEUTRAL_WORDS = ["однако", "также", "следовательно", "например", "the", "also", "then"]

WORD_RE = re.compile(r"[0-9A-Za-zА-Яа-яЁё]+")
NUMBER_RE = re.compile(r"\d+")


def _words_with_spans(text):
    return list(WORD_RE.finditer(text))


def _is_cyr(c):
    return "\u0400" <= c <= "\u04FF"


# ---- subtle ----

def homoglyph(text, rng):
    matches = _words_with_spans(text)
    candidates = []
    for m in matches:
        for pos, ch in enumerate(m.group()):
            for src, dst in HOMOGLYPHS:
                if ch == src:
                    candidates.append((m.start() + pos, src, dst))
    if not candidates:
        return None
    pos, src, dst = rng.choice(candidates)
    return text[:pos] + dst + text[pos + 1 :], f"homoglyph {src!r}->{dst!r}"


def digit_shift(text, rng):
    numbers = [m for m in NUMBER_RE.finditer(text) if len(m.group()) >= 1]
    if not numbers:
        return None
    m = rng.choice(sorted(numbers, key=lambda x: len(x.group()), reverse=True)[:3])
    digits = list(m.group())
    i = rng.randrange(len(digits))
    digits[i] = str((int(digits[i]) + 1) % 10)
    return text[: m.start()] + "".join(digits) + text[m.end() :], f"digit+1 in {m.group()}"


def punct_swap(text, rng):
    pairs = [(",", "."), (".", ","), (";", ":"), (":", ";")]
    rng.shuffle(pairs)
    for src, dst in pairs:
        pos = text.find(src)
        if pos >= 0:
            return text[:pos] + dst + text[pos + 1 :], f"{src!r}->{dst!r}"
    return None


# ---- moderate ----

def drop_word(text, rng):
    words = [m for m in _words_with_spans(text) if len(m.group()) >= 3]
    if not words:
        return None
    m = rng.choice(words)
    out = text[: m.start()] + text[m.end() :]
    return out.rstrip() if m.end() == len(text.strip()) else out, f"drop word {m.group()!r}"


def dup_word(text, rng):
    words = _words_with_spans(text)
    if not words:
        return None
    m = rng.choice(words)
    return text[: m.end()] + " " + m.group() + text[m.end() :], f"duplicate word {m.group()!r}"


def split_word(text, rng):
    words = [m for m in _words_with_spans(text) if len(m.group()) >= 8]
    if not words:
        return None
    m = rng.choice(words)
    w = m.group()
    mid = len(w) // 2
    return text[: m.start()] + w[:mid] + " " + w[mid:] + text[m.end() :], f"split word {w!r}"


def join_words(text, rng):
    for m in re.finditer(r"(\S{3,}) (\S{3,})", text):
        return text[: m.start()] + m.group(1) + m.group(2) + text[m.end() :], f"join {m.group()!r}"
    return None


def swap_words(text, rng):
    pairs = list(re.finditer(r"(\S+) (\S+)", text))
    if not pairs:
        return None
    m = rng.choice(pairs)
    swapped = m.group(2) + " " + m.group(1)
    return text[: m.start()] + swapped + text[m.end() :], f"swap {m.group()!r}"


def letter_change(text, rng):
    words = [m for m in _words_with_spans(text) if len(m.group()) >= 4]
    if not words:
        return None
    m = rng.choice(words)
    w = m.group()
    i = rng.randrange(1, len(w) - 1) if len(w) > 2 else 0
    repl = "х" if _is_cyr(w[i]) else "q"
    if w[i] == repl or not w[i].isalpha():
        repl = "е" if _is_cyr(w[i]) else "z"
    return text[: m.start()] + w[:i] + repl + w[i + 1 :] + text[m.end() :], f"letter in {w!r}"


def latex_bound_drop(text, rng):
    m = re.search(r"_\{([^\}=]+)=[^\}]*\}", text)
    if not m:
        return None
    return re.sub(r"_\{([^\}=]+)=[^\}]*\}", r"_{" + m.group(1) + "}", text, count=1), "drop bound"


def latex_frac_swap(text, rng):
    m = re.search(r"\\frac\{([^{}]+)\}\{([^{}]+)\}", text)
    if not m or m.group(1) == m.group(2):
        return None
    return (
        text[: m.start()] + "\\frac{" + m.group(2) + "}{" + m.group(1) + "}" + text[m.end() :],
        "frac swap",
    )


# ---- gross ----

def replace_number(text, rng):
    numbers = [m for m in NUMBER_RE.finditer(text) if len(m.group()) >= 1]
    if not numbers:
        return None
    m = rng.choice(numbers)
    old = m.group()
    while True:
        new = "".join(str(rng.randrange(10)) for _ in old)
        if new != old:
            break
    return text[: m.start()] + new + text[m.end() :], f"number {old}->{new}"


def drop_line(text, rng):
    if "\n" in text:
        lines = text.split("\n")
        idxs = [i for i, ln in enumerate(lines) if ln.strip()]
        if len(idxs) >= 2:
            i = rng.choice(idxs)
            gone = lines[i].strip()
            out = "\n".join(lines[:i] + lines[i + 1 :])
            return out, f"drop line {gone[:30]!r}"
    words = [m for m in _words_with_spans(text) if len(m.group()) >= 3]
    if words:
        m = max(words, key=lambda x: len(x.group()))
        return text[: m.start()] + text[m.end() :], f"drop word {m.group()!r}"
    return None


def extra_word(text, rng):
    words = _words_with_spans(text)
    if not words:
        return None
    if len(words) >= 3:
        m = rng.choice(words)
        ins = m.group()
    else:
        ins = rng.choice(NEUTRAL_WORDS)
    end = words[-1].end()
    return text[:end] + " " + ins + text[end:], f"append word {ins!r}"


def _is_latexy(text):
    return bool(re.search(r"\\frac|\\sum|\\int|\\lim|\\prod|\^|_", text))


SUBTLE = [homoglyph, digit_shift, punct_swap]
MODERATE = [drop_word, dup_word, split_word, join_words, swap_words, letter_change]
GROSS = [replace_number, drop_line, extra_word]


def _apply_any(ops, text, rng):
    order = list(ops)
    rng.shuffle(order)
    for op in order:
        result = op(text, rng)
        if result is not None and result[0] != text:
            return result
    return None


def corrupt(text, label, severity, uid):
    """Return (new_text, op) for a content corruption, or None if nothing applies."""
    rng = random.Random(f"{uid}:{severity}")
    if not text.strip():
        return None
    if label == "page_number":
        ops = {"subtle": [digit_shift], "moderate": [replace_number], "gross": [replace_number]}[severity]
        return _apply_any(ops, text, rng) or _apply_any(SUBTLE + GROSS, text, rng)
    moderate, gross = list(MODERATE), list(GROSS)
    if _is_latexy(text):
        moderate += [latex_bound_drop, latex_frac_swap]
    ops = {"subtle": SUBTLE, "moderate": moderate, "gross": gross}[severity]
    return _apply_any(ops, text, rng) or _apply_any(SUBTLE + MODERATE + GROSS, text, rng)


# ---- notation (expected to stay "yes" under the shipped profile rules) ----

CASE_FREE_LABELS = {"text", "abstract", "image_caption", "table_caption",
                    "figure_footnote", "table_footnote", "list", "code", "seal"}


def case_flip(text, rng):
    for i, ch in enumerate(text):
        if ch.isalpha() and ch.islower():
            return text[:i] + ch.upper() + text[i + 1 :], f"case {ch}->{ch.upper()}"
    return None


def rewrap(text, rng):
    if "\n" in text:
        pos = text.find("\n")
        return text[:pos] + " " + text[pos + 1 :], "join lines"
    spaces = [m.start() for m in re.finditer(" ", text)]
    if len(spaces) < 2:
        return None
    pos = spaces[len(spaces) // 2]
    return text[:pos] + "\n" + text[pos + 1 :], "split line"


def hyphen_join(text, rng):
    m = re.search(r"(\w)-\n(\w)", text)
    if not m:
        return None
    return text[: m.start()] + m.group(1) + m.group(2) + text[m.end() :], "join hyphenation"


def space_collapse(text, rng):
    if "  " in text:
        return text.replace("  ", " ", 1), "collapse double space"
    m = re.search(r"(?<=\S) (?=\S)", text)
    if not m:
        return None
    return text[: m.start()] + "  " + text[m.end() :], "double space"


def latex_space(text, rng):
    if not _is_latexy(text):
        return None
    for src, dst in (("+", " + "), ("=", " = ")):
        pos = text.find(src)
        if pos >= 0 and not src.startswith(" "):
            return text[:pos] + dst + text[pos + 1 :], f"space around {src!r}"
    return None


def re_notate(text, label, uid):
    rng = random.Random(f"{uid}:notation")
    ops = [rewrap, hyphen_join, space_collapse]
    if label in CASE_FREE_LABELS:
        ops.insert(0, case_flip)
    if label == "equation" or _is_latexy(text):
        ops.append(latex_space)
    return _apply_any(ops, text, rng)
