# The VRAM requirement: how it is computed and how it was calibrated

> The number the quant dropdown shows (`VRAM ≥ 11.4 GiB`) is a property of the
> whole server, not of the file. This document explains what the number is made
> of, where every coefficient comes from, how to measure a model that is not in
> the catalog yet, and which traps lie between a measurement and a profile.
> Russian version: `docs/27-memory-estimate.ru.md`. The decision itself:
> ADR 191 in `docs/07-glossary.md`.

## 1. What the number means

The dropdown row of a quantization reads

```
✓ Q8_0 — 9.1 GiB · VRAM ≥ 11.4 GiB
```

The first number is the size of the file, the second is the **minimum video
memory the card must have** to run the model that file belongs to: the
weights, the projector, the draft and its own context, the KV cache, the
working set of the graph, the vision encoder and the server's own overhead.
The file is the smallest part of that in most cases.

Two paths lead to the same number:

- **the file** — a model that is on disk is read from its GGUF header
  (`ModelMemoryEstimator::shapeFromFile`);
- **the profile** — a quantization that is still a download has no header, so
  the model profile ships the architecture and the measured coefficients
  (`resources/profiles/models/<id>.json`, the `memory` block).

For the catalog the profile wins (`RuntimeController::estimateModelMemory`),
because it also carries what a header cannot say: the cost of the vision
encoder, the draft and the sliding-window cache.

The requirement is **not** the sum of the buffers. It is the sum plus the
server's overhead, so that the number names the card the model runs on rather
than the card it barely starts on:

```
required = buffers + (64 MiB + 8 % of the buffers)
```

## 2. The formula

Every term of `ModelMemoryEstimator::estimateMemory` (`src/runtime/`):

| Term | Formula | Where the coefficient comes from |
| --- | --- | --- |
| Weights | the size of the GGUF file | the file (`sizeBytes` of the quant in the profile) |
| Projector | the size of the `mmproj` file | the file (`files.mmproj.sizeBytes`) |
| Draft | the size of the draft file | the file (`memory.draftBytes`) |
| Draft context | `draftContextBytes + draftKvBytesPerToken × ctx` | measured (see §5) |
| KV cache | `kvBytesPerToken × ctx + kvBytesFixed` | the architecture, exact for the plain models |
| Working set | `ubatch × workingSetBytesPerToken + ctx × ubatch × workingSetContextBytes` | measured, or derived from the shape |
| Vision encoder | `visionTokens × visionBytesPerToken`, or `visionBufferBytes` | measured (see §4) |
| Overhead | `64 MiB + 8 %` | the server's own allocations and the allocator's slack |

The derived defaults, used when a profile carries no measured value:

```
kvBytesPerToken         = 2 × nKvLayers × nKvHead × headDim × bytesPerValue
workingSetBytesPerToken = (nEmbd × 24
                           + headDim × (nHead + 2 × nKvHead) × 4
                           + nFf × 8
                           + nMoeLayers × nExpertUsed × nExpertFf × 12) × 1.12
workingSetContextBytes  = 2            (the f16 attention mask per context token)
```

The 1.12 is the margin that turns a modelled working set into a requirement:
llama.cpp's scheduler reserves up to 12 % more than the shape predicts, and
the estimate must never under-promise.

The context window, the ubatch and the cache types come from the launch
profile (`ModelMemoryEstimator::budgetFromLaunch`): the platform profile owns
`ctx-size`, the model profile owns `ubatch-size`, and `cache-type-k/v = f32`
doubles the KV cache.

## 3. The `memory` block of a model profile

```json
"memory": {
  "nLayer": 48, "nKvLayers": 8, "nHead": 16, "nKvHead": 8, "headDim": 64,
  "nEmbd": 3840, "nFf": 15360,
  "visionTokens": 1120, "visionBytesPerToken": 55419,
  "draftBytes": 253708800, "draftKvBytesPerToken": 2048, "draftContextBytes": 387994092,
  "workingSetBytesPerToken": 267320, "workingSetContextBytes": 2,
  "kvBytesFixed": 1006632960
}
```

