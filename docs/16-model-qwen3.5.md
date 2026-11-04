# 16. Model manual: Qwen3.5-4B / 9B (fix)

Self-contained manual for the Qwen3.5 pair — the fix task's default family —
as LLocr uses them: what they serve, what to download, how to launch
`llama-server` and how the request is composed, without reading the codebase
first. Every value is traceable to
`resources/profiles/models/qwen3.5-4b.json` / `qwen3.5-9b.json`,
`resources/profiles/serverLaunch.json`,
`resources/profiles/requests/fix-requests.json` or the named source file.
Russian mirror: [`16-model-qwen3.5.ru.md`](16-model-qwen3.5.ru.md).

## 1. What the models are and which task they serve

- **Qwen3.5-4B** and **Qwen3.5-9B** (Unsloth `-MTP` packaging) are general
  purpose instruct VLMs. Apache-2.0 licensed. The MTP drafting head ships
  **inside the -MTP weights** — no separate draft download (unlike Gemma 4,
  whose repo carries a drafter file).
- Task: **fix** (`roles.fix`; alias `llocr-fix`). The **4B is the task's
  default model** (`"default": true`, ADR 169); the 9B is the same recipe at
  a larger size. The fix task receives one block crop plus the OCR candidate
  and answers the three-verdict contract **OK / FIX / REVIEW** over the
  `full_block` protocol (ADR 192, which superseded the ADR 180 `span_hybrid`
  span contract the small models could not carry): `OK` keeps the candidate,
  `FIX` carries the **complete corrected block**, `REVIEW` hands the block to
  human eyes. The same model also serves the **read** task
  («Перераспознать», ADR 259) — same transport, the read protocol's own
  prompts and a FIX/REVIEW contract (ADR 260); the protocol is the action's
  choice, not the model's (ADR 266). Not declared for **blockRecognition**,
  **decision** or **layout**.

## 2. Files and download

Repositories (from `files.repo` in the profiles; `minBuild` **b4000**):

