# 15. Model manual: Gemma 4 E4B / 12B QAT (fix)

Self-contained manual for the two Gemma 4 QAT models: what they serve, what
to download, how to launch `llama-server`, how the request is composed. Every
value is traceable to the model profiles
(`resources/profiles/models/gemma-4-e4b.json` / `gemma-4-12b.json`),
`resources/profiles/serverLaunch.json`,
`resources/profiles/requests/fix-requests.json` or the named source file.
Russian mirror: [`15-model-gemma-4.ru.md`](15-model-gemma-4.ru.md).

## 1. What the models are and which task they serve

- **Gemma 4** (Google DeepMind), **QAT** checkpoints repackaged by Unsloth —
  quantization-aware-trained int4 weights keeping (near-)bfloat16 quality at
  ~72% less memory. License: Apache-2.0 (Gemma terms). Multimodal (text +
  image), 140+ languages, 128K (E4B) / 256K (12B) native context — the server
  still runs with the small `ctx-size` of §5.
- **E4B** — 4.5B effective parameters (8B with Per-Layer Embeddings),
  dedicated vision encoder (~150M). **12B Unified** — 11.95B, "encoder-free":
  image patches go into the decoder directly; llama.cpp still loads the
  projector as a separate `mmproj` file. Card vision benchmark (OmniDocBench
  1.5, avg edit distance): E4B **0.181**, 12B **0.164**.
- Task: **fix** (`roles.fix`, alias `llocr-fix`): one block crop plus the OCR
  candidate in, **OK / FIX / REVIEW** out over the `full_block` protocol
  (ADR 192) — `OK` keeps the candidate, `FIX` carries the **complete
  corrected block**, `REVIEW` hands the block to human eyes. The same model
  serves the **read** task («Перераспознать», ADR 259) with the read
  protocol's own prompts and a FIX/REVIEW contract (ADR 260/266).
  Transcription and the decision task are other models' jobs (ADR 252/254).

## 2. Files and download

Repositories (`files.repo` in the profiles; `minBuild` **b9600** — the first
llama.cpp line with Gemma4 MTP support and its early fixes):

| Repo | File | Quant id | Size | Purpose |
| --- | --- | --- | --- | --- |
| [`unsloth/gemma-4-E4B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF) | `gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf` | q4_k_xl | 3.9 GiB | main quant (Unsloth Dynamic) |
| 〃 | `gemma-4-E4B-it-qat-UD-Q2_K_XL.gguf` | q2_k_xl | 3.0 GiB | mobile mixture quant |
| 〃 | `mmproj-F16.gguf` | f16 | 944 MiB | vision projector — **mandatory**, without it the model sees no image |
| 〃 | `mtp-gemma-4-E4B-it.gguf` | q4_0 | — | **MTP drafter** |
| [`unsloth/gemma-4-12B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF) | `gemma-4-12B-it-qat-UD-Q4_K_XL.gguf` | q4_k_xl | 6.3 GiB | the repo's single quant (higher precisions degrade accuracy, per the packager) |
| 〃 | `mmproj-F16.gguf` | f16 | 167 MiB | vision projector — **mandatory** |
| 〃 | `mtp-gemma-4-12B-it.gguf` | q4_0 | — | MTP drafter |

