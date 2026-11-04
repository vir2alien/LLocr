# 13. MinerU2.5-Pro-2605-1.2B

> Self-sufficient manual for MinerU2.5-Pro-2605-1.2B as LLocr uses it: which
> tasks it serves, how to launch `llama-server` for it, how to compose its
> requests by hand. Russian mirror: `docs/13-model-mineru.ru.md`.

## 1. What it is and which tasks it serves

**MinerU2.5-Pro-2605-1.2B** (opendatalab, Apache-2.0) is a small
document-parsing VLM trained on the two-step mineru-vl-utils protocol:
layout detection over the 1036²-squashed page, then per-block transcription
by type. No single-shot page mode exists in its prompt set.

Of LLocr's four tasks (`docs/09-tasks-and-verification.md`) it serves two,
and it is the **default model for both** (ADR 252):

| task | role in the app | parser |
| --- | --- | --- |
| `layout` | the «Разметить страницу» markup pass (page toolbar, ADR 149–150) | `mineru-layout` |
| `blockRecognition` | per-block transcription after markup («Распознать», «Распознать блоки») | `raw` |

Both roles ship an identical launch layer, so the markup → recognition
sequence never reloads the weights. The model is not used for `decision`
(that speaks the visual diff of MiniCPM-OCR Jev) or `fix`. In the app the
layout pass runs `LayoutController` → `LayoutModel`, the block pass
`CheckController` → `BlockRecognitionModel`; both build the body through
`VisionChatModel::buildRoot` (`src/models/`). Profile:
`resources/profiles/models/mineru.json`.

## 2. Files and download

Repository: `mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF` (a third-party GGUF
packaging; the original weights are `opendatalab/MinerU2.5-Pro-2605-1.2B`).
The profile declares `minBuild: b4000`.

| file | size (profile `sizeBytes`) | purpose |
| --- | --- | --- |
| `MinerU2.5-Pro-2605-1.2B.Q8_0.gguf` | 531,066,976 (~531 MB) | the recommended quant |
| `MinerU2.5-Pro-2605-1.2B.Q6_K.gguf` | 505,735,264 | alternative quant |
| `MinerU2.5-Pro-2605-1.2B.Q5_K_M.gguf` | 420,084,832 | alternative quant |
| `MinerU2.5-Pro-2605-1.2B.Q4_K_M.gguf` | 397,806,688 | alternative quant |
| `MinerU2.5-Pro-2605-1.2B.Q3_K_M.gguf` | 355,465,312 | alternative quant |
| `MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf` | 709,409,600 (~709 MB) | vision projector — **mandatory** |