| Repo | File | Quant id | Size | Purpose |
| --- | --- | --- | --- | --- |
| [`unsloth/Qwen3.5-4B-MTP-GGUF`](https://huggingface.co/unsloth/Qwen3.5-4B-MTP-GGUF) | `Qwen3.5-4B-Q8_0.gguf` | q8_0 | 4.3 GiB | the fix default |
| 〃 | `Qwen3.5-4B-Q6_K.gguf` / `…-Q5_K_M.gguf` / `…-UD-Q4_K_XL.gguf` / `…-IQ4_XS.gguf` | q6_k / q5_k_m / q4_k_xl / iq4_xs | 3.4 / 3.0 / 2.8 / 2.5 GiB | smaller quants the memory budget may pick |
| 〃 | `mmproj-F16.gguf` | f16 | 641 MiB | vision projector — **mandatory**, without it the model sees no image |
| [`unsloth/Qwen3.5-9B-MTP-GGUF`](https://huggingface.co/unsloth/Qwen3.5-9B-MTP-GGUF) | `Qwen3.5-9B-Q8_0.gguf` (+ the same four quant ids) | q8_0 … | 9.1 GiB (Q8_0) … 5.1 GiB | the 9B |
| 〃 | `mmproj-F16.gguf` | f16 | 876 MiB | vision projector — **mandatory** |

- No `files.mtp` entry: speculative decoding uses the MTP head in the main
  GGUF; there is no `--model-draft` file. The app lays a repo out as
  `<modelsDir>/<org>__<repo>/…` — e.g.
  `<modelsDir>/unsloth__Qwen3.5-4B-MTP-GGUF/Qwen3.5-4B-Q8_0.gguf`
  (`ModelInstallTransaction::repoDirName`).

## 3. Launching llama-server

One concrete command — **Qwen3.5-4B** on **Windows · CUDA** (the `win-cuda`
platform profile), exactly as `ServerLaunchConfig::toArguments`
(`src/runtime/ServerLaunchConfig.cpp`) builds it: core fields first, then the
composed parameters in order:

```bat
llama-server.exe ^
  --model C:\LLocr\models\unsloth__Qwen3.5-4B-MTP-GGUF\Qwen3.5-4B-Q8_0.gguf ^
  --mmproj C:\LLocr\models\unsloth__Qwen3.5-4B-MTP-GGUF\mmproj-F16.gguf ^
  --alias llocr-fix --host 127.0.0.1 --port 8080 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 ^
  --spec-type draft-mtp --spec-draft-n-max 4 --special --temp 0 ^
  --jinja --reasoning-budget 0 --image-min-tokens 1024
```

Per-flag table (`layer` = where the value comes from): **policy** =
`serverLaunch.json → policy.parameters`, **platform** =
`serverLaunch.json → profiles[win-cuda]`, **model** =
`qwen3.5-4b.json → roles.fix.launch`, **core** = the settings fields
`ServerLaunchConfig` owns. The three layers are merged by
`LaunchProfileStore::compose` — a model-layer entry replaces a same-named
platform/policy entry (`parallel`, `no-warmup`, `flash-attn`), everything
else is appended and renumbered.

| Flag | Value | Layer | Why |
| --- | --- | --- | --- |
| `--model` / `--mmproj` | paths | core | weights + projector; **no `--model-draft`** — the MTP head is inside the weights |
| `--alias` | `llocr-fix` | core | the role's `alias`; the request's `model` field routes on it |
| `--host` / `--port` | `127.0.0.1` / pick one | core | settings defaults (`127.0.0.1`, `0` = auto-pick); a manual command must name a port |
| `--parallel` | `1` | policy (restated in model) | a single decoding slot; MTP drafting is single-stream |
| `--cache-reuse` | `0` | policy | context reuse between requests disabled |
| `--no-context-shift` | flag | policy | keep the context stable; no KV shift on overflow |
| `--no-warmup` | flag | policy (restated in model) | skip the warmup run at load |
| `--verbosity` | `3` | policy | server log level |
| `--n-gpu-layers` | `99` | platform | all layers on the GPU |
| `--flash-attn` | `on` | platform (restated in model) | flash attention kernels; required for the vision stack |
| `--ctx-size` | `16384` | platform | prompt + image + output window (8192 on the CPU profile) |
| `--spec-type` | `draft-mtp` | model | speculative decoding with the MTP head inside the weights |
| `--spec-draft-n-max` | `4` | model | draft tokens per step; 4 is what the head was trained for |
| `--special` | flag | model | emit special tokens in the server output; the app's parser strips the service markers (`stripControlTokens`) |
| `--temp` | `0` | model | server-side greedy; corrections must be deterministic |
| `--jinja` | flag | model | the GGUF's chat template (thinking is steered per request, §4) |
| `--reasoning-budget` | `0` | model | server default: no thinking unless a request asks (the request overrides) |
| `--image-min-tokens` | `1024` | model | llama.cpp warns Qwen-VL needs at least 1024 image tokens for grounding tasks |

The **9B needs the same launch set**; only the memory budget differs (the
wizard's auto-selection steps down quants to fit).

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

Wire order of the user parts: `[type prompt, image, candidate]`. It follows
the role's `promptBeforeImage` flag (`FixModel::buildRequestBody`); the flag
defaults to `false` and the qwen profiles do not set it, so the candidate
follows the image. (`FixModel` keeps the type prompt first in both flag
states; only the candidate/image order flips.)

Top-level fields (`VisionChatModel::buildRoot`): `model` = the resolved model
id — the managed server's alias `llocr-fix`, or the external server's model
name; `messages` as above; then **every** profile request parameter as a
top-level JSON field, plus two injected ones:

| Field | Value | Source |
| --- | --- | --- |
| `temperature` | `0.0` | role `request` — greedy for a correction protocol |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | `1.0` / `0.0` / `0.0` | role `request` — neutral |
| `stream` | `false` | role `request` — the parser consumes one complete response |
| `cache_prompt` | `false` | role `request` — a **no-op** for this family: hybrid recurrent memory cannot be rewound, so llama-server forces full prompt re-processing whatever this says. Kept off to make that explicit (§6) |
| `stop` | `["</CANDIDATE>"]` | role `request` — ends the reply at the verdict. Only the protocol marker belongs here: llama-server already stops at the end-of-text token, and naming that in the stop list cuts the answer to nothing and lands every block on Review |
| `max_tokens` | per block, §5 | role `maxOutput` 8192 as the default (`requestWithMaxOutput`); overridden per block type from `fix-requests.json → maxOutputByBlock` (`FixController::fixBlock`) |
| `reasoning_budget_tokens` | `0` / 512 / 1024 / 1536 | injected by `applyReasoningBudget` from the global settings (`check/fixReasoningBudgetTokens` for fix, `check/readReasoningBudgetTokens` for read; default off) |
| `chat_template_kwargs.enable_thinking` | `true` iff `reasoning_budget_tokens` > 0 | `buildRoot` — the budget is also the switch (ADR 193) |

**Read protocol** («Перераспознать», `FixProtocol::ReadBlock`) — same body
except: the system prompt and block prompt come from
`resources/profiles/requests/read-requests.json` (a fix role may override
with its own `readSystemPrompt`/`readBlockPrompts` — the qwen3.5 profiles do
not), `max_tokens` comes from `read-requests.json → maxOutputByBlock`, the
reasoning budget is the read budget — and there is **no candidate part**: the
user content is only `[type prompt, image]`. The recognized text of the block
never travels (ADR 266); the reader writes the block from the image alone.

**The reply**: `choices[0].message.content` (a thought, when enabled, arrives
separately in `message.reasoning_content` and never reaches the parser).
After stripping service tokens and an outer code fence, the verdict is the
first token:

- `OK` — the candidate matches the image; the block keeps its bytes.
- `FIX` + newline + **the complete corrected block** — the diff gate
  (`FixDiffGate::fixBlock`, per-block equivalence rules of ADR 183) stores the
  correction in `box.correctedText`, or keeps the candidate's own bytes when
  the correction is only a re-spelling of it (verdict Ok).
- `REVIEW` — the block needs human eyes. Degradations to Review: a `FIX`
  keyword with an empty payload; a reply cut by the budget
  (`finish_reason: "length"` — a fragment is not a correction); a reply
  outside the three verdicts (an old `EMPTY`, a free-text remark) is a
  deviation.

**Thinking budget handling** (`FixModel::check`, ADR 193/230 behaviour): a
reply carrying `reasoning_content` but an empty `content` — the model spent
the whole budget thinking — is retried **once without the thinking**
(`reasoning_budget_tokens: 0`, `max_tokens` reduced by the thinking's
amount), with a footer note; the server is remembered and later requests skip
the thinking from the start. The budget rides the request — llama.cpp reads
`reasoning_budget_tokens` per request, so the mode takes effect on the next
fix with no server restart. The measurement behind the mode: thinking 512
tokens lifted the fix index from 0.54 to 0.65 on the stand's problem page, at
the price of speed.

## 5. Budgets

| Quantity | Value | Source |
| --- | --- | --- |
| Server context (`ctx-size`) | 16384 (GPU: Metal/CUDA) / 8192 (CPU) | `serverLaunch.json` platform profiles |
| `max_tokens` default | 8192 | role `maxOutput` (`qwen3.5-4b.json` / `qwen3.5-9b.json`) |
| `max_tokens` per block | page_number 64, header/footer 256, seal/title/key_value 512, caption/footnote/ref_text 1024, text/abstract/list 2048, code/equation 4096, table 16384 | `fix-requests.json → maxOutputByBlock` (aliases: formula→equation, reference→ref_text, image/table_caption→caption, figure/table_footnote→footnote); a type without an entry keeps the 8192 default. The whole block travels in the reply, so the budget must fit it |
| Reasoning budget (`reasoning_budget_tokens`) | off / 512 / 1024 / 1536 | global settings `check/fixReasoningBudgetTokens` (fix) and `check/readReasoningBudgetTokens` (read), default off — injected per request, no server restart (ADR 268) |
| Image token floor | 1024 | `--image-min-tokens` (§3) |

How the request budget is computed (`src/core/RequestBudget.cpp`,
`FixController::fixBlock`): the prompt is estimated as
`imageTokens + cyrillicChars/2 + otherChars/4` over system + type prompt +
candidate; the room is `ctx-size − promptTokens`; the **answer keeps its room
first** (`effectiveAnswer = min(max_tokens, room)`) and the **thinking yields
before the answer** (`effectiveReasoning = min(budget, room − answer)`, a
correction matters more than a thought); the request carries
`max_tokens = answer + reasoning` and the clipped reasoning. When the chosen
thinking does not fit, it shrinks and the footer says to what.

## 6. Known limitations

- Qwen3.5 logs `non-consecutive token position` warnings on every M-RoPE
  image batch: image tokens share one scalar position and the recurrent
  cache's `find_slot` expects strictly advancing ones (upstream bug #28166,
  open). The decoder continues correctly and no work is repeated — the flood
  is noise, and `LlamaServerProcess` filters these lines out of the log
  window.
- The actual per-block cost: prompt caching is impossible for
  hybrid-recurrent models (the recurrent state cannot be rewound), so every
  fix request re-processes its full prompt — text, the 1024-token image and
  all — no matter what `cache_prompt` says. The warnings are not the latency;
  the forced re-prefill is.
- A restart of the managed server brings back the role it was serving
  (`restartBringsBackTheStartedRole`) — do not "fix" a role flip by re-picking
  the model.

## 7. Where things live and links

| Piece | Location |
| --- | --- |
| Model profiles | `resources/profiles/models/qwen3.5-4b.json`, `qwen3.5-9b.json` |
| Fix / read request texts (shared by all fix models) | `resources/profiles/requests/fix-requests.json`, `read-requests.json` |
| Request body and reply parsing | `VisionChatModel::buildRoot`, `FixModel::buildRequestBody` / `parseResponse`, gate `FixDiffGate::fixBlock` (`src/models/`) |
| Launch assembly and budget | `ServerLaunchConfig::toArguments` (`src/runtime/`), `LaunchProfileStore::compose`, `src/core/RequestBudget.cpp`, `FixController::fixBlock` (`src/app/`) |
| Task selection in the app | Settings → «Модели», the fix row (`tasks/fix/modelPath`, `tasks/fix/mmprojPath`) |
| Task vocabulary and pipeline | `docs/09-tasks-and-verification.md` |
| VRAM estimate and calibration | `docs/18-memory-estimate.md` |
| The other fix family | `docs/15-model-gemma-4.md` |

- 4B: <https://huggingface.co/unsloth/Qwen3.5-4B-MTP-GGUF>
- 9B: <https://huggingface.co/unsloth/Qwen3.5-9B-MTP-GGUF>
- Russian mirror: [`16-model-qwen3.5.ru.md`](16-model-qwen3.5.ru.md)

Design decisions: ADR 162–164 (the fix task, the two-layer verification),
169 (task defaults — the 4B), 180→192 (`span_hybrid` → `full_block`), 181
(equivalence, deviations), 182 (request texts in their own profiles),
193/268 (the reasoning budget), 259/260/266 (the read protocol).
