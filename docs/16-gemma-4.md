# Gemma 4 E4B / 12B QAT — model manual

Working notes for the two Gemma 4 QAT models as they are used in LLocr: what
they are, where their GGUF comes from, how the MTP drafter and the vision
stack are launched, how the fix request is composed, and which
card recommendations LLocr follows verbatim. Everything here is from the model
cards, the Unsloth guides, or measured in this project.

A Russian mirror of this document lives in
[`16-gemma-4.ru.md`](16-gemma-4.ru.md).

## 1. What the models are

- **Gemma 4** (Google DeepMind), **QAT** checkpoints repackaged by Unsloth —
  quantization-aware-trained int4 weights that keep (near-)bfloat16 quality at
  ~72% less memory. License: Apache-2.0 (Gemma terms). Both models are
  multimodal (text + image; audio exists on the E-series but LLocr does not
  use it), 140+ languages, 128K (E4B) / 256K (12B) context.
- **Gemma 4 E4B** — 4.5B effective parameters (8B with the Per-Layer
  Embeddings), dedicated vision encoder (~150M). Runs on ~5 GB without MTP,
  6.5–7 GB with the drafter.
- **Gemma 4 12B Unified** — 11.95B, "encoder-free": in transformers the image
  patches go straight into the decoder through linear layers. llama.cpp still
  ships the projector as a separate `mmproj` file and loads it the usual way.
  Runs on ~7 GB without MTP, 8–9 GB with the drafter.
- Card vision benchmarks (OmniDocBench 1.5, average edit distance — lower is
  better): E4B **0.181**, 12B **0.164** (Gemma 3 27B: 0.365). The card names
  document parsing, OCR and handwriting recognition among the target uses.

LLocr uses both models in the **fix** task only (ADR 162/163) — universal
models that confirm / correct / refuse a recognized block candidate
(OK/FIX/REVIEW). Page OCR stays with the det-token models (`unlimited-ocr`,
`lfm25-vl-3b`), the decision task with clef-flash.

## 2. Files and repositories

| Repo | LLocr ships | Purpose |
| --- | --- | --- |
| [`unsloth/gemma-4-E4B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF) | `gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf` (q4_k_xl), `gemma-4-E4B-it-qat-UD-Q2_K_XL.gguf` (q2_k_xl), `mmproj-F16.gguf`, `mtp-gemma-4-E4B-it.gguf` | The main quant (Unsloth Dynamic, noticeably better than a naive Q4_0), the mobile mixture quant, the vision projector, and the MTP drafter. |
| [`unsloth/gemma-4-12B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF) | `gemma-4-12B-it-qat-UD-Q4_K_XL.gguf` (q4_k_xl), `mmproj-F16.gguf`, `mtp-gemma-4-12B-it.gguf` | Same shape; the repo carries a single quant ("only one GGUF per model" — higher precisions degrade accuracy per the packager). |

Notes:

- The **MTP drafter** sits at the repo root as a near-lossless "smart Q4_0"
  (`mtp-gemma-4-*.gguf`); the `MTP/` folder holds BF16/F16/Q4_0/Q8_0
  variants. llama.cpp's `-hf` flow auto-discovers the drafter; LLocr installs
  explicitly, so the profile declares the file and the install pipeline
  downloads it from the same repo and records it as the role's
  `launch/draftPath` (passed as `--model-draft`).
