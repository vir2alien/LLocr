# LFM2.5-VL-3B — model manual

Working notes for LiquidAI/LFM2.5-VL-3B as it is used in LLocr: what the model
is, how to launch it, how to compose requests, what its output looks like, and
what has to be compensated for on our side. Everything here is either from the
model card or measured in this project (see the ADR references).

A Russian mirror of this document lives in
[`13-lfm25-vl-3b.ru.md`](13-lfm25-vl-3b.ru.md).

## 1. What the model is

- **LiquidAI/LFM2.5-VL-3B** — a multimodal variant of LFM2.5 built for
  on-device deployment: LFM2.5-2.6B language backbone + SigLIP2 NaFlex 400M
  vision encoder, ~3.1B parameters, 128,000-token vocabulary, 32,768-token
  context.
- 16 languages, including English and Russian.
- Positioning per the card: single-turn, high-throughput, low-latency tasks.
  It **answers directly instead of reasoning** — there is no thinking mode,
  verbose chain-of-thought prompts only hurt it.
- Full-page OCR with layout annotation is a trained, first-class capability —
  this is what LLocr uses. Grounding (bounding boxes from natural-language
  queries) is trained too, which is why the layout annotation carries usable
  coordinates at all.
- Native formats: **OTSL** (TableFormer vocabulary: `<fcel> <lcel> <ucel>
  <xcel> <nl>`) for tables, LaTeX for equations, plain text for everything
  else.

## 2. Files and repositories

