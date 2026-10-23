# Models: Qwen3.5-4B / 9B (fix)

> Manual launch, request contract and app wiring for the fix-family
> default. Russian version: `docs/26-qwen3.5.ru.md`. The correction
> protocol itself: ADR 163, ADR 192 and `docs/todo/fix_models_improvements.md`;
> the other fix family (Gemma 4 QAT): `docs/16-gemma-4.md`.

## 1. What they are

**Qwen3.5-4B** and **Qwen3.5-9B** (unsloth -MTP packaging) are general
purpose instruct VLMs serving the **fix** task: they receive a block crop
plus the OCR candidate framed as data and answer with the three-token
contract **OK / FIX / REVIEW**. A FIX carries the **complete corrected
block** and nothing else; the gate decides whether that block may replace the
candidate (the `full_block` protocol, ADR 192 — it supersedes the ADR 180
`span_hybrid` span edits, which the small models could not carry) and keeps
the candidate's own bytes when the corrected block is only a re-spelling of it
(ADR 181). The 4B is the task's default model
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

The per-type prompts (`blockPrompts`, 15 types; `formula`/`reference` ride
the `blockAliases` hop, and the caption and footnote types are merged) pin
what a FIX may change per block class — and name the output mode: the
complete corrected block. They
live in the shared request profile
`resources/profiles/requests/fix-requests.json` (ADR 182), which every
fix model names — the model profiles keep the parameters only. Reply
parsing (`FixModel` + `FixDiffGate`):
`OK` → Ok, `REVIEW` → Review, `FIX` → the complete corrected block, which
replaces the candidate unless the gate finds it equivalent to it — an
equivalent block reads as Ok and the candidate's bytes stay (the model
re-spelled it, changed nothing). An empty reply, a `FIX none` and a reply cut
by the token budget (`finish_reason: length`) land on Review — the reason
lands in the footer. A reply that is none of the three verdicts — an old
`EMPTY`, a free-text remark — is a deviation and lands on
Review too (ADR 181).

Sampling: `temperature 0`, neutral penalties, `stream false`,
`cache_prompt false` — the flag is a no-op here: hybrid recurrent memory
cannot be rewound, so llama-server force-disables prompt caching and
re-processes the full prompt of every request regardless (§7).
`stop`
(`["<|im_end|>", "<|endoftext|>", "</CANDIDATE>"]`) ends the reply at the
verdict. `maxOutputByBlock` budgets the reply per block type (a page
number 64 tokens, a formula 2048, a table 8192) — the block-level value
overrides the request profile's `max_tokens`, and it has to fit the whole
block, because the whole block travels in the reply (ADR 192). The budgets
are the protocol's rule, so they live in the shared request profile
(`fix-requests.json`) — one copy for all four fix models; the model profile
keeps the parameters only. Formula candidates travel in
canonical LaTeX (`FixDiffGate::normalizeLatexCandidate`), so the spaced
LaTeX of the OCR models does not read as churn.

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

- Qwen3.5 logs `non-consecutive token position` warnings on every M-RoPE
  image batch: image tokens share one scalar position and the recurrent
  cache's `find_slot` expects strictly advancing ones (upstream bug
  #28166, open). The decoder continues correctly and no work is
  repeated — the flood is noise, and `LlamaServerProcess` filters these
  lines out of the log window.
- Related, and the actual per-block cost: prompt caching is impossible
  for hybrid-recurrent models (the recurrent state cannot be rewound),
  so every fix request re-processes its full prompt — text, the
  1024-token image and all — no matter what `cache_prompt` says. The
  warnings are not the latency; the forced re-prefill is.
- A restart of the managed server brings back the role it was serving
  (stage-8 regression `restartBringsBackTheStartedRole`) — do not "fix"
  a role flip by re-picking the model.
- The 9B needs the same launch set as the 4B; only the memory budget
  differs (the wizard's auto-selection steps down quants to fit).
