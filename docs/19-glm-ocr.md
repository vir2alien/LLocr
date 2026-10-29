# GLM-OCR — model manual

Working notes for GLM-OCR as it is used in LLocr: what it is, where its GGUF
comes from, how the requests are composed around its trained task prefixes,
and what the model deliberately does not do (layout, coordinates). Everything
here is from the model card, the llama.cpp PR, or measured in this project.

A Russian mirror of this document lives in
[`19-glm-ocr.ru.md`](19-glm-ocr.ru.md).

## 1. What the model is

- **GLM-OCR** (Z.ai, `zai-org/GLM-OCR`), MIT license. A dedicated ~0.9B
  document-parsing VLM: a GLM 0.5B-class decoder (16 layers, GQA 8 KV heads)
  plus a CogViT vision encoder with a token-downsampling connector — under
  the hood "just GLM4V with some additional features" (llama.cpp PR #19677).
  Transformers class `GlmOcrForConditionalGeneration`; in GGUF the
  architecture is `glm4`.
- Trained for document parsing — text, formulas, tables — plus information
  extraction. Card benchmark: OmniDocBench V1.5 **94.62** (claimed #1 at
  release); the card calls out complex tables, code-heavy documents, seals,
  and skewed/warped scans. Small enough for a 4 GB GPU.
- **Recognition only, no detection.** The model emits no layout tokens and no
  coordinates; the official SDK pipeline pairs it with an external layout
  model (PP-DocLayoutV3) and feeds it crops. LLocr therefore does not give it
  the layout task, and its page-level replies parse with the `raw` parser.
- **Dynamic-resolution vision**: the processor scales images between
  `shortest_edge` 12544 px and `longest_edge` 9633792 px (patch 14, merge 2 →
  28 px per vision token). llama.cpp's mtmd applies the same scaling, so no
  `--image-min/max-tokens` pinning is needed.
- The weights carry one MTP/nextn layer, but llama.cpp loads it "preserved
  but unused" and the GGUF repo ships no drafter file — no speculative
  decoding for this model.

## 2. Files and repositories

| Repo | LLocr ships | Purpose |
| --- | --- | --- |
| [`ggml-org/GLM-OCR-GGUF`](https://huggingface.co/ggml-org/GLM-OCR-GGUF) | `GLM-OCR-Q8_0.gguf` (q8_0, 950 MB), `GLM-OCR-f16.gguf` (f16, 1.79 GB), `mmproj-GLM-OCR-Q8_0.gguf` (484 MB) | The recommended quant, the full-precision weights, and the vision projector. |

Notes:

- `minBuild` is **b8096**: llama.cpp learned `glm4`/`GlmOcrForConditionalGeneration`
  on 2026-02-18 (`Add support for GLM-OCR Model` #19677, commit `eeef3cf`),
  and the b8096 tag from the same evening contains it (compare API: the tag
  is 3 commits ahead of the merge, 0 behind).
- The memory estimator reads `glm4.*` GGUF metadata keys, so the wizard's
  memory warning reflects the real 16 layers / 8 KV heads instead of the
  generic fallback.

## 3. Launching llama-server

The community reference command (llama.cpp discussion #19721):

```sh
llama-server -m GLM-OCR-Q8_0.gguf --mmproj mmproj-GLM-OCR-Q8_0.gguf \
  -c 12000 -ngl 99 --flash-attn off -fit off
```

What LLocr actually starts (platform layer from `serverLaunch.json`, model
layer from the profile):

```sh
llama-server \
  --model <…>/GLM-OCR-Q8_0.gguf \
  --mmproj <…>/mmproj-GLM-OCR-Q8_0.gguf \
  --alias llocr-glm-ocr --host 127.0.0.1 --port <n> \
  --n-gpu-layers 99 --ctx-size 16384 \
  --flash-attn on --image-min-tokens 456 --image-max-tokens 6144 \
  --cache-reuse 0 --no-context-shift
```

Layer by layer:

- `--model / --mmproj / --alias / --host / --port` — core fields,
  single-sourced from the activation/install settings.
- **Model layer** (`glm-ocr.json`, both tasks identically):
  - `--flash-attn on` — the machine layer lets the user turn flash attention
    off, and without it the text attention graph materializes
    ctx² × 16 heads × F32 — **~17 GB at the machine's 16384** — which Metal
    refuses to allocate on the first request. Flash attention on is verified
    correct on the current runtime (b11564, transcribes a mixed
    English/Russian page exactly): the February-2026 "flash-attn must be
    off" reports are fixed in current builds.
  - `--image-min-tokens 456 --image-max-tokens 6144` — the native dynamic
    range is 16…12288 vision tokens (§1). The floor keeps small block crops
    above 16-token coarseness (the blockRecognition task sends crops); the
    ceiling keeps a ~240-dpi A4 page plus `max_tokens` 8192 inside the
    machine's 16384 context — at the uncapped 12288 the image alone would
    overflow it.
- No `--special`: the `raw` parser reads plain content, not det tokens.
- No `--jinja` and no thinking flags: the GGUF template renders the GLM
  `<|user|>/<|assistant|>` turns itself, and the model does not reason.
- Policy: `--parallel 1 --cache-reuse 0 --no-context-shift --no-warmup`
  (platform layer).

## 4. Composing requests

### Message layout

- **Image before text** — the card's chat template puts the image content
  part first, so the profile sets no `promptBeforeImage` (the Gemma/Qwen
  family order; the LFM2.5 of the archive puts text first).
- The OCR request is one user message: the page image plus the whole prompt.
- Block-recognition requests carry the profile's system prompt (the
  transcription contract) and one user message with the crop plus the block
  prompt. `GeneralPurposeModel` also sends
  `chat_template_kwargs: {"enable_thinking": false}` on every such request —
  the GLM template ignores the unknown kwarg, which is harmless.

### The trained task prefixes

GLM-OCR is prompt-switched between three trained modes, and the profile
maps the app's block kinds onto them:

| Prefix | Used for |
| --- | --- |
| `Text Recognition:` | the page-level prompt and every text-like block kind (text, title, list, captions, code, headers/footers, …) |
| `Table Recognition:` | `table` blocks |
| `Formula Recognition:` | `equation` / `formula` / `equation_block` blocks |

The page-level `prompts` list ships the one entry (`text-recognition`); the
model has no other whole-page mode — the SDK's per-region pipeline is what
the «Разметить» command replaces, and that stays with MinerU (the layout
task's default; the archive's TeleOCR carried it first).

### Request parameters

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | 0.0 | Deterministic greedy decoding for transcription (the community guide's 0.02 has the same intent; the repo convention is greedy — same as gemma, and as the archive's teleocr). |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | 1.0 / 0.0 / 0.0 | Neutral. |
| `max_tokens` | 8192 | Dense pages and large HTML tables. |
| `stream` | false | The parser consumes one complete response. |
| `cache_prompt` | true | Pages/blocks share the system message; there is no drafter to conflict with. |

## 5. Output formats

- **The trained whole-page mode** (no task since ADR 252 — the single-pass
  page recognition is cut): the reply is the page transcription — plain text
  or Markdown (tables may come out as HTML or Markdown tables). No layout
  tokens, no coordinates; the whole reply becomes one text page through the
  `raw` parser. The whole-page prompt ships the trained
  `Text Recognition:` prefix.
- **blockRecognition task**: the reply is the block text (tables as a table,
  formulas as LaTeX), read by `BlockRecognitionModel::parseResponse`, which
  strips control tokens and a whole-reply Markdown fence.

## 6. Role in the verification pipeline

- **blockRecognition** — the one declared task: after the
  decision model marks a block as mismatched (or on the manual «Распознать»
  commands), the crop is re-transcribed with the trained prefix for its
  kind, and the reply becomes the block text.
- **layout** — not declared: the model produces no boxes (§1).
- **decision** — not applicable: the decision task speaks the visual diff of
  MiniCPM-OCR Jev (ADR 254).

## 7. Where things live in LLocr

| Piece | Location |
| --- | --- |
| Model profile (files, prefixes, request layer) | `resources/profiles/models/glm-ocr.json` |
| Request/response composition, modality order | `OcrModel::buildRequestBody` (`src/models/`) |
| Block-recognition request/response | `BlockRecognitionModel` (`src/models/`) |
| Raw parsing (no fragments) | `RawParser` (`src/parsers/`) |
| Memory estimate (glm4 metadata keys) | `ModelMemoryEstimator` |
| Installer UI rows | `ModelQuantModel` (one list per task — assignment in «Модели по задачам», management in «Модели») |

Design decisions: ADR 88 (models are data — the profile is the whole
contract), 112 (a new model is one JSON profile plus, if the reply shape is
new, one parser — here the existing `raw` suffices), 126 (the machine owns
`flash-attn`/`ctx-size`; an empty model launch layer is a normal thing).

## 8. Links

- Model card: <https://huggingface.co/zai-org/GLM-OCR>
- GGUF: <https://huggingface.co/ggml-org/GLM-OCR-GGUF>
- llama.cpp: support PR #19677, request issue #19335, usage discussion
  #19721
- ngxson, «Using OCR models with llama.cpp»:
  <https://blog.ngxson.com/using-ocr-models-with-llama-cpp>
