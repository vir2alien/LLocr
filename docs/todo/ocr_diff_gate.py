"""
Reference parser & diff-gate for OCR verification outputs:
  - Variant A (`span_hybrid`): span replacements `old => new` for text blocks,
    full block + `EDITS:` for `table` / `equation` / `formula`.
  - Variant B (`full_block_with_edits`): full corrected block + mandatory
    `EDITS:` section for all block types, verified by a deterministic diff-gate.

If the model makes an undeclared edit, fails uniqueness, or produces a no-op
`FIX`, the parser safely downgrades the verdict to `REVIEW` so silent drift
never corrupts already-correct OCR text.
"""

from __future__ import annotations

import difflib
import re
import unicodedata
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


SPECIAL_TOKENS = (
    "<|im_end|>",
    "<|endoftext|>",
    "<|im_start|>",
    "<|end|>",
)

FULL_BLOCK_TYPES_IN_VARIANT_A = {"table", "equation", "formula"}


@dataclass
class EditPair:
    old: str
    new: str
    location: Optional[str] = None  # e.g. "r2c3" for table edits


@dataclass
class VerificationResult:
    verdict: str  # "OK" | "FIX" | "REVIEW" | "EMPTY"
    raw_verdict: str
    text: str
    edits: List[EditPair] = field(default_factory=list)
    downgraded_reason: Optional[str] = None


def strip_special_tokens(raw: str) -> str:
    cleaned = raw
    for tok in SPECIAL_TOKENS:
        cleaned = cleaned.replace(tok, "")
    # Strip accidental markdown fences if a model still emits them
    cleaned = cleaned.strip()
    if cleaned.startswith("```") and cleaned.endswith("```"):
        lines = cleaned.splitlines()
        if len(lines) >= 2:
            cleaned = "\n".join(lines[1:-1]).strip()
    return cleaned


def normalize_for_equivalence(s: str, is_code: bool = False) -> str:
    """
    Applies the prompt's equivalence policy so no-op 'fixes' (like NBSP -> space
    or straight -> typographic quotes) are caught and treated as OK.
    """
    s = unicodedata.normalize("NFC", s)
    s = s.replace("\u00ad", "")  # soft hyphen
    s = s.replace("\u2026", "...")
    for lig, exp in (("ﬀ", "ff"), ("ﬁ", "fi"), ("ﬂ", "fl"), ("ﬃ", "ffi"), ("ﬄ", "ffl")):
        s = s.replace(lig, exp)
    if not is_code:
        # Spaces
        s = re.sub(r"[\u00a0\u2000-\u200a\u202f\u205f\u3000]+", " ", s)
        s = re.sub(r"[ \t]+", " ", s)
        # Quotes
        s = re.sub(r"[«»“”„‟]", '"', s)
        s = re.sub(r"[‘’‚‛]", "'", s)
    return s.strip()


def _parse_edit_lines(lines: List[str]) -> Tuple[List[EditPair], Optional[str]]:
    edits: List[EditPair] = []
    for raw_line in lines:
        line = raw_line.strip()
        if not line:
            continue
        if "=>" not in line:
            return [], f"Malformed edit line without '=>': {line!r}"
        left, right = line.split("=>", 1)
        loc = None
        # Optional table prefix like "r2c3: old" or "structure: ..."
        m = re.match(r"^(r\d+c\d+|structure)\s*:\s*(.*)$", left.strip())
        if m:
            loc = m.group(1)
            old_val = m.group(2).strip()
        else:
            old_val = left.strip()
        new_val = right.strip()
        old_val = old_val.replace("\\n", "\n")
        new_val = new_val.replace("\\n", "\n")
        edits.append(EditPair(old=old_val, new=new_val, location=loc))
    if not edits:
        return [], "FIX verdict contained no edit lines"
    return edits, None


