# 17. MiniCPM-OCR Jev

> Self-sufficient manual for MiniCPM-OCR Jev as LLocr uses it: which task it
> serves, how to launch `llama-server` for it, how to compose its requests by
> hand. Facts are from the model card, the llama.cpp PR, or measured in this
> project. Russian mirror: `docs/17-model-minicpm-jev.ru.md`. The OCR model it
> was trained alongside — MiniCPM-OCR 1.0 — is cut into the archive:
> `archive/models/docs/30-minicpm-ocr.md` (ADR 251).

## 1. What it is and which task it serves

**MiniCPM-OCR Jev** (OpenBMB, 2B parameters, Apache-2.0) is an OCR **quality-assessment**
model for text, formula and table regions: a Qwen3.5-2B trunk whose `lm_head` is
replaced with a **binary classification head**. Given a region crop, the candidate
OCR text and the image that candidate paints, it answers one question — *is this
OCR text consistent with the original image?* — with **P(consistent), a score from
0 to 1, in a single forward pass** (no generated tokens). Decision AUROC 93.6 (×100)
on JevOCRBench, 91.9–93.4 against the parses of MiniCPM-OCR 1.0, GLM-OCR,
PaddleOCR-VL-1.6 and Xiaomi-OCR — hence the generalization across LLocr's parsers.
It is the first **visual-diff** decision model in LLocr: the check compares the crop
with the candidate *rendered back into an image*, not the candidate text read as
characters.

Of LLocr's four tasks it serves **decision** — the verification pass («Проверить»),
of which it is the default and only model (ADR 252/254); the whole pass runs the
visual-diff protocol. It can serve no other task (no `lm_head` — text generation
answers 501), and the orientation check does not use the decision model at all
(ADR 242). Configuration and installation live in Settings → «Модели». The profile
(`resources/profiles/models/minicpm-jev.json`) declares the protocol
**`systemone_visual_diff_v1`** (`visualDiffDecisionProtocol()` in
`core/DecisionRequest.h`); `DecisionModel::buildRequestBody` branches on it,
requests go to `/v1/systemone` (a llama.cpp extension), and the role ships **no
request profile** — `test_model_preset_catalog` pins that emptiness.

## 2. Files and download