Without the mmproj the model sees no image (the profile's own note). The
app installs the files via Settings → Models; on disk the repo lands as
`<modelsDir>/mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF/…` — the `/` of the
repo id becomes `__` (`ModelInstallTransaction::repoDirName`). The default
models dir is `<AppData>/llocr/LLM OCR/models`
(`QStandardPaths::AppDataLocation` + `models`).

## 3. Launching llama-server

The app assembles the command in `ServerLaunchConfig::toArguments()`
(`src/runtime/ServerLaunchConfig.cpp`): the core flags first (`--model
--mmproj --alias --host --port`), then parameters sorted by `order` from
three layers — the shared **policy**
(`resources/profiles/serverLaunch.json` → `policy.parameters`), the
**platform profile** (same file, keyed by os + backend) and the **model
layer** (`roles.<task>.launch` in the model profile). A model-layer flag
replaces a same-named platform row (`LaunchProfileStore::compose`); the
alias is a label and stays out of the restart comparison, so one running
server serves both roles.

Windows, CUDA (the `win-cuda` platform profile), layout role:

```bat
llama-server.exe ^
  --model "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF\MinerU2.5-Pro-2605-1.2B.Q8_0.gguf" ^
  --mmproj "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF\MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf" ^
  --alias llocr-layout ^
  --host 127.0.0.1 --port 8081 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 ^
  --special
```

For the blockRecognition role everything above is identical except
`--alias llocr-blockrecognition`. The app's default port is `0`
(auto-pick); `8081` stands for any free port. macOS Metal and Linux CUDA
run the same flags; Windows/Linux Vulkan set `--flash-attn off`
(experimental there); the `cpu` profile drops `--n-gpu-layers`, sets
`--flash-attn off` and `--ctx-size 8192`.

| flag | value | layer that sets it | why |
| --- | --- | --- | --- |
| `--model` | path to the quant | core — the task's launch settings | the weights |
| `--mmproj` | path to the projector | core — the task's launch settings | vision projector, mandatory |
| `--alias` | `llocr-layout` / `llocr-blockrecognition` | core — `roles.<task>.alias` | the name the request's `model` field carries |
| `--host` | `127.0.0.1` | core — settings (`launchHost`) | local server |
| `--port` | `8081` (default `0` = auto) | core — settings (`launchPort`) | |
| `--parallel` | `1` | policy (`serverLaunch.json`) | one concurrent slot |
| `--cache-reuse` | `0` | policy | no context-reuse window between requests |
| `--no-context-shift` | — | policy | keep the context stable, no KV shift on overflow |
| `--no-warmup` | — | policy | skip the warmup run at load (faster start) |
| `--verbosity` | `3` | policy | server log level for the log window |
| `--n-gpu-layers` | `99` | platform `win-cuda` | all layers offloaded to the GPU |
| `--flash-attn` | `on` | platform `win-cuda` | |
| `--ctx-size` | `16384` | platform `win-cuda` | prompt + image + output |
| `--special` | — | model layer — `roles.layout.launch` and `roles.blockRecognition.launch` | render special tokens in the reply |

**`--special` is mandatory.** The layout reply *is* special-token markup —
`<|box_start|>x1 y1 x2 y2<|box_end|><|ref_start|>TYPE<|ref_end|>` — and
table replies carry the OTSL cell tokens (`<fcel>`/`<ecel>`/`<nl>`); the
server strips special tokens by default, and a stripped reply parses as
empty. This was the root cause of the first «no layout blocks» failures.

The 1036² page squash is done by the caller (`LayoutController`
hard-resizes the page to 1036×1036 without keeping the aspect, mirroring
the upstream calibration; normalized block coordinates are invariant to the
squeeze) — do not send native-resolution pages to the layout prompt.

## 4. Composing the request

`POST <server>/v1/chat/completions` with `Content-Type: application/json`;
`Authorization: Bearer <key>` is sent only when a key is configured
(external mode — the managed local server needs none). The image goes
**before** the text (the profile sets no `promptBeforeImage`), encoded as a
PNG data URL. The builder (`VisionChatModel::buildRoot`) always writes
`chat_template_kwargs.enable_thinking`; the profile declares no reasoning,
so it is always `false`. The request never carries the block's previous
text — the crop is the only source of truth (ADR 163).

**layout** — the system message is `roles.layout.systemPrompt`, the page
image is 1036², the prompt is `roles.layout.prompts[0]`:

```json
{
  "model": "llocr-layout",
  "messages": [
    { "role": "system", "content": "You are a helpful assistant." },
    { "role": "user", "content": [
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64>" } },
      { "type": "text", "text": "\nLayout Detection:" }
    ] }
  ],
  "temperature": 0.0,
  "top_p": 0.01,
  "repeat_penalty": 1.0,
  "presence_penalty": 0.0,
  "frequency_penalty": 0.0,
  "stream": false,
  "cache_prompt": true,
  "max_tokens": 4096,
  "chat_template_kwargs": { "enable_thinking": false }
}
```

**blockRecognition** — the block crop plus the per-type prompt from
`blockPrompts`: text-like kinds → `\nText Recognition:`, `table` →
`\nTable Recognition:`, `equation`/`formula` → `\nFormula Recognition:`
(note the leading `\n`; every label the layout pass can name has an entry):

```json
{
  "model": "llocr-blockrecognition",
  "messages": [
    { "role": "system", "content": "You are a helpful assistant." },
    { "role": "user", "content": [
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64>" } },
      { "type": "text", "text": "\nTable Recognition:" }
    ] }
  ],
  "temperature": 0.0,
  "top_p": 0.01,
  "repeat_penalty": 1.0,
  "presence_penalty": 1.0,
  "frequency_penalty": 0.05,
  "stream": false,
  "cache_prompt": true,
  "max_tokens": 8192,
  "chat_template_kwargs": { "enable_thinking": false }
}
```

- `model` is the alias the server was started with (Managed) or the task's
  model name (External).
- `presence_penalty 1.0` / `frequency_penalty 0.05` on blockRecognition are
  the upstream anti-loop set that stops table and formula repetition; the
  layout role keeps both at neutral.
- `stream: false` gives the parser one complete response; `cache_prompt:
  true` keeps the image embeddings and the system message cached between
  pages and blocks. Both come from the profile's `request` list like every
  sampling parameter above.

**Reply shapes.**

- layout: one block per line,
  `<|box_start|>x1 y1 x2 y2<|box_end|><|ref_start|>TYPE<|ref_end|>`,
  optionally followed by a rotation token (`<|rotate_up|>` and friends).
  The `mineru-layout` parser reads the lines structurally — nothing may
  strip the service tokens before it. The parser then absorbs covered
  children into composite containers (cover ratio ≥ 0.9: `equation_block` →
  `equation`/`text`, `image_block` → `image`/`chart`/`image_caption`,
  `list` → `text`/`list_item`) and styles the labels via the profile's
  `blocks.styles` (`doc_title`/`paragraph_title` → heading,
  `caption`/`footnote`/`header`/`footer` → italic, `list`/`list_item` →
  text, …).
- blockRecognition: plain block text, read raw — tables as OTSL
  (`<fcel>`/`<ecel>`/`<nl>`), formulas as LaTeX; a whole-reply Markdown
  fence is stripped.

Live-checked on the eq_hard corpus: the layout pass returns positioned
blocks and the absorption merges covered children (22 → 18 boxes on the
reference page); recognized blocks then go through the usual decision/fix
pipeline.

## 5. Budgets

| budget | value | source |
| --- | --- | --- |
| context window | 16384 on GPU platforms, 8192 on `cpu` | `serverLaunch.json` platform profiles |
| `max_tokens`, layout | 4096 | `roles.layout.maxOutput` |
| `max_tokens`, blockRecognition | 8192 | `roles.blockRecognition.maxOutput` |
| image tokens, layout page | 1369 (the 37² grid of the 1036² input) | `memory.visionTokens`, used by the memory estimate (`ModelProfiles::visionTokensFor`) |
| image tokens, block crop | ~512 (the app's default estimate) | `kDefaultCropTokens` — the profile sets no `image-min-tokens` |

The role's `maxOutput` is injected into every request as `max_tokens` by
the per-task request profile (`ModelProfiles::requestWithMaxOutput`).
Neither role declares reasoning, so the budget travels unchanged — the
`RequestBudget` clipping of ADR 193 belongs to the fix task. The guard is
the server's context window itself: a 1036² page (~1369 vision tokens)
plus a 4096-token answer fits 16384 with room to spare; the `cpu` profile
(8192) leaves less headroom.

## 6. Known limitations

- **Two-step only.** There is no trained single-shot page prompt; asking
  for raw page transcription is out of distribution.
- The reply markup is special vocabulary — any new consumer of the layout
  reply must bypass the service-token sanitizer (the `<|...|>`-stripping
  regex eats the data).
- The GGUF repository is a third-party packaging (`mradermacher`).
- **The MLX conversion of MinerU2.5 loses Cyrillic.** Measured on
  `mlx-community/MinerU2.5-2509-1.2B-bf16` served by omlx: the layout reply
  arrives transliterated (Latin look-alikes, letters spaced out), while the
  same page through llama.cpp (this profile's GGUF) reads correctly. A
  text-only request to the MLX build answers in the same transliteration,
  so the defect is in that conversion's tokenizer — use a llama.cpp build
  of MinerU, or another model, for Russian documents.

## 7. Links

- GGUF: <https://huggingface.co/mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF>
- Original weights: <https://huggingface.co/opendatalab/MinerU2.5-Pro-2605-1.2B>
- Related manuals: `14-model-glm-ocr.md` (the other recognition model),
  `09-tasks-and-verification.md` (the four tasks and the pipeline),
  `18-memory-estimate.md` (what VRAM the server needs).
- Russian mirror: `docs/13-model-mineru.ru.md`.
