# Model: MiniCPM-OCR Jev

> Manual launch, request contract and app wiring. Russian version:
> `docs/31-minicpm-jev.ru.md`. The OCR model it was trained alongside is
> MiniCPM-OCR 1.0 — `archive/models/docs/30-minicpm-ocr.md` (the model is
> cut, ADR 251).

## 1. What it is

**MiniCPM-OCR Jev** (OpenBMB, 2B parameters) is an OCR **quality-assessment**
model for text, formula and table regions. It is a Qwen3.5-2B trunk whose
`lm_head` is replaced with a **binary classification head**: given a region
crop, the candidate OCR text and the image that candidate paints, it answers
one question — *is this OCR text consistent with the original image?* — and
answers it with **P(consistent), a score from 0 to 1, in a single forward
pass** (no generated tokens). Decision AUROC 93.6 (×100) on JevOCRBench —
above models many times its size. Apache-2.0.

It is the first **visual-diff** decision model in LLocr: the check compares
the crop with the candidate *rendered back into an image*, not the candidate
text read as characters. That is why it generalizes across parsers (the card
reports 91.9–93.4 AUROC against MiniCPM-OCR 1.0, GLM-OCR, PaddleOCR-VL-1.6
and Xiaomi-OCR parses).

## 2. Why a separate protocol, not a request profile

The System One decision models the app shipped before (clef-flash,
d3-flash/d3-mini — in the archive, ADR 251) read a **structured state**
(block type, candidate,
notation axes) and answer a **client-worded question** through their trained
noul head. Jev cannot do that:

- The task wording is **baked into the model's prompt** (the training
  prompt: original image → `OCR文本：<text>` → rendered image → the binary
  instruction). The llama.cpp server rejects a request that carries
  `instructions`, and a noul question with `criteria` — a request profile
  with its own texts would be dead weight.
- The wire shape differs: the state is the **bare OCR text as a string**
  (not the structured object), and the request carries **exactly two
  images** — the original crop first, then the render of the candidate.
  One image or three: HTTP 400.
- What stays is the *answer*: a noul head that returns P(class 1) with zero
  generated tokens — the same `answers.match` the app already parses, and
  the same Ok/Mismatch threshold routing.

What the server enforces for this model
(`tools/server/server-decision.cpp`, the `openbmb-minicpm-ocr-jev` branch).
Every line below is a 400, not a silent fallback:

| the request | the server's complaint |
|---|---|
| `state` is not a string | `"state" must be the OCR text` |
| more than one question | `this model answers exactly one question` |
| a question carrying `instructions` | `"instructions" is not used by this model, the task is part of its prompt` |
| a noul question carrying `criteria` | `"criteria" is not supported for a noul question` |
| any number of images other than two | `this model needs exactly 2 images (the original, then the rendered one), got N` |