Repository: [`openbmb/MiniCPM-OCR-Jev-gguf`](https://huggingface.co/openbmb/MiniCPM-OCR-Jev-gguf).
The profile declares `minBuild: b11928`: llama.cpp learned the model (the `systemone`
template in the GGUF, the mapped classification head, the `/v1/systemone` decision
server) on 27 Oct 2026, PR #30980, and b11928 is exactly the release whose merge
commit is that PR — the first build that recognizes the model. Upstream's own
requirement is llama.cpp ≥ v0.8.0, the release that carries the support.

| file | size (profile `sizeBytes`) | purpose |
| --- | --- | --- |
| `MiniCPM-OCR-Jev-Q8_0.gguf` | 2,012,021,120 (~2.0 GB) | the recommended quant, sha256 `7e5227dd…` |
| `MiniCPM-OCR-Jev-Q6_K.gguf` | 1,556,399,488 (~1.56 GB) | |
| `MiniCPM-OCR-Jev-Q5_K_M.gguf` | 1,411,129,728 (~1.41 GB) | |
| `MiniCPM-OCR-Jev-Q4_K_M.gguf` | 1,274,405,248 (~1.27 GB) | |
| `MiniCPM-OCR-Jev-IQ4_XS.gguf` | 1,201,869,184 (~1.20 GB) | |
| `mmproj-model-f16.gguf` | 671,372,416 (~0.67 GB) | vision projector — **mandatory**, sha256 `cad9364b…` |

Hardware, as upstream states it: an NVIDIA card with ≥ 8 GB of VRAM (driver ≥ 535,
CUDA ≥ 12.0) or an Apple-silicon machine with ≥ 16 GB of unified memory.

## 3. Launching llama-server

The app assembles the command in `ServerLaunchConfig::toArguments()`
(`src/runtime/ServerLaunchConfig.cpp`): the core flags first (`--model --mmproj
--alias --host --port`), then parameters sorted by `order` from three layers — the
shared **policy** (`serverLaunch.json` → `policy.parameters`), the **platform
profile** (same file, keyed by os + backend) and the **model layer**
(`roles.decision.launch`); a model-layer flag replaces a same-named platform row
(`LaunchProfileStore::compose`). Windows, CUDA (the `win-cuda` platform profile):

```bat
llama-server.exe ^
  --model "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\openbmb__MiniCPM-OCR-Jev-gguf\MiniCPM-OCR-Jev-Q8_0.gguf" ^
  --mmproj "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\openbmb__MiniCPM-OCR-Jev-gguf\mmproj-model-f16.gguf" ^
  --alias llocr-decision ^
  --host 127.0.0.1 --port 8081 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 --batch-size 2048 --ubatch-size 512
```

| flag | value | layer that sets it | why |
| --- | --- | --- | --- |
| `--model` / `--mmproj` | paths to the quant and the projector | core — the task's launch settings | the weights; the projector is **mandatory** — without it the model sees no image |
| `--alias` | `llocr-decision` | core — `roles.decision.alias` | the name the request's `model` field carries |
| `--host` / `--port` | `127.0.0.1` / `8081` (default `0` = auto-pick) | core — settings (`launchHost` / `launchPort`) | local server |
| `--parallel` | `1` | policy (`serverLaunch.json`) | one concurrent slot |
| `--cache-reuse` | `0` | policy | no context-reuse window between requests |
| `--no-context-shift` | — | policy | keep the context stable, no KV shift on overflow |
| `--no-warmup` | — | policy | skip the warmup run at load (faster start) |
| `--verbosity` | `3` | policy | server log level for the log window |
| `--n-gpu-layers` | `99` | platform `win-cuda` | all layers offloaded to the GPU |
| `--flash-attn` | `on` | **model layer** — replaces the platform row by name | the default off path materializes the full attention buffer and inflates the memory requirement |
| `--ctx-size` | `16384` | **model layer** — replaces the platform row (which also says 16384) | the request carries two images, so it is much longer than a text-only one; 16384 covers it (§5) |
| `--batch-size` | `2048` | model layer | prompt processing batch, sized for the two-image prompt |
| `--ubatch-size` | `512` | model layer | keeps the working set on the card instead of splitting the graph onto the host (below) |

macOS Metal and Linux CUDA run the same flags; Windows/Linux Vulkan and `cpu` set
`--flash-attn off` in the platform layer — the model layer overrides it back to
`on` (§7), and `cpu` has no `--n-gpu-layers` at all.

The upstream recipe — **not** what the app runs:

```
llama-server \
  -m ./models/MiniCPM-OCR-Jev-F16.gguf \
  --mmproj ./models/mmproj-model-f16.gguf \
  -ngl 99 \
  -c 32768 \
  --port 18499
```

`-ngl 99` and `--mmproj` mean the same here; `-c 32768` is upstream's context — the
app carries 16384 instead (§5). The batches are sized down from what the model
could take: at `--ubatch-size 8192` llama.cpp moves 768 MiB of the 2.1 GiB working
set onto the host; at 512 the whole 68 MiB stays on the GPU — on a 6 GiB card,
3407 vs 4895 MiB of VRAM, and the auto-fit passes at load instead of aborting.

The engine has to recognize the model at startup: the log must show `decision model
type: openbmb-minicpm-ocr-jev` and `listening on`, and `GET /health` must answer
`{"status":"ok"}` — both lines are in the app's server log window.

## 4. Composing the request

`POST <server>/v1/systemone` with `Content-Type: application/json` — a llama.cpp
extension, not OpenAI chat. `Authorization: Bearer <key>` goes only when a key is
configured (external mode; the managed local server needs none).

**Why a separate protocol, not a request profile.** The System One decision models
the app shipped before (clef-flash, d3-flash/d3-mini — in the archive, ADR 251) read
a **structured state** (block type, candidate, notation axes) and answer a
**client-worded question** through their trained noul head. Jev cannot do that: the
task wording is **baked into the model's prompt** (the training prompt: original
image → `OCR文本：<text>` → rendered image → the binary instruction), so the server
rejects a request carrying `instructions` or a noul question with `criteria` — a
request profile with its own texts would be dead weight. The wire shape differs
too: the state is the **bare OCR text as a string** (not the structured object),
and the request carries **exactly two images** — the original crop first, then the
render of the candidate. What stays is the *answer*: a noul head that returns
P(class 1) with zero generated tokens — the same `answers.match` the app already
parses, and the same Ok/Mismatch threshold routing.

The server enforces this shape for this model (llama.cpp
`tools/server/server-decision.cpp`, the `openbmb-minicpm-ocr-jev` branch). Every
line below is a 400, not a silent fallback:

