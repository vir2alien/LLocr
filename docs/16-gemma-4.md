# Gemma 4 E4B / 12B QAT — model manual

Working notes for the two Gemma 4 QAT models as they are used in LLocr: what
they are, where their GGUF comes from, how the MTP drafter and the vision
stack are launched, how the block-recognition request is composed, and which
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

LLocr uses both models in the **blockRecognition** role only — phase 2 of the
verification pipeline (ADR 145–147). The page-OCR role stays with the
det-token models (`unlimited-ocr`, `lfm25-vl-3b`), the decision role with
d1-3B.

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
  --no-warmup --jinja --flash-attn off --image-min-tokens 1120 \
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
  - `--jinja` — the GGUF chat template parses Gemma 4's thought channel and
    applies the request's `enable_thinking=false` (§4).
  - `--image-min-tokens 1120` — Gemma 4 quantizes each image to one of five
    visual token budgets: **70 / 140 / 280 / 560 / 1120**. The card says to
    use the higher budgets for OCR, document parsing and small text, so the
    profile pins the top budget (§ Best practices 5).
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
- Thinking is disabled per request: `chat_template_kwargs:
  {"enable_thinking": false}` — the flag `GeneralPurposeModel` already sends
  on every block-recognition request, and the same switch the Gemma 4
  template documents. The 12B still emits an **empty** thought block when
  thinking is off (the E-series does not); `--jinja` makes llama.cpp parse
  the channel markers out, so `content` arrives clean either way.
- One block crop per request; the previously recognized text is not sent.

### The block-recognition prompts

The profile is based on `qwen3.5-9b.json` and reuses its prompt set: the
shared transcription system prompt (the image is the only source of truth,
the reply is the block text and nothing else) plus one wording per block
type. The table prompt pins the plain-markup contract — raw `<table>` HTML,
no Markdown fences, no `border`/`style`/`data-*` attributes — which both
families need.

### Request parameters

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | 0.0 | Deterministic greedy decoding for transcription. (The card's chat sampling — temp 1.0, top-p 0.95, top-k 64 — is for conversation, not for a verification pipeline.) |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | 1.0 / 0.0 / 0.0 | Neutral. |
| `max_tokens` | 8192 | Generous for a table crop; matches the qwen profiles. |
| `stream` | false | The parser consumes one complete response. |
| `cache_prompt` | false | Every block request stays independent of the draft's shared KV cache. |

## 5. Output formats

Plain text per block; tables as raw HTML; formulas as LaTeX — the reply is
the new block text verbatim (`GeneralPurposeModel::parseResponse` strips
control tokens and a whole-reply Markdown fence; an empty reply maps to
`Review`, not to an error).

## 6. Role in the verification pipeline

- **blockRecognition (phase 2)** — the role both profiles declare: after the
  decision model marks a block as mismatched (or on the manual «Recognize»
  commands), the crop is re-transcribed and the reply becomes the block text.
- **ocr** — not declared: the page-OCR path is the det-token models' job.
- **decision** — not applicable: d1-3B is the decision model
  (`docs/14-d1-3b.md`).

## 7. Where things live in LLocr

| Piece | Location |
| --- | --- |
| Model profiles (files, prompts, launch/request layers) | `resources/profiles/models/gemma-4-e4b.json`, `gemma-4-12b.json` |
| Block-recognition request/response, modality order | `GeneralPurposeModel::buildRequestBody` / `parseResponse` (`src/models/`) |
| Drafter download and `launch/draftPath` wiring | `ModelInstallTransaction`, `ModelInstaller`, `ServerLaunchConfig` |
| Memory estimate (gemma4 metadata keys) | `ModelMemoryEstimator` |
| Installer UI rows | `ModelQuantModel` (the block-recognition list; LFM2.5 stays first as the role's default) |

Design decisions: ADR 88 (models are data — the profiles are the whole
contract), 126 (launch layers: the machine owns `flash-attn`/`ctx-size`
unless the weights demand otherwise — the E4B's `-fa off` is such a demand),
145–147 (the two-phase pipeline and the `blockRecognition` role).

## 8. Links

- E4B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF>
- 12B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF>
- Unsloth «How to Run Gemma 4 QAT» guide:
  <https://unsloth.ai/docs/models/gemma-4/qat>
- Unsloth MTP guide (drafter precisions, `--spec-*` flags):
  <https://unsloth.ai/docs/models/mtp>
- llama.cpp: Gemma4 MTP #23398, E2B/E4B assistants #24282
- Gemma 4 family and license: <https://ai.google.dev/gemma/docs/core>
