# Model: Cloudflare clef-flash (decision)

> Manual launch, request contract and calibration. Русская версия:
> `docs/18-clef-flash.ru.md`. Measurements and the model-selection
> rationale: `docs/17-d1-verification-study.md`.

## 1. What it is

**clef-flash** is a Cloudflare decision model (~9.4B parameters): Qwen3.5-9B
with its native vision encoder plus a small "joint schema" head that scores
every option of every question in one pass. System One class: no text
generation, only probabilities over typed questions. Apache-2.0 licensed.
In LLocr it fills the **decision** task — verifying a recognized block
against its crop (`/v1/systemone`).

Why this model: d1-3B (see study §17) fundamentally cannot verify formulas
and tables — clef-flash with the same request scheme closes formulas
(AUC 0.907) and makes tables workable (0.787).

## 2. Files

| file | size | purpose |
|---|---|---|
| `Cloudflare_clef-flash-Q4_K_M.gguf` | 6.04 GB | the model (the only quant in the profile) |
| `mmproj-Cloudflare_clef-flash-f16.gguf` | 0.92 GB | vision projector, **mandatory** |

- Repository: `bartowski/Cloudflare_clef-flash-GGUF` (profile
  `resources/profiles/models/clef-flash.json`).
- Without the mmproj the model cannot see the crop — every verdict is
  unreliable.
- Q4_K_M is the repository's recommended balance; imatrix is impossible for
  decision models, so sub-4-bit quants lose noticeably more than usual.
- Memory: ~7 GB of weights; runs on a 16 GB RAM machine (the measurement
  used Q5_K_M, 7.9 GB — under memory pressure but stable).
- llama.cpp build **no older than b11430** (verified on b11564); the
  architecture is detected as `clef`, and the server exposes only
  `/v1/systemone` — no chat or completions.

## 3. Launch

App arguments (the model layer of the profile overrides `ctx-size` and adds
`batch-size`/`ubatch-size` on top of the platform layer `n-gpu-layers`/
`flash-attn` and the policy layer `parallel`/`cache-reuse`/
`no-context-shift`/`no-warmup`):

