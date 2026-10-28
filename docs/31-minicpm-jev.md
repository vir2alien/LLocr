# Model: MiniCPM-OCR Jev

> Manual launch, request contract and app wiring. Russian version:
> `docs/31-minicpm-jev.ru.md`. The OCR model it was trained alongside is
> MiniCPM-OCR 1.0 — `docs/30-minicpm-ocr.md`.

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
d3-flash/d3-mini) read a **structured state** (block type, candidate,
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

So the model profile declares the **`decision`** role with the protocol
**`systemone_visual_diff_v1`** (`visualDiffDecisionProtocol()` in
`core/DecisionRequest.h`), and `DecisionModel::buildRequestBody` branches on
it. There is deliberately no request profile: the role ships no
`statePrompt`/`question`/`criteria`, and `test_model_preset_catalog` pins
that emptiness.

## 3. The render leg

The second image is the candidate text drawn as a picture — and the render leg
is what decides the verdict: the model compares the crop with the picture, so a
picture that lost its tail, or that shows the raw markup, is another text.

`app/CandidateRenderer` produces it with the app's own preview stack — the
markdown page (`qrc:/preview/preview.html`: marked + KaTeX + DOMPurify)
renders the text on a 780 px-wide white page, the page is printed to a PDF and
rasterized at 2×. Three conditions make the picture comparable with the crop
(ADR 237):

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

The state carries the block's own text (the upstream contract: the OCR text
goes to the model as it is); the picture is painted from the display text.

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
  (profile `resources/profiles/models/minicpm-jev.json`).
- llama.cpp build **no older than b11928** (27 Oct 2026, PR #30980). The
  conversion writes the `systemone` template into the GGUF and maps the
  classification head; text generation answers 501 — the model has no LM
  head.

```
llama-server \
  --model <modelsDir>/openbmb__MiniCPM-OCR-Jev-gguf/MiniCPM-OCR-Jev-Q8_0.gguf \
  --mmproj <modelsDir>/openbmb__MiniCPM-OCR-Jev-gguf/mmproj-model-f16.gguf \
  --alias llocr-decision \
  --host 127.0.0.1 --port <auto> \
  --n-gpu-layers 99 --parallel 1 --cache-reuse 0 --no-context-shift \
  --ctx-size 32768 --batch-size 8192 --ubatch-size 8192 --flash-attn on
```

The profile ships `ctx-size/batch-size/ubatch-size/flash-attn` — the
llama.cpp README for the model warns that a two-image prompt is much longer
than a text-only one, so the context and batches are sized accordingly.

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

## 6. Thresholds

The profile ships the upstream service default **0.5** — one value, with
`blockAliases` mapping `formula` → `equation` and per-block entries for the
three trained categories (text/equation/table). The upstream docs are
explicit that scores are not bitwise reproducible and that the render
environment shifts the absolute values, so **do not trust the split until a
run through `scripts/decision-eval/score-project.py` +
`scripts/decision-calibrate.py` recalibrates the per-block thresholds on
this app's own renders** (the ADR 184 rule: a threshold is measured against
the request that ships). Empty/unreadable crops are not Jev's questions —
there are no `emptyThreshold`/`readableThreshold` values to ship.

## 7. Settings in the app

- Task — **decision** («Модели» window). The task's default stays
  clef-flash; picking Jev switches the whole verification pass to the
  visual-diff protocol.
- Installation via Settings → Models (HF repository
  `openbmb/MiniCPM-OCR-Jev-gguf`).
- The orientation check (`askChoice`) still sends its own choice question —
  the server rejects it with 400 while Jev is the decision model; run the
  orientation check on a System One decision model.

## 8. Known limitations

- **No text generation** — the model cannot serve any other task, and its
  endpoint refuses `/v1/chat/completions` outright (501).
- **Three categories only.** The model was trained on text, formula and
  table regions; other block types get the role threshold, and a code block
  or a seal is out of distribution.
- The candidate text rides the wire verbatim (the training data kept
  whitespace); the app does not strip or normalize it.
- The memory block of the profile is an estimate calibrated on the CPU
  probe (docs/27 §4–5); rerun `scripts/measure-memory.sh` if the launch
  flags change.