| Repo | Files | Purpose |
| --- | --- | --- |
| [`LiquidAI/LFM2.5-VL-3B-GGUF`](https://huggingface.co/LiquidAI/LFM2.5-VL-3B-GGUF) | `LFM2.5-VL-3B-Q4_K_M.gguf`, `LFM2.5-VL-3B-Q8_0.gguf`, `mmproj-LFM2.5-VL-3B-F16.gguf` | The target model and the vision projector. The LLocr catalog ships the q4_k_m and q8_0 quantizations. |
| [`LiquidAI/LFM2.5-VL-3B-DSpark-GGUF`](https://huggingface.co/LiquidAI/LFM2.5-VL-3B-DSpark-GGUF) | `LFM2.5-VL-3B-DSpark-F16.gguf` (567 MB, sha256 `4eb314404b7b4c0033799faf5662f8277cf3d94163b8d791a22363165e294821`) | A standalone **speculative-decoding drafter** (DSpark): 4 attention layers, a Markov head, a confidence head, trained with block size 9. Token embeddings and the LM head are shared with the target at load time, so it must be paired with the `LFM2.5-VL-3B-GGUF` target — it is not runnable alone. |

The drafter is optional: the target runs fine without it, just slower. LLocr
downloads it together with the model and records the path in
`launch/draftPath` (ADR 137).

## 3. Launching llama-server

**Minimum build: b8000.** LFM2.5-VL image tiling landed in llama.cpp PR
[#19454](https://github.com/ggml-org/llama.cpp/pull/19454); older builds
mis-handle large page images.

The model card's reference command:

```sh
llama-server -hf LiquidAI/LFM2.5-VL-3B-GGUF:F16 \
  -hfd LiquidAI/LFM2.5-VL-3B-DSpark-GGUF:F16 \
  --spec-type draft-dspark --spec-draft-n-max 8 --spec-draft-n-min 0 \
  -ngl 99 -ngld 99 -fa on
```

What LLocr actually starts (the flag layers, see `serverLaunch.json` and
`resources/profiles/models/lfm25-vl-3b.json`):

```sh
llama-server \
  --model <…>/LFM2.5-VL-3B-Q8_0.gguf \
  --mmproj <…>/mmproj-LFM2.5-VL-3B-F16.gguf \
  --model-draft <…>/LFM2.5-VL-3B-DSpark-F16.gguf \
  --alias llocr-lfm25-vl-3b --host 127.0.0.1 --port <n> \
  --special --spec-type draft-dspark --spec-draft-n-max 8 --spec-draft-n-min 0 \
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 \
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup
```

Layer by layer:

- `--model / --mmproj / --model-draft / --alias / --host / --port` — core
  fields, single-sourced from the activation/install settings.
- Model layer (owned by `lfm25-vl-3b.json`, both roles):
  - `--special` — emit special tokens in the output; the det-token parser
    needs them;
  - `--spec-type draft-dspark` — speculative decoding with the DSpark
    drafter;
  - `--spec-draft-n-max 8` — the drafter was trained with block size 9; 8 is
    the card's recommendation for Apple silicon;
  - `--spec-draft-n-min 0` — let the drafter skip low-confidence steps.
- Platform layer (the machine's memory): `--n-gpu-layers`, `--flash-attn`,
  `--ctx-size`. `flash-attn on` is required by the DSpark recipe.
- Policy (how the server is driven): `--parallel 1 --cache-reuse 0
  --no-context-shift --no-warmup`.

Notes:

- **Speculative decoding is exact under greedy decoding** — the target
  verifies every proposed token, so the output equals the target model alone.
  With the card's recommended `temperature=0.2` (recognition) it is still
  lossless in the llama.cpp implementation (rejected tokens are discarded);
  the acceptance rate is reported in the server timing logs.
- On builds without the `--spec-*` flags LLocr's capability probe retries
  without the offending flags — the model degrades to non-speculative speed,
  never to wrong output.
- The two roles (OCR and verification) declare the **same** launch layer on
  purpose: switching tasks re-uses the loaded weights instead of reloading
  them, and the DSpark drafter accelerates verification for free.

## 4. Composing requests

### Message layout

- The chat template is ChatML-like; the card's default system prompt
  ("You are a helpful assistant trained by Liquid AI.") applies when no
  system message is sent.
- **Text first, image last.** Every LFM2.5 example renders the user text and
  then the `<image>` tag. In the OpenAI-compatible API that means the content
  parts array must be `[text, image_url]` — LLocr encodes this as
  `promptBeforeImage: true` in both roles of the model profile (the Qwen
  family documents the opposite order, so the flag is per-model data).
- One page per request: the prompt talks about pages in the plural, but a
  single-image message is simply `image_index=0`.

### The recognition prompt

Shipped verbatim in the profile (`roles.ocr.prompts`, id
`layout-annotation`) — it is the model card's recommended prompt:

```text
Parse this document into its layout regions. The pages are provided as images
in reading order. For every region, in reading order across all pages, output
a header line immediately followed by the region's content:

image_index=<n> <label> [xmin, ymin, xmax, ymax]
<content>

where:
- image_index is the zero-based index of the page image the region appears on
  (0 for the first image, 1 for the second, and so on)
- <label> is one of these layout labels: text, title, list, table,
  table_caption, table_footnote, image, image_block, image_caption,
  image_footnote, chart, equation, formula_number, code, code_caption,
  algorithm, aside_text, ref_text, phonetic, page_header, page_footer,
  page_number, page_footnote
- [xmin, ymin, xmax, ymax] are normalized integer coordinates in [0, 1000]
- <content> is the region's content: plain text for text regions, LaTeX for
  equations, OTSL for tables, and a short description for images and charts

Separate each region block with one blank line. Return only the parsed regions.
```

### Request parameters

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | 0.2 | The card's recommendation (with top-k 50). |
| `top_k` | 50 | The card's recommendation. |
| `repeat_penalty` | 1.0 | The card's recommendation (disabled). |
| `max_tokens` | 8192 | A full annotated page exceeds 4k tokens routinely. |
| `stream` | false | The parser consumes one complete response. |

For the **verification** role the sampling is greedy (`temperature 0`,
penalties off, `stream false`, `cache_prompt true`) — the structured
OK/FIX/REVIEW protocol wants determinism, and greedy decoding keeps the
DSpark speculation exact.

## 5. Coordinates: the 2:3 grounding canvas

The model does **not** ground its boxes on the page. The coordinates it
emits are grounded on a portrait **2:3 canvas** (height:width = 1.5): the
page is fitted into that canvas and the boxes refer to the fitted image.

With `a = H/W` (the page's aspect, portrait pages have `a > 1`):

- **`a ≤ 1.5`** (every normal portrait page, A4 is 1.414) — the width fills
  the canvas, the height is letterboxed and centered:
  - `x_model = x_true` (exact),
  - `y_model = y_true · (a/1.5) + 500 · (1 − a/1.5)`.
  - A4: `y_model ≈ 0.945 · y_true + 27.5` (of 1000). Measured against a real
    reply: residuals ≤ 4 units; the naive reading of the model's boxes
    without this correction puts every block visibly too high.
- **`a > 1.5`** (taller than 2:3) — the height fills, the width is
  letterboxed:
  - `y_model = y_true` (exact),
  - `x_model = x_true · (1.5/a) + 500 · (1 − 1.5/a)`.

**Compensation in LLocr:** `Lfm25VlParser` overrides the `calibrateRect()`
hook with the inverse mapping, and `ParserOptions::pageAspect` carries the
page's `H/W` (filled from `DocumentPage::pixelSize`, A4 as the default). If a
future LiquidAI or llama.cpp update removes the letterbox, deleting the
override restores identity (ADR 136).

Do not draw the model's boxes on the page without this correction — the
overlay will drift upward by up to ~5% of the page height.

## 6. Output quirks the parser absorbs

The `lfm2.5-vl` parser (det-token family) handles the model's transcription
artifacts; all of the below are fixed in LLocr, listed here so raw replies
can be read while debugging (`LLOCR_RAW_DEBUG=1` dumps every request/reply):

- **Tables come as OTSL** (`<fcel> <lcel> <ucel> <xcel> <nl>`) and the tokens
  leak into `text` and `equation` blocks too — everywhere, not only inside
  `table` blocks. Converted to GFM pipe tables (or real HTML with
  rowspan/colspan when "Tables as HTML" is on).
- **Table-caption drift**: the model sometimes writes the caption as
  `<label>…</label>` + `<content>` OTSL rows — rewritten into a synthetic
  `table_caption` + `table` pair.
- **Equations arrive dollar-wrapped**: `$C_{MHA}(T) = L_m + T$. (5)` — the
  wrapper is stripped, the trailing number becomes `\tag{n}`, nested inline
  `\(...\)` inside display `\[…\]` is un-nested.
- `image_block` becomes an image placeholder like `image`/`chart`; the alt
  text keeps only the first line.
- Duplicates (the model re-emitting a paragraph at shifted coordinates) are
  detected by text, tail-joined or dropped, and marked `duplicateSuspect` for
  the verification pass.

## 7. Using it as the verification model

The profile declares a `check` role next to `ocr` (ADR 138), so LFM2.5-VL-3B
can be picked as the block verifier:

- Same weights, same launch layer — switching OCR ↔ verification never
  reloads the server, and the DSpark drafter accelerates verification.
- The structured protocol: the reply must be exactly `OK`, `FIX` + newline +
  the complete corrected block, or `REVIEW`. The profile carries its own
  system prompt and per-block prompts whose first paragraph is a mandatory
  reply-format contract — a 3B direct-answer model follows the format much
  better when it is restated in the request and shaped like its trained
  header-line-then-content layouts (ADR 139/140).
- Known degenerate output: a bare `FIX` with no corrected block. LLocr maps
  that to `REVIEW` (needs human eyes) instead of an error. Corrected text
  without the `FIX` line stays an error — it cannot be told apart from
  commentary.
- Message layout: type prompt, then the OCR candidate, then the image
  (`promptBeforeImage` applies to the check role too).

## 8. Where things live in LLocr

| Piece | Location |
| --- | --- |
| Model profile (prompt, launch layer, request params, check role, DSpark module, `promptBeforeImage`) | `resources/profiles/models/lfm25-vl-3b.json` |
| Parser (`lfm2.5-vl`): OTSL conversion, calibration, equation fixes | `src/parsers/` (`Lfm25VlParser`, `DetTokensParser` base) |
| Label → style map (incl. `image_block`, `code` overrides) | `resources/profiles/labels.json` |
| Verifier system prompt + block prompts | the check role of the profile (overrides `resources/profiles/verifyPrompts.json`) |
| DSpark download/install | `ModelInstallTransaction` (any model with `files.mtp`) |
| Launch/flag composition | `ServerLaunchConfig` + `LaunchProfileStore` (platform table shows only `n-gpu-layers`, `flash-attn`, `ctx-size`) |

Design decisions: ADR 87 (the model adapter and parser), 112 (coordinate
calibration is a parser hook), 135 (duplicate handling), 136 (the 2:3
calibration and equation fixes), 137 (DSpark module), 138–140 (the check
role, verifier prompts, bare-FIX handling), 141 (launch-table simplification),
143 (the part-order audit).

## 9. Links

- Model card: <https://huggingface.co/LiquidAI/LFM2.5-VL-3B>
- GGUF: <https://huggingface.co/LiquidAI/LFM2.5-VL-3B-GGUF>
- DSpark drafter (GGUF): <https://huggingface.co/LiquidAI/LFM2.5-VL-3B-DSpark-GGUF>
- DSpark drafter (original): <https://huggingface.co/LiquidAI/LFM2.5-VL-3B-DSpark>
- Release post: <https://www.liquid.ai/blog/lfm2-5-vl-3b>
- DSpark post: <https://www.liquid.ai/blog/lfm2-5-vl-dspark>
- Liquid AI documentation (chat template, inference): <https://docs.liquid.ai>
- llama.cpp (server, speculative decoding, image tiling PR
  [#19454](https://github.com/ggml-org/llama.cpp/pull/19454)):
  <https://github.com/ggml-org/llama.cpp>
- OTSL / TableFormer paper: <https://arxiv.org/abs/2305.03393>