def apply_span_edits(
    candidate: str,
    edits: List[EditPair],
    is_code: bool = False,
) -> VerificationResult:
    """
    Applies Variant A span edits sequentially while enforcing:
      1. `old` must exist in `candidate`
      2. `old` must match uniquely in the remaining window (or whole string)
      3. if all changes are equivalent under the policy, downgrade FIX -> OK
    """
    updated = candidate
    applied: List[EditPair] = []

    for edit in edits:
        if not edit.old:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=edits,
                downgraded_reason="Empty old_substring in span edit (insertion without anchor)",
            )
        occurrences = updated.count(edit.old)
        if occurrences == 0:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=edits,
                downgraded_reason=f"Anchor not found in candidate: {edit.old!r}",
            )
        if occurrences > 1:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=edits,
                downgraded_reason=f"Ambiguous anchor ({occurrences} matches): {edit.old!r}",
            )
        updated = updated.replace(edit.old, edit.new, 1)
        applied.append(edit)

    if normalize_for_equivalence(updated, is_code=is_code) == normalize_for_equivalence(
        candidate, is_code=is_code
    ):
        return VerificationResult(
            verdict="OK",
            raw_verdict="FIX",
            text=candidate,
            edits=applied,
            downgraded_reason="All proposed edits were equivalence-policy no-ops; kept original candidate",
        )

    return VerificationResult(
        verdict="FIX",
        raw_verdict="FIX",
        text=updated,
        edits=applied,
    )


def verify_full_block_with_diffgate(
    candidate: str,
    corrected_block: str,
    declared_edits: List[EditPair],
    is_code: bool = False,
    is_table: bool = False,
) -> VerificationResult:
    """
    Diff-gate for Variant B (and Variant A's table/equation blocks):
      1. Checks that every declared `old` existed in `candidate` and every `new`
         exists in `corrected_block`.
      2. Extracts actual character/word diff spans between `candidate` and
         `corrected_block` and verifies that every non-equivalent change is
         covered by the declared `EDITS:` list.
      3. If the model silently drifted outside declared edits, downgrades to REVIEW.
    """
    if normalize_for_equivalence(corrected_block, is_code=is_code) == normalize_for_equivalence(
        candidate, is_code=is_code
    ):
        return VerificationResult(
            verdict="OK",
            raw_verdict="FIX",
            text=candidate,
            edits=declared_edits,
            downgraded_reason="Corrected block is equivalent to candidate; kept original candidate",
        )

    for edit in declared_edits:
        if edit.location == "structure":
            continue
        if edit.old and edit.old not in candidate:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=declared_edits,
                downgraded_reason=f"Declared old fragment not found in candidate: {edit.old!r}",
            )
        if edit.new and edit.new not in corrected_block:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=declared_edits,
                downgraded_reason=f"Declared new fragment not found in corrected block: {edit.new!r}",
            )

    # Check for undeclared edits by comparing token-level diffs
    cand_tokens = re.findall(r"\S+", candidate)
    corr_tokens = re.findall(r"\S+", corrected_block)
    matcher = difflib.SequenceMatcher(None, cand_tokens, corr_tokens, autojunk=False)

    declared_old_blob = " ".join(e.old for e in declared_edits)
    declared_new_blob = " ".join(e.new for e in declared_edits)
    has_structural_edit = any(e.location == "structure" for e in declared_edits)

    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            continue
        old_chunk = " ".join(cand_tokens[i1:i2])
        new_chunk = " ".join(corr_tokens[j1:j2])

        if normalize_for_equivalence(old_chunk, is_code=is_code) == normalize_for_equivalence(
            new_chunk, is_code=is_code
        ):
            continue

        if is_table and has_structural_edit:
            continue

        old_covered = (not old_chunk) or (old_chunk in declared_old_blob) or any(
            tok in declared_old_blob for tok in cand_tokens[i1:i2]
        )
        new_covered = (not new_chunk) or (new_chunk in declared_new_blob) or any(
            tok in declared_new_blob for tok in corr_tokens[j1:j2]
        )

        if not (old_covered and new_covered):
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                edits=declared_edits,
                downgraded_reason=(
                    f"Undeclared diff detected outside EDITS: {old_chunk!r} -> {new_chunk!r}"
                ),
            )

    return VerificationResult(
        verdict="FIX",
        raw_verdict="FIX",
        text=corrected_block,
        edits=declared_edits,
    )


