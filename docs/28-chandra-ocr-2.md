# Model: Chandra-OCR-2

> Manual launch, request contract and app wiring. Russian version:
> `docs/28-chandra-ocr-2.ru.md`. The model card:
> <https://huggingface.co/datalab-to/chandra-ocr-2>.

## 1. What it is

**Chandra-OCR-2** is DataLab's document OCR model: a Qwen3.5-VL-4B
fine-tune that reads a page and answers with **HTML**. It is trained with
two prompts:

| prompt | answer | the app's task |
|---|---|---|
| `ocr_layout` | every block wrapped in a `<div>` with `data-bbox` (0–1000) and `data-label` | **pageOcr** — recognition with markup |
| `ocr` | the same HTML without the layout markup | **rawPageOcr** — recognition without markup |

Both prompts are shipped verbatim in the profile
(`resources/profiles/models/chandra-ocr-2.json`); the reference client
(`chandra-ocr` on PyPI) is the source of the text.

The model is Qwen3.5-VL: the same decoder, the same vision tower and the
same hybrid recurrent memory as the app's Qwen3.5 fix models, so the
launch flags, the memory profile and the projector are the same family.

## 2. Which tasks it fits

| task | fits | why |
|---|---|---|
| **pageOcr** (OCR with markup) | **yes** | the `ocr_layout` prompt returns the blocks with their boxes and labels — the app's markup OCR |
| **rawPageOcr** (OCR without markup) | **yes** | the `ocr` prompt returns the document without the layout markup; the page is one text, without blocks |
| **blockRecognition** (OCR of a single block) | **no** | measured: on a crop the model answers the *page* task — a layout div whose box is the whole crop, wrapped in `$$…$$` — so the reply is not the block's text (ADR 145) |
| **decision** (verification) | no | the decision protocol is a different training (`/v1/systemone`, one `noul` answer) — the model does not answer it |
| **fix** (correction) | no | the fix protocol needs `OK`/`FIX`/`REVIEW`; the model answers with a document, not with a verdict |
| **layout** (markup without text) | no | the layout prompt always carries the text with the boxes; there is no markup-only prompt |

The block task was tried before it was ruled out: a formula crop came back
as

```
$$
<div data-bbox="10 10 990 980" data-label="Equation-Block">
<math display="block">\frac{p}{p_{\text{н.впр}}} \approx …</math>
</div>
$$
```

— the page answer on a one-block image, the box covering the whole crop.
The model has no trained per-block prompt, and the plain prompt allows
`data-bbox`/`data-label`, so the crop comes back as a document with one
block in it. Reading that as the block's text would mean stripping a
layout wrapper the model never promised — a heuristic about one model's
quirks, not a contract. The role is therefore not declared.

## 3. Files

