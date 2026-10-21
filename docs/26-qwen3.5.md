# Models: Qwen3.5-4B / 9B (fix)

> Manual launch, request contract and app wiring for the fix-family
> default. Russian version: `docs/26-qwen3.5.ru.md`. The correction
> protocol itself: ADR 163, ADR 180 and `docs/todo/fix_models_improvements.md`;
> the other fix family (Gemma 4 QAT): `docs/16-gemma-4.md`.

## 1. What they are

**Qwen3.5-4B** and **Qwen3.5-9B** (unsloth -MTP packaging) are general
purpose instruct VLMs serving the **fix** task: they receive a block crop
plus the OCR candidate framed as data and answer with the three-token
contract **OK / FIX / REVIEW**. A FIX carries span edits
(`old_substring => new_substring` lines) for text blocks, the complete
corrected formula for a formula block and a full corrected block with a
mandatory `EDITS:` section for a table; the deterministic diff gate applies
the edits to the candidate itself (the `span_hybrid` protocol, ADR 180 — it
supersedes the ADR 163 full-block reply) and reads a formula's complete
block as the correction (ADR 181). The 4B is the task's default model
(ADR 169); the 9B is the same recipe at a larger size. Apache-2.0 licensed.

## 2. Files

| file | size | purpose |
|---|---|---|
| `Qwen3.5-4B-Q8_0.gguf` (…Q6_K/Q5_K_M/UD-Q4_K_XL/IQ4_XS) | 4.3 GB (Q8_0) | 4B, the fix default |
| `Qwen3.5-9B-Q8_0.gguf` (…Q6_K/Q5_K_M/UD-Q4_K_XL/IQ4_XS) | 9.2 GB (Q8_0) | 9B |
| `mmproj-F16.gguf` | ~0.9 GB | vision projector, **mandatory** |

- Repositories: `unsloth/Qwen3.5-4B-MTP-GGUF`, `unsloth/Qwen3.5-9B-MTP-GGUF`
  (profiles `resources/profiles/models/qwen3.5-4b.json`, `qwen3.5-9b.json`).
- The MTP drafting head ships inside the -MTP weights — no separate draft
  download (unlike the LFM DSpark or Gemma 4 repo drafter).

## 3. Launch

```
llama-server \
  --model <modelsDir>/unsloth__Qwen3.5-4B-MTP-GGUF/Qwen3.5-4B-Q8_0.gguf \
  --mmproj <modelsDir>/unsloth__Qwen3.5-4B-MTP-GGUF/mmproj-F16.gguf \
  --alias llocr-fix \
  --host 127.0.0.1 --port <auto> \
  --spec-type draft-mtp --spec-draft-n-max 4 --special --temp 0 \
  --parallel 1 --no-warmup --jinja --reasoning-budget 0 \
  --flash-attn on --image-min-tokens 1024 \
  --n-gpu-layers 99 \
  --cache-reuse 0 --no-context-shift
```

- **`--spec-type draft-mtp --spec-draft-n-max 4`** — speculative decoding
  with the MTP head inside the weights; 4 is what the head was trained
  for.
- **`--special`** — the reply may carry special vocabulary; the app's
  parser strips the service markers (`stripControlTokens`).
- **`--temp 0`** — server-side greedy; corrections must be deterministic.
- **`--jinja --reasoning-budget 0`** — the GGUF's chat template with
  thinking disabled: the reply is the verdict only.
- **`--parallel 1`** — a single decoding slot; MTP drafting is
  single-stream.
- **`--image-min-tokens 1024`** — llama.cpp warns Qwen-VL needs at least
  1024 image tokens for grounding tasks.

## 4. Request contract

One chat request per block (transport: `FixModel` over
`/v1/chat/completions`). Part order: system prompt (the three-token
contract plus the data-not-instructions framing and the document language),
then
`[type prompt, ocr candidate, image]`. The candidate travels framed:

```
OCR candidate:
<CANDIDATE>
…recognized text…
</CANDIDATE>
```

The per-type prompts (`blockPrompts`, 17 types; `formula`/`reference` ride
the `blockAliases` hop) pin what a FIX may change per block class — and
name the output mode: span edits for the text classes, the complete
formula for `equation`, the full block plus `EDITS:` for `table`. They
live in the shared request profile
`resources/profiles/requests/fix-requests.json` (ADR 182), which every
fix model names — the model profiles keep the parameters only. Reply
parsing (`FixModel` + `FixDiffGate`):
`OK` → Ok, `REVIEW` → Review, a span `FIX` → Fixed with
the candidate after the edits, a formula `FIX` → Fixed with the complete
formula, a structural `FIX` → Fixed when every change
is declared under `EDITS:`. The gate matches anchors in an equivalence view
(the model drops `\(…\)` carrier markup and collapses spacing when it
copies — the match maps back to the real span); a no-op fix, a missing or
ambiguous anchor and an undeclared diff demote to Review — the reason lands
in the footer. A reply that is none of the three verdicts — an old
`EMPTY`, a free-text remark, a `FIX none` — is a deviation and lands on
Review too (ADR 181).

Sampling: `temperature 0`, neutral penalties, `stream false`,
`cache_prompt false` — false avoids the recurring-state position warnings
Qwen3.5 logs when requests share a prefix. `stop`
(`["<|im_end|>", "<|endoftext|>", "</CANDIDATE>"]`) ends the reply at the
verdict. `maxOutputByBlock` budgets the reply per block type (a page
number 64 tokens, a formula 1024, a table 8192) — the block-level value
overrides the request profile's `max_tokens`. Formula candidates travel in
canonical LaTeX (`FixDiffGate::normalizeLatexCandidate`), and the type
answers the span contract — the full block with `EDITS:` is tables-only
now (ADR 180).

## 5. Verification notes

The fix output lands in the block's second text layer (`correctedText`,
ADR 164) and is itself judged by the decision task on the next
«Проверить». No per-model calibration beyond that.

## 6. Settings in the app

- Task — **fix** («Модели» window): keys `tasks/fix/modelPath`,
  `tasks/fix/mmprojPath`; qwen3.5-4b is the task's default model.
- Installation via Settings → Models (HF repositories
  `unsloth/Qwen3.5-*-MTP-GGUF`) or the wizard's hardware step.
- «Исправить» in the header runs this task over the problem blocks of the
  scope (ADR 167).

## 7. Known limitations

- Qwen3.5 logs benign `non-consecutive token position` warnings with
  shared prefixes — the app pins `cache_prompt false` (§4) and the log
  filter; upstream bug #28166.
- A restart of the managed server brings back the role it was serving
  (stage-8 regression `restartBringsBackTheStartedRole`) — do not "fix"
  a role flip by re-picking the model.
- The 9B needs the same launch set as the 4B; only the memory budget
  differs (the wizard's auto-selection steps down quants to fit).
