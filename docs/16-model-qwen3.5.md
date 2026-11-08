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
  choice, not the model's (ADR 266); the 4B ships its own measured read
  wording (§4). Not declared for **blockRecognition**,
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

**The 4B's measured fix prompts** (ADR 267; the prompt_ab stand,
`OCRtest/fix_models/prompt_ab.py`): the fix role ships its own system prompt
(one sentence) and block prompts; the block prompts state the task and the
OK/FIX/REVIEW contract twice — the doubling won the earlier fix stand. The
`equation` prompt is the measured exception, a single sentence:

> OK if the OCR candidate matches the image, FIX followed by the corrected
> LaTeX if not, REVIEW if the image is unreadable.

A long formula prompt fixates the model on the wording instead of the image
and the candidate: on initially-correct candidates it rewrites instead of
`OK`, on an unreadable crop it hallucinates a formula instead of `REVIEW`.
Measured on the 55 formulas of the OCR_TEST corpus (thinking 256, glyph
stage 32 px, the app's own wire — profile prompts, gated replies):

| prompt | correct → OK | unreadable → REVIEW | wrong: improved / spoiled |
| --- | --- | --- | --- |
| long, doubled (was) | 17/24 | 11/20 | 7 / 5 |
| one sentence (ships) | **20/24** | **16/20** | **7 / 2** |

Elaborate comparisons ("Compare the image with the candidate. OK — …") are
worse on both ends: the strict-comparison wording raises REVIEW on broken
crops but pushes the model to invent differences on correct candidates
(10/24 OK, 13 spoiled).

**The one-sentence shape does not transfer to the other types** (the
alltypes stand, 142 blocks of the ten non-formula labels of the same
corpus): the long per-type prompts win on every axis. Correct candidates
stay OK 65/71 under the shipped wording against 49/71 under per-type one
sentences and 44/71 under one shared sentence; on blocks with errors the
shipped wording improves 12 and spoils 2 against 9 and 1. Two measured
failure modes of the short wording:

- **Service types collapse.** A page number's FIX payload would equal the
  candidate, and the model refuses to write it: bare `FIX` with no body —
  the gate files it under Review (0/10 page numbers, 0/5 headers OK). An
  OK-first phrasing recovers only half (5/10, 2/5).
- **Candidate echo.** On long candidates (text, tables) the short wording
  makes the model quote the framed candidate — the reply's FIX body starts
  with `OCR candidate:\n<CANDIDATE>…`, which the diff gate stores verbatim
  (no echo strip in `FixDiffGate::fixBlock` yet): 7 of 26 fixed blocks
  under the typed wording, 14 of 32 under the shared one, 0 of 55 under
  the shipped prompts. A follow-up gate strip would make the wording safe;
  without it the long prompts are the only echo-free option.

Tables add their own finding: even the shipped table prompt does not hold
the candidate's OTSL serialization — all 9 corpus tables came back
re-serialized as HTML (which `BlockRender` passes through raw instead of
`formatOtslTable`). A "in the candidate's own format" clause keeps the
serialization but brings the echo along. Table wording needs its own
iteration together with the gate echo strip.
| `chat_template_kwargs.enable_thinking` | `true` iff `reasoning_budget_tokens` > 0 | `buildRoot` — the budget is also the switch (ADR 193) |

**Read protocol** («Перераспознать», `FixProtocol::ReadBlock`) — same body
except: the system prompt and block prompt come from the fix role's own
`readSystemPrompt` / `readBlockPrompts` when it ships them, else from
`resources/profiles/requests/read-requests.json` — the 4B ships its measured
read wording (below), the 9B takes the shared texts. `max_tokens` comes from
`read-requests.json → maxOutputByBlock`, the reasoning budget is the read
budget — and there is **no candidate part**: the
user content is only `[type prompt, image]`. The recognized text of the block
never travels (ADR 266); the reader writes the block from the image alone.

**The 4B's measured read optimum** (the manual read stand, `OCRtest/manual/`;
the settings the corpus run picked):

- **Doubled block prompt.** The winning wording states the task, the
  FIX/REVIEW contract and the escape twice: the full text repeats verbatim,
  separated by a blank line, and the separators ride as literal `\n`
  characters. The 4B ships it per block type in `roles.fix.readBlockPrompts`
  — all 15 canonical types; `readSystemPrompt` is `You are an OCR agent.` A
  type the profile omits would fall back to the shared reader.
- **Reasoning on, budget 256 tokens.** The read stand pins the thinking
  server-side (`--reasoning on --reasoning-budget 256`). In the app the
  budget rides the request (`reasoning_budget_tokens`, ADR 268) from
  `check/readReasoningBudgetTokens`, whose steps are 0 / 512 / 1024 / 1536 —
  the measured optimum sits below the smallest step.
- **Image scale: body-text letter height ≈ 32 px.** Read crops read best
  when the main text's letters are ~32 px tall — the scale the formula crop
  target enforces in-app (§5). At the 300-DPI crop's native ~18 px small
  print loses accents (an overline); at 32–33 px it reads cleanly.

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
| Formula crop letter height | 32 px for `equation`/`formula` | `roles.fix.cropGlyphHeightByBlock` (ADR 287) |
| Read image scale | body-text letter height ≈ 32 px | the read stand's measured optimum (§4); in-app only formula crops are scaled (`cropGlyphHeightByBlock`) |

The fix request's crop carries a glyph target (`cropGlyphHeightByBlock`,
ADR 287): before the image is encoded, the crop's letter height is measured
(the median of its letter-like connected components) and a crop with
smaller letters is stretched up to the target. The 32 px target is the
scale the read experiments measured: at the 300-DPI crop's ~18 px letters
small print loses accents (an overline), at 32–33 px it reads cleanly.
Only the formula labels are in the map — a table or a text block passes
untouched, and only upward scaling happens, so a crop already at the target
keeps its bytes. Both protocols (fix and read) share the model, so both get
the scale.

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
| Fix request texts (shared by all fix models); read texts (shared — the 4B overrides them in its profile) | `resources/profiles/requests/fix-requests.json`, `read-requests.json` |
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
