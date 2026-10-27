# Model: MiniCPM-OCR 1.0

> Manual launch, request contract and app wiring. Russian version:
> `docs/30-minicpm-ocr.ru.md`. The companion decision model (visual-diff
> verification) is MiniCPM-OCR Jev — `docs/31-minicpm-jev.md`.

## 1. What it is

**MiniCPM-OCR 1.0** (OpenBMB, 1.3B parameters) is the first OCR-specific
model of the MiniCPM-V/o family: SigLIP vision encoder + a Qwen3.5 language
trunk (24 layers, 6 of them full attention), trained for text-centric
document parsing — text, tables, formulas, charts, seals — for Chinese and
English documents. It reports 93.68 on Real5-OmniDocBench. Apache-2.0.

The page layout in the upstream pipeline is a separate model
(PP-DocLayoutV3) that ships **outside** the GGUF, so in LLocr the model
serves **blockRecognition** and **rawPageOcr** — the GLM-OCR shape. It has
no layout role: markup passes need teleocr or mineru.

## 2. Files

| file | size | purpose |
|---|---:|---|
| `MiniCPM-OCR-1_0-<quant>.gguf` | 0.50–1.52 GB | the model; quants f16/q8_0/q6_k/q5_k_m/q4_k_m are curated in the profile |
| `mmproj-model-f16-4x.gguf` | 0.90 GB | vision projector, **mandatory**; the 4×-downsample projector is the only one the repo ships |
| `dspark-MiniCPM-OCR-1_0-F16.gguf` | 0.61 GB | the DSpark drafter — a draft, not a standalone model |

- Repositories: `openbmb/MiniCPM-OCR-1.0-gguf` (model + projector) and
  `openbmb/MiniCPM-OCR-1.0-DSpark-gguf` (drafter). The profile
  `resources/profiles/models/minicpm-ocr.json` names both; the installer
  downloads the drafter into the model's own directory.
- llama.cpp build **no older than b11822** (22 Oct 2026: PR #30979
  registered the `minicpm_ocr` model type — the MiniCPM-V 4.6 stack under a
  different `model_type` — and PR #30980 added Jev on top of it).

## 3. Launch

The profile ships five flags per role (both roles declare the same list, so
a role switch never reloads the model):

```
llama-server \
  --model <modelsDir>/openbmb__MiniCPM-OCR-1.0-gguf/MiniCPM-OCR-1_0-Q8_0.gguf \
  --mmproj  <modelsDir>/openbmb__MiniCPM-OCR-1.0-gguf/mmproj-model-f16-4x.gguf \
  --model-draft <modelsDir>/openbmb__MiniCPM-OCR-1.0-gguf/dspark-MiniCPM-OCR-1_0-F16.gguf \
  --alias llocr-blockrecognition \
  --host 127.0.0.1 --port <auto> \
  --n-gpu-layers 99 --parallel 1 --cache-reuse 0 --no-context-shift \
  --flash-attn on \
  --reasoning off \
  --spec-type draft-dspark --spec-draft-n-max 15 --spec-draft-n-min 0
```

Two flags are the model's own requirement, not a tuning knob:

- **`--reasoning off`** (llama.cpp PR #30979). The model writes its
  transcription **inside the thinking block**: with the default `auto` the
  reply comes back as empty `content` and the text sits in
  `reasoning_content`, which the app reads as an empty reply. (The embedded
  chat template already disables thinking generation; this flag moves the
  answer into `content`.)
- **`--spec-type draft-dspark`** + the two budget flags. The DSpark drafter
  is the LFM2.5 DSpark mechanism (an absolute `d2t` target-token table);
  the drafter was trained with block size 16 — the upstream vLLM config
  runs `num_speculative_tokens: 15`. DSpark requires a matching main and
  draft from the same release.

No `--image-min-tokens`/`--image-max-tokens`: the MiniCPM projector slices
images itself (up to 9 slices at 448 px, 4× downsampled — roughly 2560
visual tokens on a dense page), and the server guide uses no budget flags.

## 4. Request contract

- The trained prefixes select the task, and the prompt must match the crop
  (the base card): `Text Recognition:` for text/paragraph/title/page,
  `Table Recognition:` for tables, `Formula Recognition:` for display
  formulas, `Seal Recognition:` for seals. The blockRecognition role maps
  the app's block types onto these four; `rawPageOcr` sends the page with
  `Text Recognition:`.
- The reply is the text and nothing else — read raw (`parser: raw`), the
  GLM-OCR shape. Tables come out as HTML (the SDK post-processes them),
  formulas as LaTeX with `$…$`/`$$…$$` delimiters.
- Sampling: `temperature 0`, neutral penalties, `stream false`,
  `cache_prompt true`.
- KIE (field extraction into a JSON object) and `Chart to Table:` are
  trained prompts the profile does **not** wire — LLocr has no task for
  them.

## 5. Verification notes

Recognized blocks go through the usual decision/fix pipeline. The
companion quality-assessment model **MiniCPM-OCR Jev** (docs/31) is
trained on MiniCPM-OCR 1.0's parses — pairing the two is the intended
configuration.

## 6. Settings in the app

- Tasks — **blockRecognition** and **rawPageOcr** («Модели» window); the
  task defaults stay glm-ocr.
- Installation via Settings → Models (HF repository
  `openbmb/MiniCPM-OCR-1.0-gguf`), the wizard's hardware step, or the
  drafter downloads with the model automatically.

## 7. Known limitations

- **No layout.** PP-DocLayoutV3 is not part of the GGUF — the profile
  deliberately does not declare the layout task.
- **Chinese and English only.** The model is trained on zh/en documents;
  other languages are out of distribution.
- DSpark decoding is exact under greedy decoding only; the temperature is
  pinned to 0 in the profile for that reason.
- Q4_K_M may drop closing-tag slashes in code/XML-heavy documents (the
  card's quantization table) — prefer q8_0 for such pages.
- The memory block of the profile is an estimate calibrated on the CPU
  probe (docs/27 §4–5); rerun `scripts/measure-memory.sh` if the launch
  flags change.
