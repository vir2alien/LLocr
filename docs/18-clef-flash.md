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

## 4. Request scheme (v7, unified with d3)

clef ships the **same scheme v7 as the d3 family**
(`docs/21-d3.md` §4b): one noul question `match`, the axes the block is
checked on in `state.notation`, a per-block threshold map. clef reads the
questions of a request jointly in one prompt, so a second question would
cost it no latency — unlike d3, which pays one forward pass per question.
The scheme is one question anyway: the axes carry the veto the second
question used to provide, in a single pass.

Rules:

- **State is a JSON object only**, with `block_type` (the block type),
  `recognized_text` (the recognized text as-is, no translation) and
  `notation` (the axes every block is checked on plus the block type's own
  rules, from the request profile — the role-level `statePrompt` and the
  per-block `blockPrompts[type].statePrompt` joined). The request texts are
  shared with the d3 models — they live in
  `resources/profiles/requests/decision-requests.json` (ADR 182).
- **One question**, `match` (noul), built by the app from the profile's
  `question`. The `has_text`/`readable` noul gates and the `diagnosis`
  choice stay out of the profile; the parser still reads all five answers,
  so a profile may ship them again without code changes.
- **No criteria** — measured, they do not improve separation (docs/17, §6).
- Instruction texts are fixed (English); the content stays in the document
  language, which the state names through a `{{LANGUAGE}}` placeholder the
  app fills in.
- The image is a PNG data-URI of the block crop at page resolution (300 DPI
  for PDFs), exactly what the app sends.

Response:

```json
{
  "model": "llocr-decision",
  "answers": {
    "match": { "type": "noul", "noul": 0.83 }
  },
  "usage": { "input_tokens": 902, "output_tokens": 0 }
}
```

Routing: `Ok` when `match ≥ threshold`, else `Mismatch` (ADR 183). A crop
clef cannot read scores low and lands on **Review** in the fix task, which
answers REVIEW for an unreadable, clipped or empty crop.

## 4b. The measured v5 baseline

The scheme that closed formulas before v6 (docs/17, measured 13–14 Oct
2026): structured state + **one** noul question `match`, verdict `Ok` when
`noul ≥ decision/matchThreshold`. The 18 Oct 2026 corpus run of the app
(clef at 0.7, v5): FN 51 % / FP 20 % overall, FP 37 % on formulas —
dominated by `\tag{n}`, `\mathrm`, `\left(...\right)` being read as content.
That finding (and the unification with d3) is what motivated v6 here.

## 5. Calibration (measured 22 Oct 2026, iteration 7)

The request texts are shared by every decision model (ADR 182), so rewriting
the request moves each model's p(match) distribution — clef's thresholds were
re-measured on the run that matches the shipped request (ADR 184). The v5
measurement below is kept for the history; it described a request that no
longer exists.

Corpus: `OCRtest/decision_models/test 2/iteration7/OCR_TEST_clef-flash.llocr`
— 177 blocks (73 with errors, 104 clean), the same scope as the app's own
runs. The verdict is a pure threshold over the stored P(match) (ADR 173), so
the operating point is read off the FN/FP(t) curve, no new run needed.

The rewritten request cost clef its separation:

| block class | AUC (iteration 4, old request) | AUC (iteration 7, shipped request) |
|---|---|---|
| formulas | 0.782 | 0.715 |
| tables | 0.889 | 0.722 |
| text with formulas | 0.870 | 0.821 |
| plain text | 0.860 | 0.479 |
| service blocks | 0.978 | 1.000 |

At the old thresholds the new run gives FN 18 / FP 32 — against FN 11 / FP 36
of iteration 4. The per-block thresholds are the knees of the new curves, the
last step that buys a caught error for at most one false alarm:

| block type | threshold | FN / FP there |
|---|---|---|
| text | 0.80 | 12 / 15 |
| equation, formula | 0.75 | 10 / 6 |
| table | 0.50 | 0 / 2 |
| table_caption | 0.80 | 0 / 0 |
| table_footnote | 0.75 | 0 / 0 |
| title | 0.85 | 0 / 1 |
| list, image_caption | 0.75 | not calibrated (one block each) |

**FN 22 / FP 25** in total. `scripts/decision-eval/pick-thresholds.py` picks
the thresholds; the rule is checked against d3-mini, whose profile it
reproduces exactly (FN 12 / FP 22).

The plain-text AUC of 0.479 is the sharpest symptom: on running text clef now
answers worse than chance. The wording is shared (ADR 182), so this is the
price clef pays for the wording written on d3-mini — undoing it needs a
separate wording for clef.

### The v5 measurement (13–14 Oct 2026, docs/17)

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
