# Models: Qwen3.5-4B / 9B (fix)

> Manual launch, request contract and app wiring for the fix-family
> default. Russian version: `docs/26-qwen3.5.ru.md`. The correction
> protocol itself: ADR 163 and `docs/20-model-task-rework.md`; the other
> fix family (Gemma 4 QAT): `docs/16-gemma-4.md`.

## 1. What they are

**Qwen3.5-4B** and **Qwen3.5-9B** (unsloth -MTP packaging) are general
purpose instruct VLMs serving the **fix** task: they receive a block crop
plus the OCR candidate framed as data and answer with the three-token
contract **OK / FIX / REVIEW** (ADR 163). The 4B is the task's default
model (ADR 169); the 9B is the same recipe at a larger size. Apache-2.0
licensed.

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
- **`--special`** — the FIX payload may carry special vocabulary; the
  server strips it by default.
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
contract plus the data-not-instructions framing), then
`[type prompt, ocr candidate, image]`. The candidate travels framed:

```
OCR candidate:
<ocr_candidate>
…recognized text…
</ocr_candidate>
```

The per-type prompts (`blockPrompts`, 18 types) pin what a FIX may change
per block class — tables by cell position with rowspan/colspan, formulas
symbol-by-symbol without simplification, and so on. Reply parsing:
`OK` → Ok, `REVIEW` → Review, `FIX\n…` → Fixed with the payload; empty or
garbage replies are errors, never silent verdicts.

Sampling: `temperature 0`, neutral penalties, `stream false`,
`cache_prompt false` — false avoids the recurring-state position warnings
Qwen3.5 logs when requests share a prefix.

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