- The MTP drafter sits at the repo root as a near-lossless "smart Q4_0" (the
  repo's `MTP/` folder holds other precisions). LLocr installs explicitly —
  the profile declares the file (`files.mtp`), the install downloads it from
  the same repo and records it as the role's `launch/draftPath`
  (passed as `--model-draft`).
- The app lays a repo out as `<modelsDir>/<org>__<repo>/…` — e.g.
  `<modelsDir>/unsloth__gemma-4-E4B-it-qat-GGUF/…`
  (`ModelInstallTransaction::repoDirName`).
- The memory estimator reads `gemma4.*` GGUF metadata keys, so the wizard's
  memory warning reflects the real layer/head counts.

## 3. Launching llama-server

One concrete command — Gemma 4 **E4B** on **Windows · CUDA** (the `win-cuda`
platform profile), exactly as `ServerLaunchConfig::toArguments`
(`src/runtime/ServerLaunchConfig.cpp`) builds it — core fields first, then
the composed parameters in order:

```bat
llama-server.exe ^
  --model C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf ^
  --mmproj C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\mmproj-F16.gguf ^
  --model-draft C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\mtp-gemma-4-E4B-it.gguf ^
  --alias llocr-fix --host 127.0.0.1 --port 8080 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn off --ctx-size 16384 ^
  --spec-type draft-mtp --spec-draft-n-max 4 --temp 0 ^
  --jinja --reasoning-budget 0 --image-min-tokens 1120
```

Layer legend: **policy** = `serverLaunch.json → policy.parameters`,
**platform** = `serverLaunch.json → profiles[win-cuda]`, **model** =
`gemma-4-e4b.json → roles.fix.launch`, **core** = the settings fields
`ServerLaunchConfig` owns. `LaunchProfileStore::compose` merges the layers:
a model entry replaces a same-named platform/policy one (`flash-attn off`,
`parallel`, `no-warmup`), the rest is appended and renumbered.

| Flag | Value | Layer | Why |
| --- | --- | --- | --- |
| `--model` / `--mmproj` / `--model-draft` | paths | core | weights, projector, drafter (the draft path from the role's install record) |
| `--alias` | `llocr-fix` | core | the role's `alias`; the request's `model` field routes on it |
| `--host` / `--port` | `127.0.0.1` / pick one | core | settings defaults (`127.0.0.1`, `0` = auto-pick); a manual command must name a port |
| `--parallel` | `1` | policy (restated in model) | a single decoding slot; MTP drafting is single-stream |
| `--cache-reuse` | `0` | policy | context reuse between requests disabled |
| `--no-context-shift` | flag | policy | keep the context stable; no KV shift on overflow |
| `--no-warmup` | flag | policy (restated in model) | skip the warmup run at load (faster start, slower first request) |
| `--verbosity` | `3` | policy | server log level |
| `--n-gpu-layers` | `99` | platform | all layers on the GPU |
| `--flash-attn` | `off` | **model (E4B)** | the card's E4B command runs `-fa off`; replaces the platform's `on` |
| `--ctx-size` | `16384` | platform | prompt + image + output window (8192 on the CPU profile) |
| `--spec-type` | `draft-mtp` | model | speculative decoding with the repo's MTP drafter |
| `--spec-draft-n-max` | `4` | model | draft tokens per step (card value; the packager suggests trying 1–6) |
| `--temp` | `0` | model | server-side greedy decoding |
| `--jinja` | flag | model | the GGUF chat template parses Gemma 4's thought channel — the request's thinking mode decides whether the model reasons first |
| `--reasoning-budget` | `0` | model | server default: no thinking unless a request asks (the request overrides) |
| `--image-min-tokens` | `1120` | model | Gemma 4 quantizes an image to one of five budgets **70/140/280/560/1120**; the card recommends the top budgets for OCR and small text |

**12B differences**: no `flash-attn` entry in its launch list — the platform
value stands (`on` on Metal/CUDA, `off` on Vulkan/CPU, matching the card's
`-fa on`) — plus one appended flag **`--ubatch-size 2048`**: the 12B decodes
image patches into the main model with non-causal attention, which cannot be
split across ubatches; with the server default 512 a block crop upscaled to
the 1120-token budget lands just above the cap (the resize rounds up to the
patch grid) and aborts the server on a GGML assert. The E4B encodes images in
its own projector on the causal path and keeps the default.

## 4. Composing the request

Transport: `FixModel` over `VisionChatModel` (`src/models/`),
`POST /v1/chat/completions`, one request per block, `stream: false`. The
system prompt and per-block prompt come from the shared request profile
`fix-requests.json` (the role names it via `promptProfile`; the merge is a
fallback — a role's own wording would win, ADR 182/267). The image is the
block crop as a PNG data URL; the candidate is the block's effective text, a
formula already in canonical LaTeX (ADR 194).

**Fix protocol** (`full_block`) — the literal body for a `text` block,
thinking off (the two long strings are verbatim in
`resources/profiles/requests/fix-requests.json`, keys `systemPrompt` and
`blockPrompts.text`):

```json
{
  "model": "llocr-fix",
  "messages": [
    { "role": "system", "content": "<fix-requests.json → systemPrompt, verbatim>" },
    { "role": "user", "content": [
      { "type": "text", "text": "<fix-requests.json → blockPrompts.text, verbatim>" },
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64 of the block crop>" } },
      { "type": "text", "text": "OCR candidate:\n<CANDIDATE>\nLemma 2 holds for every n ≥ 1.\n</CANDIDATE>" }
    ] }
  ],
  "chat_template_kwargs": { "enable_thinking": false },
  "temperature": 0.0,
  "repeat_penalty": 1.0,
  "presence_penalty": 0.0,
  "frequency_penalty": 0.0,
  "stream": false,
  "cache_prompt": false,
  "stop": ["</CANDIDATE>"],
  "max_tokens": 2048,
  "reasoning_budget_tokens": 0
}
```

Wire order of the user parts: `[type prompt, image, candidate]` — the role's
`promptBeforeImage` flag (`FixModel::buildRequestBody`) defaults to `false`
and the gemma profiles do not set it, so the candidate follows the image
(`FixModel` keeps the type prompt first in both flag states).

Top-level fields (`VisionChatModel::buildRoot`): `model` = the resolved model
id — the managed server's alias `llocr-fix`, or the external server's model
name; `messages` as above; then **every** profile request parameter as a
top-level JSON field, plus two injected ones:

| Field | Value | Source |
| --- | --- | --- |
| `temperature` | `0.0` | role `request` — greedy for a correction protocol (the card's chat sampling is for conversation) |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | `1.0` / `0.0` / `0.0` | role `request` — neutral |
| `stream` | `false` | role `request` — the parser consumes one complete response |
| `cache_prompt` | `false` | role `request` — block requests stay independent of the draft's shared KV cache |
| `stop` | `["</CANDIDATE>"]` | role `request` — ends the reply at the verdict; only the protocol marker belongs here (naming the end-of-text token would cut the answer to nothing) |
| `max_tokens` | per block, §5 | role `maxOutput` 8192 as the default (`requestWithMaxOutput`); overridden per block type from `fix-requests.json → maxOutputByBlock` (`FixController::fixBlock`) |
| `reasoning_budget_tokens` | `0` / 512 / 1024 / 1536 | injected by `applyReasoningBudget` from the global settings (`check/fixReasoningBudgetTokens` fix, `check/readReasoningBudgetTokens` read; default off) |
| `chat_template_kwargs.enable_thinking` | `true` iff `reasoning_budget_tokens` > 0 | `buildRoot` — the budget is also the switch (ADR 193) |

**Read protocol** («Перераспознать», `FixProtocol::ReadBlock`) — same body
except: prompts come from `resources/profiles/requests/read-requests.json`
(a fix role may override with its own `readSystemPrompt`/`readBlockPrompts`;
the gemma profiles do not), `max_tokens` from the read profile's
`maxOutputByBlock`, the reasoning budget is the read budget — and there is
**no candidate part**: the user content is only `[type prompt, image]`; the
recognized text never travels (ADR 266), the reader writes the block from the
image alone.

**The reply**: `choices[0].message.content` (a thought, when enabled, arrives
separately in `message.reasoning_content` and never reaches the parser;
`--jinja` makes llama.cpp route it there). After stripping service tokens and
an outer code fence, the verdict is the first token: `OK` keeps the block's
bytes; `FIX` + newline + **the complete corrected block** goes through the
diff gate (`FixDiffGate::fixBlock`, per-block equivalence rules of ADR 183),
which stores the correction in `box.correctedText` or keeps the candidate's
own bytes when the correction is only a re-spelling of it (verdict Ok);
`REVIEW` hands the block to human eyes. Degradations to Review: a `FIX`
keyword with an empty payload; a reply cut by the budget
(`finish_reason: "length"` — a fragment is not a correction); a reply outside
the three verdicts (an old `EMPTY`, a free-text remark) is a deviation.

**Thinking budget handling** (`FixModel::check`, ADR 193/230 behaviour): a
reply carrying `reasoning_content` but an empty `content` — the model spent
the whole budget thinking — is retried **once without the thinking**
(`reasoning_budget_tokens: 0`, `max_tokens` reduced by the thinking's
amount), with a footer note; the server is remembered and later requests skip
the thinking from the start.

## 5. Budgets

| Quantity | Value | Source |
| --- | --- | --- |
| Server context (`ctx-size`) | 16384 (GPU: Metal/CUDA) / 8192 (CPU) | `serverLaunch.json` platform profiles |
| `max_tokens` default | 8192 | role `maxOutput` |
| `max_tokens` per block | page_number 64, header/footer 256, seal/title/key_value 512, caption/footnote/ref_text 1024, text/abstract/list 2048, code/equation 4096, table 16384 | `fix-requests.json → maxOutputByBlock` (aliases: formula→equation, reference→ref_text, image/table_caption→caption, figure/table_footnote→footnote); a type without an entry keeps the 8192 default. The whole block travels in the reply, so the budget must fit it |
| Reasoning budget | off / 512 / 1024 / 1536 | global settings `check/fixReasoningBudgetTokens` (fix) and `check/readReasoningBudgetTokens` (read), default off — injected per request, no server restart (ADR 268) |
| Image token floor | 1120 | `--image-min-tokens` (§3) |

How the request budget is computed (`src/core/RequestBudget.cpp`,
`FixController::fixBlock`): the prompt is estimated as
`imageTokens + cyrillicChars/2 + otherChars/4` over system + type prompt +
candidate; the room is `ctx-size − promptTokens`; the **answer keeps its room
first**, the **thinking yields before the answer**, and the request carries
`max_tokens = answer + reasoning` with the clipped reasoning. When the chosen
thinking does not fit, it shrinks and the footer says to what.

## 6. Known limitations

- **E4B runs flash attention off** (the card's own command); the 12B wants it
  on — the platform layer's default, which the 12B profile does not override.
  **12B needs `--ubatch-size 2048`** (§3): with the default 512 the first
  image decode aborts the server (`non-causal attention requires
  n_ubatch >= n_tokens`).
- A thought cut in the middle (a small budget) sometimes runs into the answer
  itself — a tail of explanation follows the corrected block. The off mode
  stays the default; pick the budget large enough for the thought to fit.
- MTP drafting is single-stream (`--parallel 1`); `cache_prompt` stays off so
  block requests stay independent of the draft's shared KV cache. Earlier
  llama.cpp builds lack Gemma4 MTP (`minBuild` b9600).
- Measured on llama.cpp b11799 (model `gemma-4-E4B-it-qat-UD-Q4_K_XL`,
  server `--jinja --reasoning-budget 0`): the gemma-4 template knows the
  `enable_thinking` switch and the thought channel, the budget rides the
  request and cuts the thought at the wanted token, budget 0 produces no
  thinking. The 12B emits an *empty* thought block when thinking is off;
  `--jinja` parses the markers out, so `content` arrives clean either way.
- A restart of the managed server brings back the role it was serving
  (`restartBringsBackTheStartedRole`) — do not "fix" a role flip by re-picking
  the model.

## 7. Where things live and links

Code: model profiles `resources/profiles/models/gemma-4-e4b.json` /
`gemma-4-12b.json`; request texts `resources/profiles/requests/fix-requests.json`
/ `read-requests.json`; `VisionChatModel::buildRoot`, `FixModel::buildRequestBody`
/ `parseResponse`, `FixDiffGate::fixBlock` (`src/models/`);
`ServerLaunchConfig::toArguments` + `LaunchProfileStore::compose`
(`src/runtime/`); `src/core/RequestBudget.cpp` + `FixController::fixBlock`
(`src/app/`). Docs: `docs/09-tasks-and-verification.md` (the task vocabulary
and pipeline), `docs/18-memory-estimate.md` (VRAM), `docs/16-model-qwen3.5.md`
(the other fix family).

- E4B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF>
- 12B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF>
- Unsloth «How to Run Gemma 4 QAT»: <https://unsloth.ai/docs/models/gemma-4/qat>
- Unsloth MTP guide (drafter precisions, `--spec-*` flags):
  <https://unsloth.ai/docs/models/mtp>
- llama.cpp: Gemma4 MTP #23398, E2B/E4B assistants #24282
- Gemma family and license: <https://ai.google.dev/gemma/docs/core>
- Russian mirror: [`15-model-gemma-4.ru.md`](15-model-gemma-4.ru.md)

Design decisions: ADR 88 (the profiles are the whole contract), 126 (launch
layers), 162–164 (the fix task, two-layer verification), 183 (per-block
equivalence), 192 (`full_block`), 259/260/266 (the read protocol).
