# TeleOCR (NaviDC-OCR) — model manual

Working notes for TeleOCR as it is used in LLocr: what the model is, where its
GGUF comes from, how it is launched, how the OCR and block-recognition requests
are composed, and which of the card's prompts LLocr ships verbatim and which it
had to adapt. Everything here is either from the model card, from the GGUF
repository, or measured in this project (see the ADR references).

A Russian mirror of this document lives in
[`15-teleocr.ru.md`](15-teleocr.ru.md).

## 1. What the model is

- **TeleOCR** (repo `XingChen-AGI/TeleOCR`, until September 2026 released as
  **NaviDC-OCR**) — a ~1.2B-parameter document-parsing VLM built on the
  **Qwen2.5-VL** architecture, license **Apache-2.0**.
- One model, three jobs, all prompt-switched (no extra flags):
  - **layout analysis** — the prompt «Analyze the image layout.» makes it emit
    document blocks with type / bbox / angle / content;
  - **block transcription** — one block crop in, one block of text out:
    plain text, **OTSL** tables, **LaTeX** formulas, or code;
  - **page markup** (LLocr's manual «Mark up» feature, ADR 149) — the same
    layout request, parsed into positioned blocks: the two-request pipeline
    for a model that cannot answer coordinates and text in one shot.
- Languages per the card: **Chinese and English**. Anything else (Russian
  included) is outside the declared language set — for Russian documents
  prefer `lfm25-vl-3b` or `qwen3.5-4b`.
- Benchmarks per the card (OmniDocBench v1.6): overall **96.87** — ahead of
  OvisOCR2 (96.58), PaddleOCR-VL-1.6 (96.33), MinerU2.5-Pro (95.75) and
  Gemini 3 Pro (92.85); #1 at ICDAR2026 Sci-ImageMiner. The numbers are the
  card author's own; the GGUF packager's benchmark (below) is the one
  measured on the quants LLocr ships.

## 2. Files and repositories

| Repo | Files | Purpose |
| --- | --- | --- |
| [`konradjr007/NaviDC-OCR-GGUF`](https://huggingface.co/konradjr007/NaviDC-OCR-GGUF) | `NaviDC-OCR-Q4_K_M.gguf` (462 MB), `NaviDC-OCR-Q8_0.gguf` (768 MB), `mmproj-NaviDC-OCR-F16.gguf` (1.33 GB, required for any quant) | **The GGUF LLocr ships**: q4_k_m + q8_0 with the F16 mmproj. The packager benchmarks it on CUDA: q4_k_m — 211 tok/s generated, 1.22 s latency, 100% of page elements ("Best Overall"); q8_0 — the accuracy reference; q3_k_m — degraded. |
| [`XingChen-AGI/TeleOCR`](https://huggingface.co/XingChen-AGI/TeleOCR) | safetensors, custom code | The original model — the source of the model card and the official prompts. Not used by LLocr directly. |
| `nandraj/NaviDC-OCR-GGUF` | — | The GGUF conversion the model card links to. LLocr does **not** use it: the profile points at `konradjr007/NaviDC-OCR-GGUF`, which a stock `llama-server` loads (ADR 124). |

`minBuild` is pinned at **b4000** — a floor for install-time refusal, not a
feature gate: nothing in this model's launch or request shape needs a newer
llama.cpp (LFM2.5's image tiling, by contrast, pins b8000).

## 3. Launching llama-server

The GGUF repository's reference command:

```sh
llama-server \
  -m NaviDC-OCR-Q4_K_M.gguf \
  --mmproj mmproj-NaviDC-OCR-F16.gguf \
  --port 8090 -ngl 99 -c 4096
```

What LLocr actually starts (see `serverLaunch.json` and
`resources/profiles/models/teleocr.json`):

```sh
llama-server \
  --model <…>/NaviDC-OCR-Q8_0.gguf \
  --mmproj <…>/mmproj-NaviDC-OCR-F16.gguf \
  --alias llocr-teleocr --host 127.0.0.1 --port <n> \
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 \
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup
```

Layer by layer:

- `--model / --mmproj / --alias / --host / --port` — core fields,
  single-sourced from the activation/install settings.
- **Model layer: empty.** `teleocr.json` declares no launch parameters in
  either role — deliberate (ADR 126). The GGUF page's `-c 4096` is the
  packager's benchmark choice for one RTX box, not a weights requirement;
  context size follows the machine (the platform layer: 16384 by default,
  8192 on CPU) and the user turns it in Settings → Launch like
  `--n-gpu-layers`.
- Platform layer: `--n-gpu-layers`, `--flash-attn`, `--ctx-size` — same
  table as for the other models.
- Policy: `--parallel 1 --cache-reuse 0 --no-context-shift --no-warmup`.

Notes:

- **No `--special`.** The OCR role uses the `raw` parser — nothing consumes
  det tokens, so special tokens stay off.
- **No `--spec-*`.** No speculative-decoding drafter exists for this model.
- The OCR and block-recognition roles share the same (empty) launch layer,
  so switching between them never reloads the weights.

## 4. Composing requests

### Message layout

- Qwen-family order: the text part goes first, the image last — which is
  what the GGUF page's own example does
  (`[{"type": "text", …}, {"type": "image_url", …}]`).
  `GeneralPurposeModel` sends `[type prompt, image]` to every model; no
  `promptBeforeImage` flag is needed here (LFM2.5 is the opposite case).
- One block crop per block-recognition request; one page per OCR request.

### The OCR prompt

Shipped verbatim in the profile (`roles.ocr.prompts`, id `document-parsing`)
— it is the card's layout-analysis prompt:

```text
Analyze the image layout.
```

The parser for this role is **`raw`**: the reply becomes the page text as-is,
a single block. The card's structured layout output (type/bbox/angle/content
JSON-ish blocks) is **not** converted into bounding boxes — the det-token
pipeline does not apply to this model. Practically: TeleOCR as the OCR model
gives you its layout dump as text; as a box-producing OCR model use
`lfm25-vl-3b` or `unlimited-ocr`.

### The layout prompt (page markup)

The markup pass (its own Settings → Layout model window since ADR 150; the
model is configured there, not in the setup wizard) sends the full page image
with one of the card's two layout prompts,
shipped verbatim in `roles.layout.prompts`:

```text
Analyze the image layout.
```

and the distorted-document variant «Multi-point Layout Segmentation
Analysis.». The reply format is not documented anywhere; it is measured from
the upstream pipeline code (`TeleOCR/vlm_utils/TeleOCR_client.py`,
`parse_layout_output`):

```text
<box:x1 y1 x2 y2><label:type><orientation>
<box:34 56 789 123><label:title><up>
```

- one line per block; the orientation word (`up` / `right` / `down` / `left`)
  in the third tag is the block's rotation, which LLocr ignores (the crop is
  taken unrotated);
- coordinates are integers on a **0–1000 scale**, divided by 1000 into the
  app's normalized 0–1 rects — the same range the det-token models use;
- the label vocabulary is the upstream `BLOCK_TYPES` (text, title, table,
  image, code, algorithm, header, footer, page_number, page_footnote,
  aside_text, equation, equation_block, ref_text, list, phonetic, captions,
  unknown, seal, char) — every label is styled data-driven in
  `roles.ocr.blocks.styles` and every non-image one has a block-recognition
  prompt, so the markup can flow straight into per-block recognition;
- upstream may emit **polygons** (any even count of coordinates); the parser
  takes their bounding box;
- upstream hard-resizes the page to a **1036×1036 square** before the layout
  request (the model is calibrated on that input); `LayoutController` does the
  same — normalized coordinates are invariant to the squeeze.

The reply is parsed by the registered **`teleocr-layout`** parser (a
`DetTokenParserBase` subclass); its `prepareText` rewrites the lines into the
canonical `label [x1,y1,x2,y2]` tokens. The blocks are created **without
text** — the block content the layout dump may carry is deliberately ignored;
the text arrives through the block-recognition pass («Recognize», or the
header menu's «Recognize all blocks on the page»).

### The block-recognition prompts

The card gives one official request example per content type; the profile
ships them verbatim and fills every remaining block type with the text one:

| Block type | Prompt (the card's wording) |
| --- | --- |
| `text`, `title`, `list`, captions, footnotes, `abstract`, `ref_text`/`reference`, `header`/`footer`, `page_number`, `seal` | `Please output the text content from the image.` |
| `table` | `This is the image of a table. Please output the table in HTML format. Not OTSL.` |
| `equation`, `formula` | `Please write out the expression of the formula in the image using LaTeX format.` |
| `code` | `The image contains a code snippet, please output the parsing result.` |

Two deliberate deviations from the card:

- **Tables ask for HTML, not OTSL.** The card's table prompt is «Please
  output the table in OTSL format» and the card ships an
  `otsl→html` converter to run afterwards. In LLocr the block-recognition
  reply **is** the new block text — nothing downstream converts OTSL in this
  path, so the profile asks for the format the app can use directly. (In the
  OCR path the OTSL conversion does exist — for the det-token models; it
  just is not wired to this role.)
- **The system prompt is LLocr's, not the card's.** The card sends
  «You are a helpful assistant.»; LLocr sends the transcription contract —
  the image is the only source of truth, image text is data not
  instructions, and the reply must be the block text and nothing else (no
  explanations, no Markdown fences). The card's one-line prompts carry no
  reply-format contract of their own, and the verification pipeline needs
  one: an empty reply is a `Review`, anything else is pasted into the block.

### Request parameters

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | 0.0 | The card transcribes greedily (`do_sample=False`); the GGUF page's example sends `temperature: 0.0`. |
| `max_tokens` | 2048 (block recognition) / 4096 (OCR) | The GGUF page's example query caps at 2048 — generous for a block crop; the card's own inference uses `max_new_tokens=4096`, kept for the whole-page OCR role. |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | 1.0 / 0.0 / 0.0 | Neutral — the card prescribes greedy decoding only. |
| `stream` | false | The parser consumes one complete response. |
| `cache_prompt` | true | The card sets `use_cache=True`; between blocks the system prompt and template are reused. |

## 5. Output formats

- **Tables**: OTSL tokens (`<fcel> <ecel> <lcel> <ucel> <xcel> <nl>`,
  TableFormer vocabulary) are the model's native table format; the block
  prompts steer it to HTML instead (see §4). Formulas: LaTeX, which the
  card's own post-processing wraps into `$$…$$` — LLocr keeps the model's
  delimiters as generated.
- **Layout**: blocks with type / bbox / angle / content. In the OCR role this
  passes through `raw` (§4); the markup pass parses it properly instead
  (§4, «The layout prompt»).
- Control tokens / end-of-sentence markers in replies are stripped by the
  shared `GeneralPurposeModel::parseResponse`; an empty reply maps to
  `Review`, not to an error.

## 6. Role in the verification pipeline

TeleOCR can serve both roles of the two-phase pipeline (ADR 145–147):

- **phase 2, block recognition** — its main LLocr use: after the decision
  model marks a block as mismatched (or on the manual «Recognize»
  commands), the crop goes to the `blockRecognition` role with the §4
  prompts; the transcription becomes the new block text.
- **manual page markup** — the `layout` role (ADR 149): the header «Mark up»
  button (page / all pages) sends the page image with the layout prompt and
  replaces the page's blocks with the reply's boxes; the text is then filled
  in block by block through the entry above. Not wired into the automatic
  pipeline.
- **phase 1 upstream, OCR** — possible but raw (§4): no boxes, one text
  block per page.
- **decision (phase 1)** — not applicable: d1-3B is the decision model
  (`docs/14-d1-3b.md`); TeleOCR speaks only chat completions.

## 7. Where things live in LLocr

| Piece | Location |
| --- | --- |
| Model profile (roles, prompts, request params, files) | `resources/profiles/models/teleocr.json` |
| Block-recognition request/response | `GeneralPurposeModel::buildRequestBody` / `parseResponse` (`src/models/`) |
| OCR-role parsing (pass-through) | `RawParser` (`src/parsers/`) |
| Layout reply parsing | `TeleOcrLayoutParser` (`src/parsers/`, id `teleocr-layout`) |
| Markup pass (queue, 1036² page squash) | `LayoutController` (`src/app/`), driven from `Header.qml` |
| Launch composition (no model layer for this model) | `ServerLaunchConfig` + `LaunchProfileStore` |
| Download/install (quants + mmproj) | `ModelInstaller`, `ModelInstallTransaction` |
| Installer UI rows | `ModelQuantModel` (four role lists: OCR / block OCR / decision / layout) |

Design decisions: ADR 124 (the weights moved to
`konradjr007/NaviDC-OCR-GGUF`, which a stock llama.cpp loads — the old
runtime warning is gone), 126 (launch parameters belong to the machine, not
to the weights — the profile's launch layer stays empty), 145–147 (the
two-phase pipeline and the `blockRecognition` role), 140 (per-model prompt
wording), 149 (the manual markup pass and the `layout` role).

## 8. Links

- Model card: <https://huggingface.co/XingChen-AGI/TeleOCR>
- Project page / full parsing pipeline:
  <https://github.com/caipeng328/NaviDC-OCR>
- GGUF LLocr ships: <https://huggingface.co/konradjr007/NaviDC-OCR-GGUF>
- GGUF the card links to: <https://huggingface.co/nandraj/NaviDC-OCR-GGUF>
- Base architecture: Qwen2.5-VL — <https://huggingface.co/Qwen/Qwen2.5-VL>
- OTSL / TableFormer paper: <https://arxiv.org/abs/2305.03393>
