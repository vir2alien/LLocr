# Model: MinerU2.5-Pro-2605-1.2B

> Manual launch, request contract and app wiring. Russian version:
> `docs/24-mineru.ru.md`.

## 1. What it is

**MinerU2.5-Pro-2605-1.2B** (opendatalab) is a small document-parsing VLM
trained on the two-step mineru-vl-utils protocol: layout detection over the
1036²-squashed page, then per-block transcription by type. No single-shot
page mode exists in its prompt set. In LLocr it serves **layout** (the
«Разметить страницу» pass) and **blockRecognition** (the second step);
pageOcr/rawPageOcr stay unclaimed. Apache-2.0 licensed.

## 2. Files

| file | size | purpose |
|---|---|---|
| `MinerU2.5-Pro-2605-1.2B-Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/Q3_K_M) | 1.6 GB (Q8_0) | the model |
| `MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf` | ~0.5 GB | vision projector, **mandatory** |

- Repository: `mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF` (profile
  `resources/profiles/models/mineru.json`); the original weights are
  `opendatalab/MinerU2.5-Pro-2605-1.2B`.

## 3. Launch

The model layer ships one flag; everything else is platform + policy:

```
llama-server \
  --model <modelsDir>/mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF/MinerU2.5-Pro-2605-1.2B-Q8_0.gguf \
  --mmproj <modelsDir>/mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF/MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf \
  --alias llocr-layout \
  --host 127.0.0.1 --port <auto> \
  --special \
  --n-gpu-layers 99 --flash-attn on --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--special` is mandatory.** The layout reply IS special-token markup —
  `<|box_start|>x1 y1 x2 y2<|box_end|><|ref_start|>TYPE<|ref_end|>` — and
  the server strips special tokens by default. This was the root cause of
  the first «no layout blocks» failures (the flag had to be measured, see
  the debugging trail in the profile history).
- Both roles share the identical flag set, so the markup → recognition
  sequence never reloads the weights.
- The 1036² page squash is done by the app (`LayoutController`), mirroring
  the upstream calibration — do not send native-resolution pages to the
  layout prompt.

## 4. Request contract

- **layout** — system message «You are a helpful assistant.», then
  `[image_url (1036² page), text "\nLayout Detection:"]`; sampling
  `top_p 0.01`, `temperature 0`. The `mineru-layout` parser
  (`llocr_parsers`) reads the `<|box_start|>` lines structurally — the
  model adapter must NOT strip them (that sanitizer was the second layer
  of the same failure). The parser then absorbs covered children into
  composite containers (equation_block → equation, image_block → image,
  list → text/list_item; ≥ 0.9 coverage, container strictly larger) and
  relabels them.
- **blockRecognition** — same envelope, text part
  `"\nText Recognition:"` / `"\nTable Recognition:"` /
  `"\nFormula Recognition:"` by block type; the profile carries a prompt
  for every label the layout pass can name, plus
  `presence_penalty 1.0` / `frequency_penalty 0.05` (the upstream
  anti-loop set). Reply is read raw.

The layout vocabulary is styled in the profile: `doc_title`/
`paragraph_title` → heading, `caption`/`footnote`/`header`/`footer` →
italic, `list`/`list_item` → text, and so on.

## 5. Verification notes

Live-verified on the eq_hard corpus: layout returns positioned blocks,
absorption merges covered children (22 → 18 boxes on the reference page).
Recognized blocks go through the usual decision/fix pipeline (docs/18,
docs/20).

## 6. Settings in the app

- Tasks — **layout**, **blockRecognition** («Модели» window); layout's
  default stays teleocr, blocks' default stays glm-ocr.
- Installation via Settings → Models (HF repository
  `mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF`) or the wizard's hardware
  step.
- The markup pass itself: page toolbar «Разметить страницу» (ADR 149–150).

## 7. Known limitations

- **Two-step only.** There is no trained single-shot page prompt; asking
  for raw page transcription is out of distribution.
- The reply markup is special vocabulary — any new consumer of the layout
  reply must bypass the service-token sanitizer (the `<|...|>`-stripping
  regex eats the data).
- The GGUF repository is a third-party packaging (`mradermacher`).