The app sends the text as it is and does nothing else to it. Note that the
server's mechanism for breaking special tokens inside the state is enabled for
the LFM (in the archive) and H2O decision models only — for Jev the OCR text
reaches the
template untouched, which is what the upstream contract asks for ("`ocr_text`
goes to the model as it is, nothing is cleaned up"). A `choice` question is
possible as well — it would have to carry `"criteria": {"0": null, "1": null}`
— but the noul hands over the probability directly, which is what the
threshold routing wants.

So the model profile declares the **`decision`** role with the protocol
**`systemone_visual_diff_v1`** (`visualDiffDecisionProtocol()` in
`core/DecisionRequest.h`), and `DecisionModel::buildRequestBody` branches on
it. There is deliberately no request profile: the role ships no texts at
all — no system prompt and no prompt profile, the wording is the model's
own — and `test_model_preset_catalog` pins that emptiness.

## 3. The two images

**The crop (the first image)** is the block's own region: the normalized rect
of the box cut out of the **full-resolution** page image (`app/AppController` →
`croppedImage`). The crop and the boxes share one coordinate system, because
the turn and the deskew are applied to the image that every consumer of the
page sees — the preview, the model and the block crops alike
(`DocumentModel::renderFull`). A page straightened after the recognition
therefore still hands the model the very region the text was read from.

**The render (the second image)** is the candidate text drawn as a picture —
and the render leg is what decides the verdict: the model compares the crop
with the picture, so a picture that lost its tail, or that shows the raw
markup, is another text.

`app/CandidateRenderer` produces it with the app's own preview stack — the
markdown page (`qrc:/preview/preview.html`: marked + KaTeX + DOMPurify)
renders the text on a 780 px-wide white page, the page is printed to a PDF and
rasterized at 2×. Four conditions make the picture comparable with the crop —
the first three are ADR 237's and the test holds them, the fourth keeps the
picture comparable with the crop in the dark theme too:

- **the text is the block's display text** (`parsers/BlockRender.h` →
  `renderBlockText`): a formula as display math (`$$…$$`), a table as a
  markdown grid. The raw block text carries the model's own delimiters
  (`\[…\]`, `<fcel>…<nl>`); marked eats the backslashes, KaTeX never fires,
  and the table arrives as a run-on line of cells. The upstream training
  renderer (the SDK's Playwright+KaTeX) drew the same shapes — a self-installed
  renderer shifts the absolute scores a little, so the thresholds are
  calibrated on this app's own renders, not copied from the SDK.
- **the page holds the whole candidate**: Chromium's print layout keeps its own
  margins, so a page cut to the measured content height is a hair shorter than
  the content — the last line falls onto a second page, and only the first
  page is rasterized. The page gets 48 px of room, and a second page is a
  failure (the block lands on Review), never a picture.
- **the picture is cut to the ink**: the crop the model sees is tight around
  the text, and a candidate framed by white margins is another region.
- **the palette is forced light**: the renderer calls the preview page with
  `{dark: false}` — a white background and dark text — whatever the app theme
  is. The preview page declares `color-scheme: light dark` and the app
  switches the scheme app-wide, so without that flag the picture would come
  out inverted (light text on a dark ground) in the dark theme, while the crop
  stays dark-on-white.

The state carries the block's own text (the upstream contract: the OCR text
goes to the model as it is); the picture is painted from the display text.
The text on the wire is the block's **effective** text — the OCR result, or
the fix model's correction when it ran first (ADR 164) — and the picture is
painted from that same text.

The upstream's score service takes a `category` (`text` / `formula` / `table`)
next to the candidate and renders the candidate itself; a category that does
not match the real type of the block raises no error but skews the score. On
the llama.cpp path there is no `category` field at all — the caller supplies
the render, so the category becomes the renderer's choice. The app takes it
from the block's label: `resources/profiles/labels.json` maps `formula` to the
`equation` style and `table` to the `table` style, and the profile's
`blockAliases` maps `formula` → `equation` for the thresholds as well.

Measured on the OCR_TEST corpus (195 blocks, MiniCPM-OCR 1.0 recognition, this
model deciding): with the broken render 155 of 195 blocks were rejected, and
97 % of the OCR-correct ones were false rejections. After the fix the same
blocks score 0.92–0.99 where the broken render gave 0.01–0.43.

The render happens inside the verification queue, before the request: a
block whose render fails (or an app built without the renderer) lands on
**Review** with a footer note instead of being judged on half the input.
A formula the app already rejected on LaTeX syntax never reaches the stage
(ADR 194) — which doubles as Jev's precheck.

## 4. Files and launch

| file | size | purpose |
|---|---:|---|
| `MiniCPM-OCR-Jev-<quant>.gguf` | 1.20–3.78 GB | the model; quants q8_0/q6_k/q5_k_m/q4_k_m/iq4_xs are curated in the profile |
| `mmproj-model-f16.gguf` | 0.67 GB | vision projector, **mandatory** |