| file | size | purpose |
|---|---|---|
| `chandra-ocr-2.Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/Q3_K_M/Q2_K) | 5.2 GB (Q8_0) | the model |
| `chandra-ocr-2.mmproj-bf16.gguf` | 0.68 GB | vision projector, **mandatory** |

- Repository: `prithivMLmods/chandra-ocr-2-GGUF` (profile
  `resources/profiles/models/chandra-ocr-2.json`); the original weights
  are `datalab-to/chandra-ocr-2`.
- Without the mmproj the model sees no image — the reply is empty.

## 4. Launch

App layering (the profile's model layer adds the flags on top of the
platform `n-gpu-layers`/`flash-attn`/`ctx-size` and the policy
`parallel`/`cache-reuse`/`no-context-shift`/`no-warmup`):

```
llama-server \
  --model <modelsDir>/prithivMLmods__chandra-ocr-2-GGUF/chandra-ocr-2.Q8_0.gguf \
  --mmproj <modelsDir>/prithivMLmods__chandra-ocr-2-GGUF/chandra-ocr-2.mmproj-bf16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <auto> \
  --image-min-tokens 456 --image-max-tokens 6144 \
  --jinja --reasoning-budget 0 --temp 0 \
  --n-gpu-layers 99 --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--jinja`** — the model is trained with the chat template that ships
  in the GGUF; the built-in one would not reproduce it.
- **`--reasoning-budget 0`** — the template has a thinking branch
  (`enable_thinking`); the budget of zero keeps the model in the
  transcription mode. The model transcribes, it does not reason.
- **`--image-max-tokens 6144`** — the model's own limit: the reference
  client scales the page to at most 3072×2048 pixels, which is 6144
  Qwen3.5-VL image tokens. The page is scaled to the same budget before
  the request (`imageLongestEdge: 2984` in the profile — the same limit
  for an A4 page).
- **`--image-min-tokens 456`** — a block crop is scaled up to this floor.
- ctx-size follows the machine (16384 on the GPU platforms): 6144 image
  tokens + the prompt + `max_tokens` 8192 fit into it.

## 5. Request contract

One chat-completions request per page: the user message carries the
**image first, the prompt second** — the order the reference client uses
(`chandra/model/vllm.py`).

```
[{"role": "user", "content": [
    {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,…"}},
    {"type": "text", "text": "<the trained prompt>"}
]}]
```

- Sampling: `temperature 0.0`, `top_p 0.1` — the reference client's
  settings (greedy decoding with a narrow nucleus).
- `max_tokens` 8192 (`maxOutput` in the profile). The reference client
  allows 12384; the app's budget fits the context window.
- The prompt is the role's: the layout one for `pageOcr`, the plain one
  for `rawPageOcr`. A model that serves both tasks answers each of them
  with its own prompt — the task reaches the profile (`OcrModel`'s role).

## 6. The reply

The layout prompt answers with one `<div>` per block:

```html
<div data-bbox="100 200 900 260" data-label="Section-Header">
  <h2>1. Introduction</h2>
</div>
<div data-bbox="100 300 900 500" data-label="Text">
  <p>Humans are remarkably adept at long-horizon tasks.</p>
</div>
```

- `data-bbox` is `x0 y0 x1 y1`, normalized 0–1000.
- `data-label` is one of the model's own labels: `Text`,
  `Section-Header`, `Page-Header`, `Page-Footer`, `Caption`, `Footnote`,
  `Equation-Block`, `List-Group`, `Image`, `Figure`, `Diagram`, `Table`,
  `Code-Block`, `Form`, `Table-Of-Contents`, `Chemical-Block`,
  `Complex-Block`, `Bibliography`, `Blank-Page`.
- The line break is written as `<br/>` — the tag closes itself. The reader
  knows such a tag (ADR 224); read as an ordinary element it would have
  swallowed the rest of the reply.

The parser (`src/parsers/ChandraParser.cpp`) turns the reply into the
app's blocks:

- the model's label becomes the app's label vocabulary (`Section-Header`
  → `title`, `Equation-Block` → `equation`, `Caption` →
  `image_caption`, `Blank-Page` is dropped);
- the box becomes the block's rectangle, normalized;
- the content becomes Markdown: paragraphs and lists as Markdown, a table
  kept as the HTML it came as (the table style renders it), `<math>` as
  `$…$` / `$$…$$`, an `<img>` as its `alt` description, a form's
  checkbox as `☐`/`☒`.

A formula inside a table cell is the app's math markup as well — `$…$`,
not a `<math>` tag: a `<math>` tag is MathML to the browser, and the
formula in it would not be drawn (ADR 225).

A block the model calls `Equation-Block` is not always a formula: it may be
a paragraph with formulas in it. The parser looks at the content — text
outside `<math>` makes the block text, not a formula (ADR 224). A formula
that carries prose would be wrapped into `$$…$$` and would not render at
all.

A reply without the layout markup (the plain prompt, or a model that
ignored it) becomes the page's text without blocks — the same fallback
the other parsers have.

The **rawPageOcr** task asks for the text alone, so the layout markup is
dropped even when the model volunteers it: the task that wants the text
must not turn into a markup page. (The plain prompt allows `data-bbox`
and `data-label`, and the model does use them on a small image.)

## 7. Settings in the app

- Tasks — **pageOcr** and **rawPageOcr** («Модели» window): the model can
  be picked for each of them.
- Installation via Settings → Models (HF repository
  `prithivMLmods/chandra-ocr-2-GGUF`) or the wizard's hardware step.
- The output parser is `chandra-ocr` (the parser id the profile
  declares); it can be overridden in the parser settings.
- External mode: any OpenAI-compatible server carrying the model; the
  parser side stays the same.

## 8. Known limitations

- The GGUF repository is a third-party packaging (`prithivMLmods`), not
  DataLab's own — the quantization provenance is the packager's.
- The model does not serve the block task (§2): a crop comes back as a
  document with one layout block, so the per-block «Распознать» needs a
  different model.
- The vision budget and the page scale were derived from the reference
  client's rule (at most 3072×2048 pixels); they were not measured on
  the app's own hardware.
- The memory block reuses the calibrated numbers of the sibling Qwen3.5
  models (the same decoder and the same vision tower); the projector
  weight is the model's own.