| the request | the server's complaint |
|---|---|
| `state` is not a string | `"state" must be the OCR text` |
| more than one question | `this model answers exactly one question` |
| a question carrying `instructions` | `"instructions" is not used by this model, the task is part of its prompt` |
| a noul question carrying `criteria` | `"criteria" is not supported for a noul question` |
| any number of images other than two | `this model needs exactly 2 images (the original, then the rendered one), got N` |

The app sends the text as it is; the server's special-token-breaking mechanism is
enabled for the LFM (in the archive) and H2O decision models only — for Jev the OCR
text reaches the template untouched ("`ocr_text` goes to the model as it is", the
upstream contract).

The body (`DecisionModel::buildRequestBody` in `src/models/DecisionModel.cpp`; the
request struct is `DecisionRequest` in `src/core/DecisionRequest.h`):

```json
{
  "model": "llocr-decision",
  "state": "<the candidate text, verbatim>",
  "images": ["data:image/png;base64,<crop>", "data:image/png;base64,<render>"],
  "questions": { "match": { "type": "noul" } }
}
```

`model` names the served model — a server that routes by the field rejects a request
without it (Managed: the alias; External: the task's model name). `state` is the
block's **effective** text (ADR 164) — the OCR result, or the fix model's correction
when it ran first — verbatim. `images` are exactly two data URLs, **the crop first,
the render second** — the order the training prompt fixes.

**The crop (the first image)** is the block's region cut out of the
**full-resolution** page image (`app/AppController` → `croppedImage`); turn and
deskew are applied to the image every consumer of the page sees
(`DocumentModel::renderFull`), so the crop is the very region the text was read
from.

**The render (the second image)** is the candidate text drawn as a picture — and
the render leg is what decides the verdict: the model compares the crop with the
picture, so a picture that lost its tail, or that shows the raw markup, is another
text. `app/CandidateRenderer` paints it with the app's own preview stack
(`qrc:/preview/preview.html`: marked + KaTeX + DOMPurify): the block's **display
text** (`parsers/BlockRender.h` → `renderBlockText` — a formula as `$$…$$` display
math, a table as a markdown grid; the raw text's delimiters `\[…\]`, `<fcel>…<nl>`
would kill the render) on a **780 px-wide** white page, printed to a PDF and
**rasterized at 2×** (capped at 4096 px per side). Chromium's print margins get
48 px of extra room — a second page is a failure (the block lands on Review), never
a picture — and the raster is cut back to the ink (2 px margin), because a
candidate framed by white margins is another region. The palette is forced light
(`{dark: false}` in the preview page's render call): without it the picture would
come out inverted in the dark theme while the crop stays dark-on-white. The
upstream training renderer (the SDK's Playwright+KaTeX) drew the same shapes; a
self-installed renderer shifts the absolute scores, so the thresholds are
calibrated on this app's own renders (§6).

The upstream's score service takes a `category` (`text` / `formula` / `table`) next
to the candidate and renders the candidate itself; on the llama.cpp path there is
no `category` field — the caller supplies the render, so the category becomes the
renderer's choice, taken from the block's label (`resources/profiles/labels.json`
maps `formula` to the `equation` style; the profile's `blockAliases` maps it for
the thresholds as well).

The render happens inside the verification queue, before the request: a block whose
render fails (or an app built without the renderer) lands on **Review** with a
footer note, never judged on half the input; a formula the app already rejected on
LaTeX syntax never reaches the stage (ADR 194) — which doubles as Jev's precheck.
(On the OCR_TEST corpus the broken render rejected 155 of 195 blocks, 97 % of the
OCR-correct ones among them; after the fix the same blocks score 0.92–0.99.)

**Reply.** The GGUF carries two classifier labels — *"the classes are fixed: '0' is
inconsistent, '1' is consistent"*. The noul answer is the probability of the option
`true` (the options of a noul question are `false` then `true`), so
`answers.match.noul` is the probability of the class **1**, that is P(consistent) —
the same number the upstream score service calls `prob_1`. The app also accepts a
bare number under `match`, or `{"probability": x}` (`DecisionModel::parseResponse`);
the aux answers (has_text/readable/diagnosis) never arrive — the routing falls back
to the plain threshold split.

One difference to keep in mind when reading the upstream docs: the upstream
**service** takes bare base64 (no `data:` prefix) in `original_image` /
`render_image`, while the **llama.cpp server** takes data URLs in `images` — the
app talks to the server directly, so it sends data URLs.

## 5. Budgets

| budget | value | source |
| --- | --- | --- |
| context window | 16384 in the app | `roles.decision.launch` (`ctx-size`) — the model row wins over any platform profile, including the `cpu` profile's 8192 |
| context window, upstream recipe | 32768 | the upstream `-c 32768` (§3) — headroom, not a requirement |
| generated tokens | **0** | the profile's `request` list is empty — the answer is computed by the noul classification head in one pass, so no `max_tokens` rides the wire and the `RequestBudget` machinery of ADR 193 does not apply |
| image tokens | 4096 budgeted per image; the mmproj keeps the checkpoint's own preprocessor budget (up to 16384 merged tokens per image) | `memory.visionTokens` / the projector |

A measured two-image request is **~6.7k tokens**, and 16384 covers it with room to
spare — a wider window buys only KV cache; `--parallel 1` keeps the one slot the
model needs.

## 6. Thresholds

The thresholds are **measured** on the OCR_TEST corpus (177 blocks inside the check
scope, MinerU2.5-Pro recognition, candidate renders by this app's own
`CandidateRenderer`, llama.cpp b11946; the run and the report live in
`OCRtest/decision_models/test 2/iteration8`; the ADR 184 rule — a threshold is
measured against the request that ships). A type's threshold is the knee of its
FN/FP(t) curve: the last step where one caught error costs at most one false alarm:

- **text 0.92**, **equation 0.87**, **table 0.4**, base (types without an entry)
  0.5 — exactly what the profile ships (`roles.decision.matchThresholdByBlock` /
  `matchThreshold`).
- On the corpus: **FN 10 (13.7 %) / FP 41 (39.4 %)** against FN 29 / FP 15 at the
  old 0.5; d3-mini scored FN 12 / FP 22 on the same corpus.
- The residual FNs are print homoglyphs (и/н, и/И, Π/II, ρ/p, г/r), integrals
  (∯→∬, ∭→∬∬) and lost overbars/punctuation — out of a visual diff's reach.
- **Tables are rejected outright** (all p < 0.02 at any threshold): the
  markdown-grid render is out of the model's distribution. The `table` entry is the
  edge of the measured region; every table keeps landing on Mismatch.

Empty/unreadable crops are not Jev's questions — there are no
`emptyThreshold`/`readableThreshold` values to ship.

## 7. Known limitations

- **No text generation** — the model cannot serve any other task, and its endpoint
  refuses `/v1/chat/completions` outright (501).
- **Three categories only.** The model was trained on text, formula and table
  regions; other block types get the role threshold, and a code block or a seal is
  out of distribution.
- **The thresholds hold for this app's render pipeline and corpus.** They are
  measured (§6), but the measurement rides on the OCR_TEST corpus and on the app's
  own renderer: documents whose typography differs sharply from the render
  (typewriter-style scans) push even correct text down the scale and land on
  Mismatch — the FP tail the thresholds cannot remove.
- **The memory block of the profile is derived, not measured**
  (`docs/18-memory-estimate.md` §6 marks the row `minicpm-jev` as derived), and its
  vision term is a **one-image** budget while a request always carries two images.
  Rerun `scripts/measure-memory.sh` if the launch flags change.
- **`--flash-attn on` overrides the platform layer** on the backends where the
  platform turns it off (Vulkan, CPU) — the same as the other vision models of the
  project (glm-ocr, qwen3.5; lightonocr, chandra-ocr-2 and minicpm-ocr are in the
  archive). On Vulkan it contradicts the platform's own note that flash attention
  there is experimental.
- **No self-check against the upstream's reference cases** (text, formula, table,
  with the reference score `observed_prob_yes`): a run through them would check the
  whole leg (crop → render → request → score) at once. The app has no such check
  today.

## 8. Links

- Model card: <https://huggingface.co/openbmb/MiniCPM-OCR-Jev> (Apache-2.0)
- GGUF: <https://huggingface.co/openbmb/MiniCPM-OCR-Jev-gguf>
- llama.cpp: support PR #30980 — build b11928 (27 Oct 2026), the first release that
  recognizes the model; upstream requires llama.cpp ≥ v0.8.0
- Related manuals: `09-tasks-and-verification.md` (the decision task and the
  verification pipeline), `18-memory-estimate.md` (§6 — the derived memory row),
  `archive/models/docs/30-minicpm-ocr.md` (the OCR model it was trained alongside)
- Russian mirror: `docs/17-model-minicpm-jev.ru.md`