- Repository: `openbmb/MiniCPM-OCR-Jev-gguf`
  (profile `resources/profiles/models/minicpm-jev.json`). The sizes and the
  sha256 of the profile match the repository file by file: `q8_0` is
  2 012 021 120 B (`7e5227dd…`), `q6_k` 1 556 399 488 B, `q5_k_m`
  1 411 129 728 B, `q4_k_m` 1 274 405 248 B, `iq4_xs` 1 201 869 184 B, and
  the projector 671 372 416 B (`cad9364b…`).
- llama.cpp build **no older than b11928** (27 Oct 2026, PR #30980). The
  conversion writes the `systemone` template into the GGUF and maps the
  classification head; text generation answers 501 — the model has no LM
  head. b11928 is exactly the release whose merge commit is PR #30980, so it
  is the first build that recognizes the model; upstream's own requirement is
  llama.cpp ≥ v0.8.0, the release that carries the support.
- The engine has to recognize the model at startup: the log must show
  `decision model type: openbmb-minicpm-ocr-jev` and `listening on`, and
  `GET /health` must answer `{"status":"ok"}`. Both lines are in the app's
  server log window.
- Hardware, as upstream states it: an NVIDIA card with ≥8 GB of VRAM (driver
  ≥535, CUDA ≥12.0) or an Apple-silicon machine with ≥16 GB of unified
  memory.

The upstream recipe for the llama.cpp backend:

```
llama-server \
  -m ./models/MiniCPM-OCR-Jev-F16.gguf \
  --mmproj ./models/mmproj-model-f16.gguf \
  -ngl 99 \
  -c 32768 \
  --port 18499
```

The app runs the same engine with the same values — `--mmproj` is what makes
the images visible at all, `-ngl 99` puts every layer on the GPU, `-c 32768`
is upstream's context — plus the app's own flags:

```
llama-server \
  --model <modelsDir>/openbmb__MiniCPM-OCR-Jev-gguf/MiniCPM-OCR-Jev-Q8_0.gguf \
  --mmproj <modelsDir>/openbmb__MiniCPM-OCR-Jev-gguf/mmproj-model-f16.gguf \
  --alias llocr-decision \
  --host 127.0.0.1 --port <auto> \
  --n-gpu-layers 99 --parallel 1 --cache-reuse 0 --no-context-shift \
  --ctx-size 16384 --batch-size 2048 --ubatch-size 512 --flash-attn on
```

The profile ships `ctx-size/batch-size/ubatch-size/flash-attn` — the
llama.cpp README for the model warns that a two-image prompt is much longer
than a text-only one, so the context and batches are sized accordingly.
A measured two-image request is ~6.7k tokens, and 16384 covers it with room
to spare. The batches are sized down from what the model could take: at
`--ubatch-size 8192` the working set is 2.1 GiB, of which llama.cpp moves
768 MiB onto the host — the graph is split across the card and the CPU.
At 512 the whole working set (68 MiB) stays on the GPU. Measured on a 6 GiB
card that is the difference between 4895 MiB and 3407 MiB of VRAM, and the
auto-fit llama.cpp runs at load then passes instead of aborting.
The context is shared by the parallel slots, and `--parallel 1` keeps the one
slot the model needs; `--cache-reuse 0` (no prefix reused between requests)
and `--no-context-shift` (no silent shift of the context) are the app's
policy layer.

The projector sets the image budget: the mmproj keeps the checkpoint's own
preprocessor budget (up to 16384 merged tokens per image), and a request
always carries **two** images — the crop and the render.

## 5. Request contract (the wire, after the render)

```json
{
  "model": "llocr-decision",
  "state": "<the candidate text, verbatim>",
  "images": ["data:image/png;base64,<crop>", "data:image/png;base64,<render>"],
  "questions": { "match": { "type": "noul" } }
}
```

- The response reads like any noul answer: `answers.match.noul` =
  P(consistent). The aux answers (has_text/readable/diagnosis) never arrive
  — the routing falls back to the plain threshold split.
- Sampling: nothing (zero generated tokens). The profile's `request` list
  is empty.

The answer's own scale: the GGUF carries two classifier labels, and the
server's comment on them is exact — *"the classes are fixed: '0' is
inconsistent, '1' is consistent"*. The noul answer is the probability of the
option `true`, and the options of a noul question are `false` then `true`,
so `answers.match.noul` is the probability of the class **1**, that is
P(consistent). It is the same number the upstream score service calls
`prob_1` — a score without a threshold and without a label.

One difference in the wire format to keep in mind when reading the upstream
docs: the upstream **service** takes bare base64 (no `data:` prefix) in
`original_image` / `render_image`, while the **llama.cpp server** takes data
URLs in `images`. The app talks to the server directly, so it sends data
URLs.

## 6. Thresholds

The thresholds are **measured** on the OCR_TEST corpus (177 blocks inside the
check scope, MinerU2.5-Pro recognition, candidate renders by this app's own
`CandidateRenderer`, llama.cpp b11946; the run and the report live in
`OCRtest/decision_models/test 2/iteration8`; the ADR 184 rule — a threshold is
measured against the request that ships). A type's threshold is the knee of its
FN/FP(t) curve: the last step where one caught error costs at most one false
alarm:

- **text 0.92**, **equation 0.87**, **table 0.4**, base (types without an
  entry) 0.5.
- On the corpus: **FN 10 (13.7 %) / FP 41 (39.4 %)** against FN 29 / FP 15 at
  the old 0.5; d3-mini scored FN 12 / FP 22 on the same corpus.
- The residual FNs are print homoglyphs (и/н, и/И, Π/II, ρ/p, г/r), integrals
  (∯→∬, ∭→∬∬) and lost overbars/punctuation — out of a visual diff's reach.
- **Tables are rejected outright** (all p < 0.02 at any threshold): the
  markdown-grid render is out of the model's distribution. The `table` entry
  is the edge of the measured region; every table keeps landing on Mismatch.

Empty/unreadable crops are not Jev's questions — there are no
`emptyThreshold`/`readableThreshold` values to ship.

## 7. Settings in the app

- Task — **decision** («Модели» window). Jev is the task's default and only
  model (ADR 252/254): the whole verification pass runs the visual-diff
  protocol.
- Installation via Settings → Models (HF repository
  `openbmb/MiniCPM-OCR-Jev-gguf`).
- The orientation check does not use the decision model at all (ADR 242): it
  reads the picture alone, so Jev is not involved.

## 8. Known limitations

- **No text generation** — the model cannot serve any other task, and its
  endpoint refuses `/v1/chat/completions` outright (501).
- **Three categories only.** The model was trained on text, formula and
  table regions; other block types get the role threshold, and a code block
  or a seal is out of distribution.
- The candidate text rides the wire verbatim (the training data kept
  whitespace); the app does not strip or normalize it.
- **The thresholds hold for this app's render pipeline and corpus.** They are
  measured (§6), but the measurement rides on the OCR_TEST corpus and on the
  app's own renderer: documents whose typography differs sharply from the
  render (typewriter-style scans) push even correct text down the scale and
  land on Mismatch — the FP tail the thresholds cannot remove. A corpus with
  another recognition (or a changed renderer) needs a re-run.
- **The memory block of the profile is derived, not measured** (docs/27 §6
  marks the row `minicpm-jev` as derived), and its vision term is a
  **one-image** budget while a request always carries two images. Rerun
  `scripts/measure-memory.sh` if the launch flags change.
- **`--flash-attn on` overrides the platform layer** on the backends where
  the platform turns it off (Vulkan, CPU) — the same as the other vision
  models of the project (glm-ocr, qwen3.5; lightonocr, chandra-ocr-2 and
  minicpm-ocr are in the archive). On Vulkan it contradicts the platform's own note that flash
  attention there is experimental.
- **No self-check against the upstream's reference cases.** Upstream ships
  three of them (text, formula, table) with the reference score
  `observed_prob_yes`; a run through them would check the whole leg (crop →
  render → request → score) at once. The app has no such check today.