| Field | Meaning |
| --- | --- |
| `nLayer` | the decoder layers |
| `nKvLayers` | the layers whose cache **follows the context** (0 = all of them). A hybrid architecture keeps fewer: the rest hold a sliding window |
| `nHead`, `nKvHead` | the attention heads and the KV heads of those layers |
| `headDim` | the width of one KV head. The GGUF's `attention.key_length` — **the total across the heads is not the per-head width** (see the traps) |
| `nEmbd`, `nFf` | the embedding and the feed-forward width (the working set) |
| `nMoeLayers`, `nExpertUsed`, `nExpertFf` | the mixture-of-experts terms (the working set) |
| `visionTokens` | the image tokens the page task fills the budget with |
| `visionBytesPerToken` | the encoder's working set per image token |
| `visionBufferBytes` | the encoder's whole working set, for a projector whose cost does not follow the token count |
| `draftBytes` | the weights of the draft (a separate GGUF) |
| `draftKvBytesPerToken` | the KV cache of the **draft's own context**, per context token |
| `draftContextBytes` | what else that context holds: the sliding cells and the compute buffer |
| `workingSetBytesPerToken` | the measured cost of one token of the graph's working set (0 = derive it) |
| `workingSetContextBytes` | the measured cost of one context token of the attention mask (0 = 2) |
| `kvBytesPerToken` | the measured KV cache per context token (0 = derive it) |
| `kvBytesFixed` | the sliding layers' cells: they do **not** follow the context |

## 4. Measuring a model that is not in the catalog

Two scripts do the work:

- `scripts/measure-memory.sh` — starts llama-server with the flags the app
  uses and writes the log;
- `scripts/memory-from-log.py` — reads a log (or several) and prints the
  numbers, including the fields of the `memory` block.

```sh
scripts/measure-memory.sh \
  --model "LiquidAI__LFM2.5-VL-3B-GGUF/LFM2.5-VL-3B-Q8_0.gguf" \
  --mmproj "LiquidAI__LFM2.5-VL-3B-GGUF/mmproj-LFM2.5-VL-3B-F16.gguf" \
  --draft  "LiquidAI__LFM2.5-VL-3B-GGUF/LFM2.5-VL-3B-DSpark-F16.gguf" \
  --ctx 16384 --flash-attn on --spec-type draft-dspark --spec-draft-n-max 8 --special
```

```
-- kv caches (main context)
      256.00 MiB   16384 cells    8 layers   -> 16384 B/token (follows the context)
-- vision (clip)
   warmup image 512x512   patch 16   merge 2   -> 1024 patches = 256 tokens
   vision           30.31 MiB   -> 124150 B/token
-- profile fields
   kvBytesPerToken 16384
   draftKvBytesPerToken 8192
   draftContextBytes 533190410
   draftBytes 567205280
   visionBytesPerToken 124150
```

An existing log is parsed without running anything:

```sh
scripts/measure-memory.sh --parse build/measure/app-gemma4-12b.log
```

### The flags

The measurement must use the flags the app launches the server with, because
they change the reservations:

| Flag | Where it comes from | Why it matters |
| --- | --- | --- |
| `--parallel 1 --cache-reuse 0 --no-context-shift` | the launch policy (`resources/profiles/serverLaunch.json`) | one context, no reuse |
| `--ctx-size` | the platform profile (16384 on macOS) | the KV cache and the mask scale with it |
| `--ubatch-size` | the model profile (512 by default, 2048 for the gemma-4 12B, 6144–8192 for the decision models) | the working set scales with it |
| `--flash-attn on\|off` | the platform profile, overridden by the model profile (the gemma-4 E4B turns it **off**) | with flash attention off the encoder keeps an attention matrix over the whole page |
| `--image-min-tokens` / `--image-max-tokens` | the model profile | the page budget of the model |
| `--spec-type` / `--model-draft` | the model profile | the draft gets a context of its own |
| `--special` | the model profile (MinerU, LFM2.5-VL, Qwen3.5) | some models need it |
| `--device none --n-gpu-layers 0` | the measurement only | the buffer sizes are the same, and the numbers are readable without a card |

### The log lines