```
llama-server \
  --model <modelsDir>/bartowski__Cloudflare_clef-flash-GGUF/Cloudflare_clef-flash-Q4_K_M.gguf \
  --mmproj <modelsDir>/bartowski__Cloudflare_clef-flash-GGUF/mmproj-Cloudflare_clef-flash-f16.gguf \
  --alias llocr-decision \
  --host 127.0.0.1 --port <auto> \
  --ctx-size 8192 \
  --batch-size 8192 --ubatch-size 8192 \
  --n-gpu-layers 99 --flash-attn on --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

Practical notes, each verified during testing:

- **The whole prompt is evaluated in one batch.** `--ubatch-size` must be at
  least the longest prompt, otherwise long requests (large tables — up to
  ~5k tokens) get truncated or fail. 8192 gives headroom.
- **`--parallel N` splits `--ctx-size` across N slots.** For parallel
  requests raise both the slot count and the total context (the per-slot
  window must stay 8192).
- **Do not enable `--image-min-tokens 1024`.** llama.cpp warns that Qwen-VL
  "requires" ≥1024 image tokens for grounding; the measurement showed a 3×
  latency increase (~7 → ~22 s per request) with no measurable gain for
  this task.
- `--cache-reuse 0`, `--no-warmup` — same policy as the other tasks: the
  prompt cache is never reused (zero generation), no warmup needed.
- `state: null` in a request is **rejected** (unlike lfm2-d1) — the server
  answers 400 `"state" must be provided`. Pass empty content as an object
  or an empty string field.

Latency (Mac, Metal, Q5_K_M): ~7–15 s per formula request, 80–110 s for
large tables (4–5k prompt tokens). That is 3–4× slower than d1-3B — the
price for being able to verify formulas.

## 4. Request scheme (v6, unified with d3)

Since 19 Oct 2026 clef ships the **same scheme v6 as the d3 family**
(`docs/21-d3.md` §4b): per-block equivalence rules carried into
`state.notation`, a per-block threshold map, and a ready two-question dict —
`match` (noul) + `diagnosis` (choice: `exact_match` / `fixable_ocr_error` /
`unreadable_or_clipped` / `empty_crop`). clef reads the questions of a
request jointly in one prompt, so the second question costs no latency —
unlike d3, which pays one forward pass per question.

Rules:

- **State is a JSON object only**, with `block_type` (the block type),
  `recognized_text` (the recognized text as-is, no translation) and
  `notation` (the block type's equivalence rules from the profile,
  `blockPrompts[type].statePrompt`; the role-level `statePrompt` is the
  fallback).
- **Two questions**, `match` + `diagnosis`, handed over as the profile's
  ready dict. The `has_text`/`readable` noul gates stay out of the profile
  for uniformity with d3 — the empty/unreadable routes run through the
  diagnosis classes; adding them back is a JSON edit (free for clef, code
  and thresholds already support them).
- **No criteria**, no longer instructions — measured, none improve
  separation.
- Instruction texts are fixed (English); the content stays in the document
  language.
- The image is a PNG data-URI of the block crop at page resolution (300 DPI
  for PDFs), exactly what the app sends.

Response (the measured choice shape: a string label next to a
`probabilities` map):

```json
{
  "model": "llocr-decision",
  "answers": {
    "match": { "type": "noul", "noul": 0.83 },
    "diagnosis": { "type": "choice", "choice": "exact_match",
                   "probabilities": { "exact_match": 0.81, "fixable_ocr_error": 0.15,
                                      "unreadable_or_clipped": 0.03, "empty_crop": 0.01 },
                   "confidence": 0.86 } },
  "usage": { "input_tokens": 902, "output_tokens": 0 }
}
```

Routing: the same four-way rule as d3 (ADR 172) — top `empty_crop` /
`unreadable_or_clipped` → Review, Ok only when `match ≥ threshold` and top
= `exact_match`, else Mismatch.

## 4b. The measured v5 baseline

The scheme that closed formulas before v6 (docs/17, measured 13–14 Oct
2026): structured state + **one** noul question `match`, verdict `Ok` when
`noul ≥ decision/matchThreshold`. The 18 Oct 2026 corpus run of the app
(clef at 0.7, v5): FN 51 % / FP 20 % overall, FP 37 % on formulas —
dominated by `\tag{n}`, `\mathrm`, `\left(...\right)` being read as content.
That finding (and the unification with d3) is what motivated v6 here.

## 5. Calibration (measured 13–14 Oct 2026, docs/17)

Full OCRtest corpus (221 blocks; formulas — 64 blocks, tables — 9; the
corruption ground truth excludes LaTeX notation changes and edits inside
HTML tags):

| block class | mean gap | AUC |
|---|---|---|
| formulas, scheme v5 | **+0.422** | **0.907** |
| formulas, raw-string state (v0) | +0.111 | 0.794 |
| tables, scheme v5 | +0.143 | 0.787 |
| tables, raw-string state (v0) | +0.021 | 0.623 |

On formulas: correct blocks 0.816, corrupted 0.394; notation-only changes
(case, spacing, `\qquad`) are not rejected (0.808); a real severity
gradient exists (subtle 0.48 / moderate 0.49 / gross 0.25).

Threshold operating points on formulas:

| threshold | missed errors | false alarms |
|---|---|---|
| 0.6 | 33 % | 7 % |
| **0.7** | **27 %** | **9 %** |
| 0.75 | 17 % | 16 % |
| 0.8 | 13 % | 25 % |

Recommendation: `decision/matchThreshold = 0.7` (measured, formulas). The
v6 profile also carries the per-block map (`matchThresholdByBlock`,
0.68–0.82) copied from d3-mini — **uncalibrated for clef**: until the next
corpus run the role-level 0.7 is the measured number, and a false-alarm
spike means raising the map, not the setting.

## 6. Settings in the app

- Task — **decision** (ADR 162; the «Модели по задачам» window): keys
  `tasks/decision/modelPath`, `tasks/decision/mmprojPath`,
  `tasks/decision/requestProfileId = clef-flash`; the threshold lives at
  `decision/matchThreshold` and is shown in the task's Request tab.
- Profile: `resources/profiles/models/clef-flash.json`; installation via
  Settings → Models (HF repository `bartowski/Cloudflare_clef-flash-GGUF`,
  the Q4_K_M quant + mmproj-f16 on the model row) or the task window's
  install queue.
- External mode: any System One-compatible server, base URL + key; the
  model name does not take part in the request body.

## 7. Known limitations

- 3–4× slower than d1-3B (§3); under memory pressure on 16 GB RAM.
- Measured on Q5_K_M; Q4_K_M is expected to be within noise for this task,
  but has not been measured directly.
- Text blocks and short headings were not measured on clef (the runs
  covered formulas and tables) — calibrating the threshold on the full
  corpus is part of the migration plan.
- `state: null` → 400 (§3).
