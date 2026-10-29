# Model: Unlimited-OCR

> Manual launch, request contract and app wiring. Russian version:
> `docs/22-unlimited-ocr.ru.md`. The upstream reference (links, token
> vocabulary, hand-run example): `docs/UnlimitedOCR.md`.

## 1. What it is

**Unlimited-OCR** is Baidu's document-parsing VLM: one pass over the page
returns labeled blocks with coordinates (text, title, table, formula,
image, caption, header/footer, page number, list, reference, code,
abstract, seal). It fills the **pageOcr** task — recognition with markup —
and is the task's default model (ADR 169). Apache-2.0 licensed.

## 2. Files

| file | size | purpose |
|---|---|---|
| `Unlimited-OCR-Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/IQ4_XS) | 2.9 GB (Q8_0) | the model |
| `mmproj-Unlimited-OCR-F16.gguf` | ~0.9 GB | vision projector, **mandatory** |

- Repository: `sahilchachra/Unlimited-OCR-GGUF` (profile
  `resources/profiles/models/unlimited-ocr.json`); the original weights are
  `baidu/Unlimited-OCR`.
- Without the mmproj the model sees no image — the reply degrades to
  garbage labels.

## 3. Launch

App layering (the profile's model layer adds the four flags on top of the
platform `n-gpu-layers`/`flash-attn` and the policy `parallel`/`cache-reuse`
/`no-context-shift`/`no-warmup`):

```
llama-server \
  --model <modelsDir>/sahilchachra__Unlimited-OCR-GGUF/Unlimited-OCR-Q8_0.gguf \
  --mmproj <modelsDir>/sahilchachra__Unlimited-OCR-GGUF/mmproj-Unlimited-OCR-F16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <auto> \
  --image-min-tokens 456 --image-max-tokens 1156 \
  --dry-sequence-breaker none --special \
  --n-gpu-layers 99 --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--image-min-tokens 456 --image-max-tokens 1156`** — vision budget
  tuned for this model; the wide page layouts it produces need the room,
  and the native minimum (16 tokens) is too coarse for small text.
- **`--special`** — the reply is det-token markup and the parser needs the
  raw markers; the server strips special tokens by default.
- **`--dry-sequence-breaker none`** — the DRY repetition penalty (see §4)
  tracks repeats across the whole reply; the default breaker list would
  reset it at every newline and let table rows loop.
- ctx-size follows the machine (ADR 126); the reference run uses 16384 with
  `max_tokens` 8192 (`maxOutput` in the profile).

## 4. Request contract

One chat-completions request per page: system message, then
`[image_url data-URI, text "document parsing."]`. The reply is parsed
outside the model adapter by the `unlimited-ocr` det-token parser
(`llocr_parsers`), which turns the labeled lines into positioned blocks.

Request-layer sampling (the profile's `request` list) is the reference
run's anti-loop set: `temperature 0`, DRY `multiplier 0.8 / base 1.75 /
allowed_length 35 / penalty_last_n 128`, `stream false`.

The label → style map ships empty (`"blocks": {"styles": {}}`): the
parser's compiled-in defaults already style the vocabulary of §1
(documented in `docs/UnlimitedOCR.md`).

## 5. Verification notes

No dedicated calibration was run for this model — pages recognized with it
go through the usual decision/fix pipeline (docs/18, docs/20).

## 6. Settings in the app

- Task — **pageOcr** («Модели» window): keys `tasks/pageOcr/modelPath`,
  `tasks/pageOcr/mmprojPath`; Unlimited-OCR is the task's default model.
- Installation via Settings → Models (HF repository
  `sahilchachra/Unlimited-OCR-GGUF`) or the wizard's hardware step.
- External mode: any OpenAI-compatible server carrying the model; the
  parser side stays the same.

## 7. Known limitations

- The GGUF repository is a third-party packaging (`sahilchachra`), not
  Baidu's own — quantization provenance is the packager's.
- The DRY set and the vision budget were tuned once on the reference
  machine; if tables still loop, `dry_multiplier` is the first knob.
- `skip_special_tokens: false` from the reference request is covered by the
  launch flag `--special` (§3) — do not double-configure it.