| Line | What it gives |
| --- | --- |
| `llama_context: n_ctx / n_ubatch / n_batch` | the budget the server used |
| `print_info: n_layer / n_embd / n_head / n_head_kv / n_embd_head_k / n_swa` | the architecture |
| `llama_kv_cache: size = X MiB (N cells, L layers)` | the KV caches: the one with `N == n_ctx` follows the context, the others are the sliding window's cells |
| `sched_reserve: CPU compute buffer size = X MiB` | the working set (the first one is the main context, the ones after `loading draft model` are the draft's) |
| `reserve_compute_meta: CPU compute buffer size = X MiB` | the vision encoder (the first one) and, for the gemma-4, the audio tower (the second) |
| `get_dummy_batch: warmup with image size = W x H` | the image the vision reservation was made for |
| `load_hparams: patch_size / n_merge` | the geometry of the patch grid |
| `[mtmd] estimated worst-case memory usage of mmproj is X MiB` | the projector weights plus the clip reservation — a free cross-check |

### The vision encoder

The clip graph's reservation is driven by the **pixel grid of the image**, not
by the token budget. The number of image tokens an image yields is

```
tokens = (W / patch_size) × (H / patch_size) / n_merge²
```

so the rate of a projector that scales with the tokens is

```
visionBytesPerToken = the clip reservation / tokens
```

The warmup image is scaled by the server to the model's image budget, so the
measurement has to run with the budget the app uses (the page budget for a
page task). A model whose projector does not follow the token count carries
`visionBufferBytes` instead — see the traps.

## 5. The draft's own context

A draft that is a **separate GGUF** (the LFM2.5-VL's DSpark drafter, the
gemma-4 MTP drafters) runs in a context of its own: llama.cpp gives it its
own KV cache and its own compute buffer. The profile carries all three parts:

```
draftBytes          the weights of the draft file
draftKvBytesPerToken the KV cache of that context, per context token
draftContextBytes   the rest of that context: the sliding cells and the
                    compute buffer of the speculative graph
```

The numbers come from the log: the caches after `loading draft model` and the
largest `sched_reserve` after it (the speculative graph reserves more than the
draft alone).

The MTP block **inside** a model (the Qwen3.5) is a layer of the model, not a
context of its own: it has no cache and no compute buffer of its own.

## 6. The calibration of the shipped models

The coefficients of every model of the catalog, as they are in the profiles
(measured against llama.cpp b11799, `--parallel 1`, the app's flags):

| Model | KV layers | KV heads × head dim | Vision | Working set | Draft (weights / context) | Sliding cells |
| --- | --- | --- | --- | --- | --- | --- |
| `glm-ocr` | 16 | 8 × 128 | 238 856 B/token (6144) | derived | — | — |
| `teleocr` | 28 | 8 × 128 | 282 280 B/token (1369) | derived | — | — |
| `mineru` | 24 | 2 × 64 | 132 324 B/token (1369) | derived | — | — |
| `paddleocr-vl` | 18 | 2 × 128 | 142 872 B/token (1369) | derived | — | — |
| `qwen3.5-4b` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `qwen3.5-9b` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `clef-flash` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `d3-flash` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `d3-mini` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `lightonocr` | 8 | 4 × 256 | 110 600 B/token (1024) | derived | — | — |
| `minicpm-ocr` | 6 | 2 × 256 | 133 612 B/token (256, the 448² warmup slice; the profile's page budget is 2560) | derived | 605 MB / 1 036 MB + 10 240 B/token | — |
| `minicpm-jev` | 6 | 2 × 256 | 110 655 B/token (2116) | derived | — | — |
| `lfm25-vl-3b` | 8 | 8 × 64 | 124 150 B/token (256) | derived | 567 205 280 / 533 190 410 | — |
| `gemma-4-12b` | 8 | 8 × 64 | 55 419 B/token (1120) | 267 320 B/token + 2 B | 253 708 800 / 387 994 092 | 1 006 632 960 |
| `gemma-4-e4b` | 4 | 2 × 512 | **3 986 933 350 flat** | 176 218 B/token + 36 B | 59 678 016 / 188 743 680 | 62 914 560 |
| `unlimited-ocr` | 12 | 10 × 128 | **2 981 117 952 flat** | derived (MoE) | — | — |

The requirement that follows from these coefficients, for the quantization each
model's manual names as the main one (the sizes in GiB, the buffers in MiB):

| Model | Quant | Weights | Proj | KV | Work | Vision | Draft | Draft ctx | Total | **Requirement** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `clef-flash` | q8_0 | 9.01 | 876 | 256 | 2063 | 108 | 0 | 0 | 12.24 | **13.28 GiB** |
| `d3-flash` | q8_0 | 8.87 | 595 | 256 | 2063 | 108 | 0 | 0 | 11.83 | **12.83 GiB** |
| `d3-mini` | q4_k_m | 3.45 | 350 | 256 | 1144 | 108 | 0 | 0 | 5.27 | **5.75 GiB** |
| `gemma-4-12b` | q4_k_xl | 6.26 | 167 | 1216 | 586 | 59 | 242 | 402 | 8.86 | **9.64 GiB** |
| `gemma-4-e4b` | q4_k_xl | 3.93 | 944 | 316 | 374 | 3802 | 57 | 244 | 9.53 | **10.35 GiB** |
| `glm-ocr` | q8_0 | 0.89 | 462 | 1024 | 65 | 1400 | 0 | 0 | 3.77 | **4.13 GiB** |
| `lfm25-vl-3b` | q8_0 | 2.68 | 814 | 256 | 97 | 30 | 541 | 636 | 5.00 | **5.46 GiB** |
| `lightonocr` | q8_0 | 4.17 | 644 | 512 | 103 | 432 | 0 | 0 | 5.83 | **6.36 GiB** |
| `mineru` | q8_0 | 0.49 | 677 | 192 | 52 | 173 | 0 | 0 | 1.56 | **1.75 GiB** |
| `paddleocr-vl` | f16 | 0.87 | 841 | 288 | 48 | 187 | 0 | 0 | 2.20 | **2.44 GiB** |
| `qwen3.5-4b` | q8_0 | 4.29 | 641 | 512 | 103 | 108 | 0 | 0 | 5.63 | **6.14 GiB** |
| `qwen3.5-9b` | q8_0 | 9.11 | 876 | 512 | 137 | 108 | 0 | 0 | 10.71 | **11.63 GiB** |
| `teleocr` | q8_0 | 0.75 | 1266 | 1792 | 52 | 369 | 0 | 0 | 4.15 | **4.54 GiB** |
| `unlimited-ocr` | q8_0 | 2.91 | 774 | 960 | 459 | 2843 | 0 | 0 | 7.83 | **8.52 GiB** |

The profiles carry the rates rounded: the measurements were 110 653 B/token
for the Qwen tower, 238 848 for the GLM-OCR tower, 282 313 for the TeleOCR one,
132 351 for the MinerU one and 142 842 for the PaddleOCR-VL one.

The draft's own KV rate (`draftKvBytesPerToken`): 8 192 B/token for the DSpark
drafter, 2 048 for the gemma-4-12B's MTP drafter, 4 096 for the gemma-4-E4B's.

The decision models (`clef-flash`, `d3-flash`, `d3-mini`) look expensive
because their launch profile asks for a large ubatch (8192 and 6144 tokens):
the working set is the largest term. The page models look cheap because the
vision encoder is small at their budget, and expensive when the projector
tiles the page (`unlimited-ocr`).

## 7. The traps

Every item here cost a measurement to find.

1. **The dummy image is not the real page.** The clip reservation is made for
   the warmup's dummy image, which the server scales to the model's image
   budget. The rate is the reservation divided by the **tokens that image
   yields** (`(W/patch) × (H/patch) / n_merge²`), not by the budget: the
   gemma-4-12B's warmup is 768×768 with a patch of 48 and a merge of 1 — 256
   tokens, not 1120.

2. **With flash attention off the encoder keeps the whole attention matrix.**
   The gemma-4-E4B runs with `--flash-attn off`, so the cost is quadratic in
   the number of patches. Its warmup reservation (276.77 MiB at 2304 patches)
   is the linear part plus the attention term; the attention term is the
   difference to the flash-on run (101.27 MiB) and it scales with the square:

   ```
   linear    = 101.27 MiB / 2304 patches      = 46 089 B/patch
   attention = (276.77 − 101.27) MiB / 2304²  = 35 B/patch²
   the app's page: 1120 tokens × 9 patches = 10 080 patches
   → 46 089 × 10 080 + 35 × 10 080² = 3 802 MiB  (visionBufferBytes 3 986 933 350)
   ```

   `scripts/memory-from-log.py --app-patches N` computes this from the two
   logs (the flash-on one and the flash-off one).

3. **A projector that tiles the page does not scale with the token count.**
   The Unlimited-OCR projector cuts the page into three windows plus a global
   view: the cost follows the pixels of the tiles (9600 patches), not the
   tokens. It carries `visionBufferBytes` (2 981 117 952) instead of a rate.

4. **The KV cache of a hybrid architecture is two caches.** The gemma-4 models
   keep a cache for the full-attention layers (it follows the context) and a
   second one for the sliding layers (a fixed number of cells: 3072 cells for
   the 12B, 1024 for the E4B). The profile carries the fixed part apart
   (`kvBytesFixed`); the rest scales with the context.

5. **The key length is the total across the heads.** The gemma-4-12B's key
   width was four times the real one, and its cache was overstated by 4 GiB.
   The per-head width is what the cache holds:
   `headDim = the K bytes per cell per layer ÷ 2 (f16) ÷ nKvHead`.

6. **The layers with a KV cache are not the layers of the model.** In a
   recurrent hybrid the state lives in SSM tensors and those layers carry no K/V
   at all (the LFM2.5-VL: 8 of its 30 layers have a cache); a sliding-window
   layer carries K/V, but in a cache of its own (the gemma-4 models). The
   speculative-decoding head (the `nextn` block) is a draft layer of its own:
   llama.cpp runs it as a separate graph and keeps **no** cache for it.

7. **A draft that is a separate file has a context of its own** — with a KV
   cache and a compute buffer. The profile declared the draft's weights but
   did not read them for a long time: the draft cost nothing.

8. **The cells of the sliding window depend on the ubatch.** The number of
   cells the server allocates for a sliding layer is not the window size; it
   is a multiple of it that depends on the batch. Read it from the log, do not
   compute it.

9. **The speculative graph reserves more than the draft alone.** The LFM's
   draft reserves 258 MiB for itself and 508.49 MiB for the graph that runs
   the target and the draft together. The number that applies is the larger
   one.

10. **`--no-warmup` hides the vision reservation.** The app's launch policy
    skips the warmup, so the clip graph is reserved for the first real
    request instead. A measurement needs the warmup (the script runs it by
    default).

11. **The estimator's file path misstates a hybrid architecture.** The header
    path counts the layers that carry K and V tensors, while only the
    full-attention layers follow the context — for the gemma-4-12B that is 48
    layers instead of 8, so its cache comes out six times too big, and the
    fixed part of the sliding window is missed altogether. The catalog path
    (the profile) is authoritative.

12. **The image budget depends on the task.** A page task fills the model's
    own budget (`image-max-tokens`); a block crop is scaled up to the floor
    the model declares (`image-min-tokens`) and stays small otherwise. The
    number in the dropdown is the page budget — the worst case.

## 8. The checklist for a new model

1. **Install the model and the projector** (the app's Settings → Models), and
   the draft file if the model ships one.
2. **Read the architecture** from the GGUF header (or from the model card):
   the layers, the KV layers, the KV heads, the key length, the embedding and
   the feed-forward width, the mixture-of-experts terms.
3. **Measure** with the app's flags:
   ```sh
   scripts/measure-memory.sh --model "<repo>/<file>.gguf" \
       --mmproj "<repo>/<mmproj>.gguf" [--draft "<repo>/<draft>.gguf"] \
       --ctx <the platform's context> --flash-attn <on|off> \
       --image-max-tokens <the model's page budget> [--ubatch <the model's>]
   ```
4. **Check the KV cache** against the formula
   `2 × nKvLayers × nKvHead × headDim × 2 × ctx`: it must match the log. If it
   does not, the layer split or the head width is wrong (§7.4–7.6).
5. **Take the vision rate** from the log: the reservation ÷ the tokens of the
   warmup image. If the model runs with flash attention off, or its projector
   tiles the page, carry `visionBufferBytes` instead (§7.2–7.3).
6. **Take the working set** from the log: the `sched_reserve` line. A single
   log cannot separate the two terms — run a second one with a different
   ubatch or context and solve the two equations, or keep the default
   derivation and check that it is not below the measurement.
7. **Add the draft's three parts** if the model has one (§5).
8. **Write the `memory` block** into the profile and the curated sizes into
   `files` (the quant's `sizeBytes`, the projector and the draft).
9. **Check the result**: the requirement must be above the total of the
   buffers, and the requirement of the largest quant must be above the card the
   model manual recommends. `test_model_memory_estimator` and
   `test_model_installer` guard the formula and the dropdown.
10. **Record the numbers** in this document's table (§6) and, if the model has
    its own manual, in it.

## 9. What the agent's shell cannot do

The sandbox of an agent session blocks the tools that measure the resident
memory, so the measurement has to come from llama.cpp's own report:

| Blocked | Instead |
| --- | --- |
| `ps`, `pgrep`, `pkill` | read the server's own log; the buffers are printed there |
| `/usr/bin/time -l` (the RSS line needs a forbidden `sysctl`) | the `sched_reserve` and `reserve_compute_meta` lines |
| `DYLD_INSERT_LIBRARIES` (the malloc interposer is stripped) | the reservations in the log |
| writing to `/tmp` | the `build/` directory (gitignored) or any other writable path |
| `timeout` | `sleep` + `kill $!` |
| Metal (the card is not available to the agent) | `--device none --n-gpu-layers 0`: the sizes are the same |
| a background task that outlives the call | one call with a long enough timeout — a parallel build of the same tree destroys the other one |

The `build/` directory holds the logs of the measurements
(`build/measure/*.log`). They are gitignored and lost with a clean tree — the
numbers that matter are in the profiles and in §6.