- `minBuild` is **b9600**: llama.cpp learned Gemma4 MTP on 2026-06-07/08
  (`llama : add Gemma4 MTP` #23398, `mtp: support for gemma-4 E2B and E4B
  assistants` #24282 ≈ b9570); b9600 is the first build a few days later, so
  the drafter path and its early fixes are in.
- The memory estimator reads `gemma4.*` GGUF metadata keys, so the wizard's
  memory warning reflects the real layer/head counts.

## 3. Launching llama-server

The cards' reference commands:

```sh
# E4B — flash attention OFF
llama-server -hf unsloth/gemma-4-E4B-it-qat-GGUF:UD-Q4_K_XL \
  --spec-type draft-mtp --spec-draft-n-max 4 -ngl 999 -fa off

# 12B — flash attention ON
llama-server -hf unsloth/gemma-4-12B-it-qat-GGUF:UD-Q4_K_XL \
  --spec-type draft-mtp --spec-draft-n-max 4 -ngl 999 -fa on
```

What LLocr actually starts (platform layer from `serverLaunch.json`, model
layer from the profiles):

```sh
llama-server \
  --model <…>/gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf \
  --mmproj <…>/mmproj-F16.gguf \
  --model-draft <…>/mtp-gemma-4-E4B-it.gguf \
  --alias llocr-verify --host 127.0.0.1 --port <n> \
  --n-gpu-layers 99 --ctx-size 16384 \
  --spec-type draft-mtp --spec-draft-n-max 4 --temp 0 --parallel 1 \
  --no-warmup --jinja --reasoning-budget 0 --flash-attn off \
  --image-min-tokens 1120 \
  --cache-reuse 0 --no-context-shift
```

Layer by layer:

- `--model / --mmproj / --model-draft / --alias / --host / --port` — core
  fields, single-sourced from the activation/install settings. The drafter
  replaces nothing the user configures: it is downloaded with the model.
- **Model layer** (`gemma-4-e4b.json` / `gemma-4-12b.json`):
  - `--spec-type draft-mtp --spec-draft-n-max 4` — the cards' server
    settings. The drafter shares the target's KV cache and does not change
    the output (the target verifies every drafted token); the packager
    measured 1.5–2.2× decode speedup and suggests trying 1–6 per machine.
  - `--temp 0 --parallel 1 --no-warmup` — server-side defaults for a
    single-stream transcription pipeline (same as the qwen profiles).
  - `--jinja` — the GGUF chat template parses Gemma 4's thought channel, so
    the request's thinking mode decides whether the model reasons first (§4).
  - `--reasoning-budget 0` — the server's default thinking budget: no thinking
    unless a request asks for it. The fix request carries the reasoning mode
    and overrides the default (ADR 193).
  - `--image-min-tokens 1120` — Gemma 4 quantizes each image to one of five
    visual token budgets: **70 / 140 / 280 / 560 / 1120**. The card says to
    use the higher budgets for OCR, document parsing and small text, so the
    profile pins the top budget (§ Best practices 5).
  - `--ubatch-size 2048` — **12B only**. The 12B is the *unified* build: the
    projector feeds image patches into the main model, and that decode runs
    with non-causal attention — a non-causal batch cannot be split across
    ubatches, the whole image must fit in one. With the server default 512
    llama-server caps the vision budget to 512 tokens, and any block crop
    upscaled up to the budget lands a few tokens *above* the cap (the resize
    rounds both sides up to the patch grid) — the first image decode aborts
    the server with `non-causal attention requires n_ubatch >= n_tokens`.
    2048 holds the pinned 1120-token budget plus the rounding overshoot. The
    E4B encodes images in its own projector on the causal path and keeps the
    default.
  - `--flash-attn off` — **E4B only**: the card's E4B command runs with
    flash attention off. The 12B card says the opposite (`-fa on`), which is
    what the platform layer already defaults to on Metal/CUDA — so the 12B
    profile does not override `flash-attn` at all and Vulkan/CPU keep the
    platform's `off`.
- Policy: `--parallel 1 --cache-reuse 0 --no-context-shift --no-warmup`
  (`parallel`/`no-warmup` are re-stated in the model layer because MTP
  drafting is single-stream; re-stating is how the qwen profiles do it).

## 4. Composing requests

### Message layout

- The Gemma 4 card's best practice: **image content before the text**.
  `GeneralPurposeModel` used to hard-code `[type prompt, image]`; it now
  follows the profile's `promptBeforeImage` flag, the same flag the OCR body
  already honored. Gemma 4 (and every Qwen-family model) gets
  `[image, type prompt]`; LFM2.5 keeps its trained `[type prompt, image]`.
- Thinking follows the request's mode: `chat_template_kwargs:
  {"enable_thinking": …}` is true exactly when the request carries a reasoning
  budget above zero (ADR 193) — the off mode keeps the old behaviour, and the
  thought, when it is on, arrives in `reasoning_content` while `content` holds
  the verdict and the corrected block. The 12B still emits an **empty** thought
  block when thinking is off (the E-series does not); `--jinja` makes llama.cpp
  parse the channel markers out, so `content` arrives clean either way.
- One block crop per request; the candidate text rides along framed by
  `<CANDIDATE>` (§ fix prompts).

### The fix prompts

The profile is based on `qwen3.5-9b.json` and reuses its prompt set for the
fix task (the `full_block` protocol, ADR 192): the verifier system prompt
(the image is the only source of truth; the three verdict tokens OK / FIX /
REVIEW, and a FIX that carries the complete corrected block) plus one check
list per block type. The system prompt names the document language through a
`{{LANGUAGE}}` placeholder the app fills in (ADR 181). The user
message orders the type prompt, the block image and the candidate framed by
`<CANDIDATE>…</CANDIDATE>`. The table prompt pins the same-serialization
contract — the corrected table answers in the candidate's own format
(HTML in, HTML out; Markdown in, Markdown out) — which both families need.

### Request parameters

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | 0.0 | Deterministic greedy decoding for the correction protocol. (The card's chat sampling — temp 1.0, top-p 0.95, top-k 64 — is for conversation, not for a verification pipeline.) |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | 1.0 / 0.0 / 0.0 | Neutral. |
| `max_tokens` | 8192 | Generous for a table crop; matches the qwen profiles. `maxOutputByBlock` budgets the reply per block type (a page number 64, a table 8192) — the budgets live in the shared request profile `fix-requests.json`, so all four fix models answer with the same ones (ADR 192). |
| `stop` | `["<end_of_turn>", "<eos>", "</CANDIDATE>"]` | Ends the reply at the verdict even when the model would keep generating. |
| `stream` | false | The parser consumes one complete response. |
| `cache_prompt` | false | Every block request stays independent of the draft's shared KV cache. |

The fix task's optional reasoning mode (ADR 193) **is** offered for the gemma
pair: the model profile carries `reasoning: true`, so the fix request carries a
thinking budget and the same four steps (off / low 512 / med 1024 / high 1536)
that the shared request profile offers.

Verified on llama.cpp b11799 (the model `gemma-4-E4B-it-qat-UD-Q4_K_XL`, the
server `--jinja --reasoning-budget 0`):

- the gemma-4 chat template knows the `enable_thinking` switch and the thought
  channel (`<|channel>thought`), so llama.cpp's autoparser moves the thinking
  into `message.reasoning_content` and leaves the verdict and the corrected
  block in `message.content`;
- the budget rides the request (`reasoning_budget_tokens`) and cuts the thought
  at the wanted token: a budget of 32 gave a ~32-token thought and the answer
  after it, a budget of 512 the full thought and the same answer;
- a budget of 0 (the off mode) produced no thinking at all — the answer as
  before.

The option had simply never been added to the gemma profiles; the model itself
is not limited. One practical caveat: a thought cut in the middle (a small
budget) sometimes runs into the answer itself — a tail with the explanation
follows the corrected block. The off mode therefore stays the default, and the
budget is picked large enough for the thought to fit.

## 5. Output formats

The fix protocol's three verdicts — `OK`, `FIX` + the corrected block, or
`REVIEW` — are mapped by `FixModel::parseResponse` and the diff gate onto the
correction layer: `OK` keeps the text, `FIX` stores the correction in
`correctedText`, `REVIEW` marks the block damaged; an empty `FIX` degrades to
`Review` (ADR 139/163), a reply outside the three verdicts degrades to
`Review` as a deviation (ADR 181).

## 6. Role in the verification pipeline

- **fix** — the task both profiles declare (ADR 162/163): after the decision
  model marks a block as mismatched (or on the manual «Fix» commands), the
  candidate and the crop go to the fix task; `OK` keeps the text, `FIX`
  stores the correction, `REVIEW` flags the block. The OCR text itself stays
  untouched for diff/revert (ADR 164).
- **pageOcr / blockRecognition** — not declared: transcription is the
  det-token models' job.
- **decision** — not applicable: clef-flash is the decision model
  (`docs/18-clef-flash.md`).

## 7. Where things live in LLocr

| Piece | Location |
| --- | --- |
| Model profiles (files, launch/request layers) | `resources/profiles/models/gemma-4-e4b.json`, `gemma-4-12b.json` |
| Fix request texts (shared with the other fix models) | `resources/profiles/requests/fix-requests.json` |
| Fix request/response, modality order | `GeneralPurposeModel::buildRequestBody` / `parseResponse` (`src/models/`) |
| Drafter download and `launch/draftPath` wiring | `ModelInstallTransaction`, `ModelInstaller`, `ServerLaunchConfig` |
| Memory estimate (gemma4 metadata keys) | `ModelMemoryEstimator` |
| Installer UI rows | `ModelQuantModel` (the fix task's list in «Модели по задачам»; management in «Модели») |

Design decisions: ADR 88 (models are data — the profiles are the whole
contract), 126 (launch layers: the machine owns `flash-attn`/`ctx-size`
unless the weights demand otherwise — the E4B's `-fa off` is such a demand),
145–147 (the two-phase pipeline), 162–164 (the fix task, the two-layer
verification).

## 8. Links

- E4B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF>
- 12B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF>
- Unsloth «How to Run Gemma 4 QAT» guide:
  <https://unsloth.ai/docs/models/gemma-4/qat>
- Unsloth MTP guide (drafter precisions, `--spec-*` flags):
  <https://unsloth.ai/docs/models/mtp>
- llama.cpp: Gemma4 MTP #23398, E2B/E4B assistants #24282
- Gemma 4 family and license: <https://ai.google.dev/gemma/docs/core>
