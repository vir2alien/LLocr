# Model: LightOnOCR-3-4B

> Manual launch, request contract and app wiring. Russian version:
> `docs/23-lightonocr.ru.md`.

## 1. What it is

**LightOnOCR-3-4B** (LightOn) is an OCR-tuned VLM that trains exactly two
prompts — `grounding` for the labeled-box page parse and the **empty
prompt** for transcription — and the card calls every other instruction out
of distribution. In LLocr it serves three tasks, one per trained prompt:
**pageOcr** (grounding), **rawPageOcr** and **blockRecognition** (both the
empty prompt). It claims no layout, decision or fix role. Apache-2.0
licensed.

## 2. Files

| file | size | purpose |
|---|---|---|
| `LightOnOCR-3-4B-Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/Q3_K_M) | 4.2 GB (Q8_0) | the model |
| `LightOnOCR-3-4B.mmproj-bf16.gguf` | ~1.3 GB | vision projector, **mandatory** |

- Repository: `prithivMLmods/LightOnOCR-3-4B-GGUF` (profile
  `resources/profiles/models/lightonocr.json`); the original weights are
  `lightonai/LightOnOCR-3-4B`.

## 3. Launch

App layering (the profile's model layer adds four flags on top of the
platform and policy layers):

```
llama-server \
  --model <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B-Q8_0.gguf \
  --mmproj <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B.mmproj-bf16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <auto> \
  --flash-attn on --jinja --reasoning-budget 0 --image-min-tokens 1024 \
  --n-gpu-layers 99 --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--jinja`** — the chat template comes from the GGUF metadata; the
  transcription mode is the empty prompt and grounding is a bare word, so
  the template's own handling is what shapes the turn.
- **`--reasoning-budget 0`** — the model is trained with thinking disabled
  (its template default); without the flag the reply can open with a think
  block.
- **`--flash-attn on`** — required by the vision stack (platform default).
- **`--image-min-tokens 1024`** — llama.cpp warns that Qwen-VL needs at
  least 1024 image tokens for grounding tasks.

All three roles carry the identical flag set, so switching between them
never reloads the same weights.

## 4. Request contract

- **pageOcr** — one chat request per page: `[image_url data-URI, text
  "grounding"]`; the `lightonocr` parser (`llocr_parsers`) turns the
  labeled reply into positioned blocks. The profile styles the card's
  vocabulary: headings stay plain text, chart blocks become data tables,
  captions/footnotes/page numbers render italic.
- **rawPageOcr / blockRecognition** — the same request shape with the text
  part **omitted entirely** (`OcrModel` drops the empty prompt): the page
  or the block crop is transcribed raw, `parser: raw`.

Sampling: `temperature 0`, `top_p 0.9`, penalties neutral, `stream false`,
`cache_prompt true` (image embeddings reused across pages).

## 5. Verification notes

No dedicated calibration; recognized pages go through the usual
decision/fix pipeline (docs/18, docs/20).

## 6. Settings in the app

- Tasks — **pageOcr**, **rawPageOcr**, **blockRecognition** («Модели»
  window); none of them defaults to it (unlimited-ocr / glm-ocr keep the
  defaults).
- Installation via Settings → Models (HF repository
  `prithivMLmods/LightOnOCR-3-4B-GGUF`) or the wizard's hardware step.

## 7. Known limitations

- **Two prompts, nothing else.** Asking it to fix, judge or lay out a page
  is out of distribution by the card's own wording — the profile
  deliberately does not declare those roles.
- The GGUF repository is a third-party packaging (`prithivMLmods`), not
  LightOn's own.
