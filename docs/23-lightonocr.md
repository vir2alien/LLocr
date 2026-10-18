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

App layering (the profile's model layer adds three flags on top of the
platform and policy layers):

```
llama-server \
  --model <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B-Q8_0.gguf \
  --mmproj <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B.mmproj-bf16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <auto> \
  --flash-attn on --jinja --reasoning-budget 0 \
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

The profile deliberately does **not** set `--image-min-tokens`: pages are
sent through `imageLongestEdge: 2048` (`OcrModel` downscales,
KeepAspectRatio — the card trains/evaluates on 400-DPI pages with a target
longest dimension of 2048 px, exactly what the official client sends), so
a page already yields ≥1024 vision tokens, and on a block crop the flag
would stretch a ~65×20 px strip over 1024 tokens. One flag set for all
three roles also means switching between them never reloads the weights.

## 4. Request contract

- **pageOcr** — one chat request per page: `[image_url data-URI, text
  "grounding"]`; the `lightonocr` parser (`llocr_parsers`) turns the
  labeled reply into positioned blocks. The profile styles the card's
  vocabulary: headings stay plain text, chart blocks become data tables,
  captions/footnotes/page numbers render italic.
- **rawPageOcr / blockRecognition** — the same request shape with the text
  part **omitted entirely** (`OcrModel` drops the empty prompt): the page
  or the block crop is transcribed raw, `parser: raw`.

Sampling: `temperature 0.1` (the official LightOnOCR client's setting for
v3 — "use temperature 0.1, not greedy decoding"; the card's example: 0.2),
`top_p 0.9`, penalties neutral, `stream false`, `cache_prompt false`
(avoids the recurring-state position warnings Qwen3.5 logs when requests
share a prefix — a throughput trade-off, not a quality one).

Reply hygiene: markdown-trained output escapes the punctuation that
follows inline math (`$x$\.` `$x$\,`); `stripEscapedPunctuation`
(`VisionChatModel`) unescapes it on every page/block reply, the same fix
the official client applies as `fix_dollars`.

## 4. Request contract

- **pageOcr** — one chat request per page: `[image_url data-URI, text
  "grounding"]`; the `lightonocr` parser (`llocr_parsers`) turns the
  labeled reply into positioned blocks. The profile styles the card's
  vocabulary: headings stay plain text, chart blocks become data tables,
  captions/footnotes/page numbers render italic.
- **rawPageOcr / blockRecognition** — the same request shape with the text
  part **omitted entirely** (`OcrModel` drops the empty prompt): the page
  or the block crop is transcribed raw, `parser: raw`.

Sampling: `temperature 0.1` (the official LightOnOCR client's setting for
v3 — "use temperature 0.1, not greedy decoding"; the card's example: 0.2),
`top_p 0.9`, penalties neutral, `stream false`, `cache_prompt false`
(avoids the recurring-state position warnings Qwen3.5 logs when requests
share a prefix — a throughput trade-off, not a quality one).

Reply hygiene: markdown-trained output escapes the punctuation that
follows inline math (`$x$\.` `$x$\,`); `stripEscapedPunctuation`
(`VisionChatModel`) unescapes it on every page/block reply, the same fix
the official client applies as `fix_dollars`.

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
- **Block crops are out of distribution too.** The model is a page model:
  the official pipeline feeds whole 2048 px pages and has no crop mode —
  footnote/page_number/formula blocks all arrive through grounding. In the
  Oct-2026 block-recognition benchmark (OCRtest) small crops came back as
  English figure descriptions ("### Figure – Pixelated Number…") and long
  formulas truncated; expect weak blockRecognition results from this model
  and use pageOcr/rawPageOcr instead.
- The GGUF repository is a third-party packaging (`prithivMLmods`), not
  LightOn's own.
