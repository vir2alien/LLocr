# Model: PaddleOCR-VL-1.6

> Manual launch, request contract and app wiring. Russian version:
> `docs/25-paddleocr-vl.ru.md`.

## 1. What it is

**PaddleOCR-VL-1.6** (PaddlePaddle, ~0.9B parameters) is a compact
element-recognition VLM. Its VLM answers six trained element prompts (OCR,
table, formula, chart, seal, spotting); the page layout in the upstream
pipeline is a separate RT-DETR model that ships **outside** the GGUF. In
LLocr it therefore serves **blockRecognition** only — transcribing a block
crop by type. Apache-2.0 licensed.

## 2. Files

| file | size | purpose |
|---|---|---|
| `PaddleOCR-VL-1.6-GGUF.gguf` | ~0.94 GB | the model — the only quant the official repo ships (f16) |
| `PaddleOCR-VL-1.6-GGUF-mmproj.gguf` | ~0.88 GB | vision projector, **mandatory** |

- Repository: `PaddlePaddle/PaddleOCR-VL-1.6-GGUF` — the official
  packaging (profile `resources/profiles/models/paddleocr-vl.json`). The
  official repo is pinned for both files: the quant ladder lives nowhere
  else, and a preset downloads everything from one repo.
- llama.cpp build **no older than b8200** (PR #18825, 19 Feb 2026 — the
  vision-encoder fix this model needs).

## 3. Launch

The model layer ships **no** launch flags — the card's server invocation is
`--temp 0` with no system message, and the temperature rides in the request
layer; everything else is the platform + policy layers:

```
llama-server \
  --model <modelsDir>/PaddlePaddle__PaddleOCR-VL-1.6-GGUF/PaddleOCR-VL-1.6-GGUF.gguf \
  --mmproj <modelsDir>/PaddlePaddle__PaddleOCR-VL-1.6-GGUF/PaddleOCR-VL-1.6-GGUF-mmproj.gguf \
  --alias llocr-blockrecognition \
  --host 127.0.0.1 --port <auto> \
  --n-gpu-layers 99 --flash-attn on --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

## 4. Request contract

- No system message. One chat request per block: `[image_url crop, text
  <type prompt>]`.
- Type prompts are the card's own: `OCR:` for text, `Table Recognition:`,
  `Formula Recognition:`, `Seal Recognition:`; every other label of the
  markup vocabulary maps onto these four (the profile carries the full
  map, including `equation_block` → the formula prompt).
- The reply is the block text and nothing else — read raw (`parser: raw`).
  Upstream asks tables in OTSL and converts client-side; LLocr's prompt
  map keeps the model's own table prompt only if it yields usable markup —
  the profile is the single place to adjust it.
- Sampling: `temperature 0`, neutral penalties, `stream false`.

## 5. Verification notes

No dedicated calibration; recognized blocks go through the usual
decision/fix pipeline (docs/20; the decision task speaks the visual diff of minicpm-jev, ADR 254).

## 6. Settings in the app

- Task — **blockRecognition** («Модели» window); the task's default is
  mineru (ADR 252).
- Installation via Settings → Models (HF repository
  `PaddlePaddle/PaddleOCR-VL-1.6-GGUF`) or the wizard's hardware step.

## 7. Known limitations

- **No layout.** The layout model of the upstream pipeline is not part of
  the GGUF — the profile deliberately does not declare the layout task;
  markup passes need mineru (the archive's teleocr carried it before).
- Single f16 quant: no smaller quants exist to step down to; the model is
  small enough that this rarely matters. The file name carries no quant
  token, so the quantization comes from the model profile.
- The card's chart and spotting prompts are not wired — LLocr has no
  chart-transcription or full-image-spotting task for blocks.
