# Model: Qwen3-VL-4B / Qwen3-VL-8B

> Manual launch, request contract and app wiring. Russian version:
> `archive/models/docs/31-qwen3-vl.ru.md`.

## 1. What it is

**Qwen3-VL-4B** and **Qwen3-VL-8B** (Qwen, Instruct, Apache-2.0) are general
vision-language models. In LLocr they served the **fix** task — the correction
of an already recognized block — and the **read** task («Распознать (LLM)»),
which re-transcribes a block from the image alone.

The pair was cut by **ADR 335**: the quality and the speed did not hold up in
the measurements, and the fix task stays with the Qwen3.5 and Gemma-4
families. The profiles and the reader wording are kept here whole.

## 2. Files and download

Repositories (from `files.repo` in the profiles; `minBuild` **b9263**):

| Repo | File | Quant id | Size | Purpose |
| --- | --- | --- | --- | --- |
| [`Qwen/Qwen3-VL-4B-Instruct-GGUF`](https://huggingface.co/Qwen/Qwen3-VL-4B-Instruct-GGUF) | `Qwen3VL-4B-Instruct-Q8_0.gguf` | q8_0 | 3.99 GiB | the 4B, main quant |
| 〃 | `Qwen3VL-4B-Instruct-Q4_K_M.gguf` | q4_k_m | 2.33 GiB | the smaller quant |
| 〃 | `mmproj-Qwen3VL-4B-Instruct-F16.gguf` | f16 | 798 MiB | vision projector — **mandatory**, without it the model sees no image |
| [`Qwen/Qwen3-VL-8B-Instruct-GGUF`](https://huggingface.co/Qwen/Qwen3-VL-8B-Instruct-GGUF) | `Qwen3VL-8B-Instruct-Q8_0.gguf` | q8_0 | 8.11 GiB | the 8B, main quant |
| 〃 | `Qwen3VL-8B-Instruct-Q4_K_M.gguf` | q4_k_m | 4.68 GiB | the smaller quant |
| 〃 | `mmproj-Qwen3VL-8B-Instruct-F16.gguf` | f16 | 1105 MiB | vision projector — **mandatory** |

- No `files.mtp` entry: no speculative decoding — the pair is a plain dense
  model. The app lays a repo out as `<modelsDir>/<org>__<repo>/…`, e.g.
  `<modelsDir>/Qwen__Qwen3-VL-4B-Instruct-GGUF/Qwen3VL-4B-Instruct-Q8_0.gguf`
  (`ModelInstallTransaction::repoDirName`).

## 3. Launching llama-server

One concrete command — **Qwen3-VL-4B** on **macOS · Metal** (the `macos-metal`
platform profile), exactly as `ServerLaunchConfig::toArguments`
(`src/runtime/ServerLaunchConfig.cpp`) builds it: core fields first, then the
composed parameters in order:

```
llama-server \
  --model <modelsDir>/Qwen__Qwen3-VL-4B-Instruct-GGUF/Qwen3VL-4B-Instruct-Q8_0.gguf \
  --mmproj <modelsDir>/Qwen__Qwen3-VL-4B-Instruct-GGUF/mmproj-Qwen3VL-4B-Instruct-F16.gguf \
  --alias llocr-fix --host 127.0.0.1 --port 8080 \
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 \
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 \
  --temp 0 --jinja --image-min-tokens 1024
```

The three layers are merged by `LaunchProfileStore::compose`: **policy**
(`serverLaunch.json → policy.parameters`), **platform**
(`serverLaunch.json → profiles[macos-metal]`) and **model**
(`qwen3-vl-4b.json → roles.fix.launch`).

| Flag | Value | Layer | Why |
| --- | --- | --- | --- |
| `--model` / `--mmproj` | paths | core | weights + projector; no `--model-draft` |
| `--alias` | `llocr-fix` | core | the role's `alias`; the request's `model` field routes on it |
| `--host` / `--port` | `127.0.0.1` / pick one | core | settings defaults (`127.0.0.1`, `0` = auto-pick) |
| `--parallel` | `1` | policy | a single decoding slot |
| `--cache-reuse` | `0` | policy | context reuse between requests disabled |
| `--no-context-shift` | flag | policy | keep the context stable; no KV shift on overflow |
| `--no-warmup` | flag | policy | skip the warmup run at load |
| `--verbosity` | `3` | policy | server log level |
| `--n-gpu-layers` | `99` | platform | all layers on the GPU |
| `--flash-attn` | `on` | platform (restated in model) | flash attention kernels; required for the vision stack |
| `--ctx-size` | `16384` | platform | prompt + image + output window (8192 on the CPU profile) |
| `--temp` | `0` | model | server-side greedy; corrections must be deterministic |
| `--jinja` | flag | model | the chat template from the GGUF metadata |
| `--image-min-tokens` | `1024` | model | llama.cpp warns that Qwen-VL needs at least 1024 image tokens for grounding tasks |

The **8B needs the same launch set**; only the memory budget differs (the
wizard's auto-selection steps down quants to fit).

## 4. Composing the request

Transport: `FixModel` over `VisionChatModel` (`src/models/`),
`POST /v1/chat/completions`, one request per block, `stream: false`. The
system prompt and the per-block prompt come from the shared request profile
`fix-requests.json` — the pair shipped no wording of its own for the fix task,
so the shared texts answered (ADR 182). The image is the block crop as a PNG
data URL; the candidate is the block's effective text, a formula already in
canonical LaTeX (ADR 194).

**Fix protocol** (`full_block`): the reply is `OK`, `FIX` + the corrected block,
or `REVIEW` — the whole first line owns the keyword (ADR 332). The request
parameters the profiles carry:

| Parameter | Value | Why |
| --- | --- | --- |
| `temperature` | `0.0` | deterministic greedy decoding |
| `repeat_penalty` | `1.0` | disables the repetition penalty |
| `presence_penalty` | `0.0` | neutral |
| `frequency_penalty` | `0.0` | neutral |
| `stream` | `false` | the parser wants one complete reply |
| `cache_prompt` | `true` | the prompt (system message, image embeddings) is reused between blocks — a dense model can reuse it, unlike the hybrid-recurrent fix family |
| `stop` | `["</CANDIDATE>"]` | the protocol marker only: an end-of-text token in the stop list is matched against the reply itself and cuts the answer to nothing (ADR 249) |

`maxOutput` is 8192 tokens for both roles.

## 5. The read task: the family's own reader

The pair was the only shipped family that carried a **reader wording of its
own** (`readSystemPrompt` in the role): the shared read contract
(`read-requests.json`) plus one clause of the family's own calibration —
"Read glyphs at the printed size; never guess small print."

The reason is a measurement (ADR 267): the family read the shared wording as a
correction-verification and answered `REVIEW` on **every** legible block. The
own wording fixed that; ADR 334 later rewrote it into the read contract, so the
override was the shared contract plus the small-print clause and nothing more.

The rest of the read task is the shared one: the request is exactly the block
image, the system prompt and the block prompt — the recognized text never
travels (ADR 266) — and the reply is the block text, with `REVIEW` as the only
verdict (ADR 331).

## 6. Memory

The coefficients of the profiles (measured against llama.cpp b12000, ADR 191):

| Model | KV layers | KV heads × head dim | Vision | Working set |
| --- | --- | --- | --- | --- |
| `qwen3-vl-4b` | 36 | 8 × 128 | 159 809 B/token (2116) | 264 233 B/token + 2 B |
| `qwen3-vl-8b` | 36 | 8 × 128 | 184 383 B/token (2116) | 409 641 B/token + 2 B |

The requirement at `q8_0` (weights + projector + KV + working set): **7.96 GiB**
for the 4B and **12.84 GiB** for the 8B.

## 7. Settings in the app

- Task — **fix** («Модели» window); the task's default is `qwen3.5-4b`
  (ADR 252).
- The pair carried no `default` flag: it was offered in the fix list, never
  auto-selected.
- Installation via Settings → Models (the Hugging Face repositories of §2) or
  the wizard's hardware step, which picks the role-default model that fits
  0.9 × (VRAM ‖ RAM).

## 8. Known limitations

- **No thinking.** The Instruct line has no thinking channel, so the roles
  declare `reasoning: false` and the fix task spends no reasoning budget on
  them (ADR 268).
- The launcher needs llama.cpp **b9263** or newer.
- The reader wording of the family was the only one of its kind in the shipped
  catalog; every other fix model took the shared reader (ADR 267).