def parse_verifier_response(
    raw_response: str,
    candidate: str,
    block_type: str = "text",
    protocol: str = "span_hybrid",  # "span_hybrid" (Variant A) | "full_block_with_edits" (Variant B)
) -> VerificationResult:
    cleaned = strip_special_tokens(raw_response)
    if not cleaned:
        return VerificationResult(
            verdict="REVIEW",
            raw_verdict="",
            text=candidate,
            downgraded_reason="Empty model response",
        )

    lines = cleaned.splitlines()
    first = lines[0].strip().rstrip(".:").upper()

    if first == "OK" and len(lines) == 1:
        return VerificationResult(verdict="OK", raw_verdict="OK", text=candidate)

    if first == "REVIEW":
        return VerificationResult(verdict="REVIEW", raw_verdict="REVIEW", text=candidate)

    if first == "EMPTY":
        return VerificationResult(verdict="EMPTY", raw_verdict="EMPTY", text="")

    if first != "FIX":
        return VerificationResult(
            verdict="REVIEW",
            raw_verdict=first,
            text=candidate,
            downgraded_reason=f"Unrecognized verdict token: {lines[0]!r}",
        )

    body_lines = lines[1:]
    is_code = block_type == "code"
    is_table = block_type == "table"

    use_full_block = (
        protocol == "full_block_with_edits"
        or block_type in FULL_BLOCK_TYPES_IN_VARIANT_A
    )

    if not use_full_block:
        edits, err = _parse_edit_lines(body_lines)
        if err:
            return VerificationResult(
                verdict="REVIEW",
                raw_verdict="FIX",
                text=candidate,
                downgraded_reason=err,
            )
        return apply_span_edits(candidate, edits, is_code=is_code)

    # Full-block + EDITS mode
    edits_idx = None
    for idx, line in enumerate(body_lines):
        if line.strip().upper() == "EDITS:":
            edits_idx = idx
            break

    if edits_idx is None:
        return VerificationResult(
            verdict="REVIEW",
            raw_verdict="FIX",
            text=candidate,
            downgraded_reason="Missing mandatory 'EDITS:' section in full-block FIX response",
        )

    corrected_block = "\n".join(body_lines[:edits_idx]).strip()
    edit_lines = body_lines[edits_idx + 1 :]
    if not corrected_block:
        return VerificationResult(
            verdict="REVIEW",
            raw_verdict="FIX",
            text=candidate,
            downgraded_reason="Empty corrected block before 'EDITS:'",
        )

    edits, err = _parse_edit_lines(edit_lines)
    if err:
        return VerificationResult(
            verdict="REVIEW",
            raw_verdict="FIX",
            text=candidate,
            downgraded_reason=err,
        )

    return verify_full_block_with_diffgate(
        candidate=candidate,
        corrected_block=corrected_block,
        declared_edits=edits,
        is_code=is_code,
        is_table=is_table,
    )


if __name__ == "__main__":
    sample_candidate = "В 2026 году коэффициент k = 0,85 был подтвержден в Таблице 3."

    # 1. Variant A: clean unique span edit
    resp_a = "FIX\nкоэффициент k = 0,85 был => коэффициент k = 0,35 был"
    res_a = parse_verifier_response(resp_a, sample_candidate, block_type="text", protocol="span_hybrid")
    assert res_a.verdict == "FIX" and "0,35" in res_a.text, res_a

    # 2. Variant A: equivalence no-op (straight quotes vs «») -> auto-downgraded to OK
    resp_noop = 'FIX\n"Альфа" => «Альфа»'
    res_noop = parse_verifier_response(
        resp_noop, 'Проект "Альфа" завершен.', block_type="text", protocol="span_hybrid"
    )
    assert res_noop.verdict == "OK", res_noop

    # 3. Variant B: full block with valid declared edit
    resp_b_ok = (
        "FIX\n"
        "В 2026 году коэффициент k = 0,35 был подтвержден в Таблице 3.\n"
        "EDITS:\n"
        "0,85 => 0,35"
    )
    res_b_ok = parse_verifier_response(
        resp_b_ok, sample_candidate, block_type="text", protocol="full_block_with_edits"
    )
    assert res_b_ok.verdict == "FIX" and "0,35" in res_b_ok.text, res_b_ok

    # 4. Variant B: silent undeclared drift ("подтвержден" -> "доказан") -> caught and sent to REVIEW
    resp_b_drift = (
        "FIX\n"
        "В 2026 году коэффициент k = 0,35 был доказан в Таблице 3.\n"
        "EDITS:\n"
        "0,85 => 0,35"
    )
    res_b_drift = parse_verifier_response(
        resp_b_drift, sample_candidate, block_type="text", protocol="full_block_with_edits"
    )
    assert res_b_drift.verdict == "REVIEW" and "Undeclared diff" in (res_b_drift.downgraded_reason or ""), res_b_drift

    print("All self-tests passed.")
