# 32. Page orientation correction: the full pipeline

> How a page that arrives skewed, sideways or otherwise turned becomes a page
> the OCR can read. The stages, the algorithms, the constants, the measured
> behaviour and the open holes. ADR 218, 221, 242–246 are the decision
> record; this page is the technical reference. Russian mirror:
> `32-orientation-pipeline.ru.md`.

## 1. What the pipeline solves

A page can lie in four ways, and the pipeline treats each differently:

| Way | Example | Answered by | Since |
| --- | --- | --- | --- |
| **Skew** ±15° (not a multiple of 90°) | ADF scan, phone photo | Pixel check → **applied automatically** (ADR 244) | ADR 243/244 |
| **Sideways** (90° or 270°) | A landscape page fed portrait | Pixel check marks it → markup pass turns it **automatically** when its tokens vote, or advises a one-click turn (ADR 245/246) | ADR 246 |
| **Upside down** (180°) | An inverted page | **Not answered** — the open stage (§6) | — |
| Skew + sideways combined | Both of the above | The skew is straightened after the quarter turn | ADR 244 |

The guiding principle is ADR 242: **a wrong turn costs more than no turn**.
Every automatic action below has a measured floor under it, and everything
that cannot meet the floor degrades to a mark or a refusal.

## 2. The pipeline at a glance

```
import ──► [pixel check: axis + skew]  ──► upright: recognition as usual
                    │                        sideways: red mark, skew still applied
                    │                            │
                    │                            ▼
                    │            [markup pass: layout model]
                    │                            │
                    │                 tokens vote a direction
                    │                   │                  │
                    │        setting ON ▼                  ▼ setting OFF
                    │            turnPage(±90/180),    one-click button
                    │            mark cleared           beside the mark
                    │                    │
                    │                    ▼
                    │            [second markup pass]  ──►  recognition
                    └── skew ≥ 0.5°: page straightened before ANY of this runs
```

The pixel check runs before every recognition and every markup pass
(`AppController::runAfterOrientationCheck`); pages the user turned by hand
(`userOriented`) are skipped forever.

## 3. Stage 1 — the pixel check (`app/PageOrientation`)

A pure function: `detectPageOrientation(const QImage &) → OrientationVerdict`
with `{textLike, axisKnown, sideways, deskewDegrees, confidence}`. No model,
no network, tens of milliseconds on the UI thread (the closing is run-based,
the sweep runs on a strided half copy).

### 3.1 Working copy and the global gates

The page is fitted into 1200 px on the long side (KeepAspectRatio). A global
Otsu threshold gives an ink fraction used only as a gate: below 0.004 the
page is blank, above 0.45 it is a photograph or full-bleed ink — in both
cases the verdict says "no text" and nothing downstream asks questions. The
geometry itself is never read from the global split (§3.2).

### 3.2 Local threshold: Sauvola, not Otsu

The ink mask comes from a **Sauvola** threshold over an integral-image
window: radius 15 px, sensitivity k = 0.25, dynamic range R = 128. A local
threshold follows the illumination — a phone photo with a light gradient or
a scanner shadow does not flip half the page from paper to ink the way one
global threshold would.

### 3.3 The margin strip and the ink bounding box

The outer 2 % of the frame are discarded (scanner borders, dark edges, the
shadow of the spine). The mask is then cropped to the bounding box of the
ink: a row or column bin counts as carrying ink when ≥ 1 % of the other side
falls inside it. Everything below is measured on the cropped mask only —
margins carry no lines, and every bin of them a profile keeps is a bin the
periodicity has to see through.

### 3.4 Closing and the line-component refusal

A morphological closing with a 1×25 bar along each direction bridges the
gaps between the letters of a word and the words of a line. It is
implemented over ink **runs**, not pixels (O(runs) instead of O(pixels):
grow each run by the radius, merge what meets, cut back by the radius — the
same mask dilate-then-erode produces, at 6–14 ms instead of ~200).

The closed mask also answers the "is this text at all" question: the number
of line-shaped connected components (long side ≥ 25 px, aspect ≥ 8) must
reach **10** across both directions. A photograph or a drawing closes into
none worth counting; the verdict is a refusal, not a guess. (A full-page
table lands here too — the honest answer for `7_7_eq_table_hard`.)

### 3.5 The skew sweep

The skew is the rotation that makes the lines sharpest. Ink points are
sampled on a halved copy (stride 2) and turned by every angle from −15° to
+15° in 0.5° steps; for each angle the ink is binned into row and column
profiles and scored by the sum of squared adjacent differences of each
profile (a profile with the lines aligned to its bins spikes). The winning
angle is refined by a parabola through it and its neighbours and clamped to
±15°. Above 15° the page is not "a bit skewed" — it is a quadrant case, and
the sweep leaves it alone.

The direction convention: `deskewDegrees` is the **correction** — the
rotation that levels the lines. A page skewed 3° clockwise reports ≈ −3°.

### 3.6 Periodicity and the axis

The single full-size pass turns the two closed masks by the correction
angle and reads a row profile and a column profile. Each profile is read
twice:

* **Periodicity** — the strongest normalized autocorrelation within the
  expected pitch of the lines (lag 12…60 px, deliberately wider than the
  document type sizes). r = 1 — perfect lines, r ≈ 0 — a smooth profile.
* **Variation** — the profile's std/mean: how deeply it alternates.

The decision has three floors:

1. Both profiles qualify only if their periodicity ≥ **0.15** (measured
   noise sits ≤ 0.13).
2. A profile may claim the axis only if it alternates deeply enough:
   variation ≥ **0.65**. The line profile dives from the ink of a line to
   the paper of the gap; the profile across a text block never empties —
   every column carries ink. Measured separation on the corpus: line
   profiles 0.74–1.84, cross profiles 0.15–0.59.
3. When **both** qualify (a contents page repeats along its entries and
   along their indents), the **deeper variation** wins the axis — not the
   higher periodicity. The ToC case that motivated the tie-break: column
   periodicity 0.680 edged out the row 0.655, but the row variation 1.84
   against 1.40 is unambiguous.

The firmness of the answer is the winning profile's periodicity; it rides
out of the check as `confidence`.

### 3.7 Why each floor exists (the failures they closed)

| Floor | The failure it closes |
| --- | --- |
| bbox crop | Scanner frames and spine shadows weighed on every profile as text |
| Sauvola | A global Otsu cut gradient-lit photo pages in half |
| Deskew sweep | A 2–3° skew smears the lines over the profile; periodicity vanished |
| Autocorrelation, absolute floor | The old variance ratio measured the layout, not the lines: margins and two-column layouts both produce a big low-frequency swing |
| Variation qualification | Formula pages: the cross profile repeats at the **letter pitch** (12 px) stronger than the irregular line rhythm — the letter pitch read pages sideways |
| Variation tie-break | Contents pages: the indent rhythm out-repeated the rows |

## 4. Stage 2 — applying the verdict

`OrientationController` runs the check per page and emits, in order:
`skewed(index, degrees)` (skew ≥ **0.5°** — below that a resample is
invisible and not worth it), `sideways(index)`, `unclear(index)`,
`noText(index)`, `finished()`.

### 4.1 The skew is applied, not reported (ADR 244)

`AppController::applyPageDeskew` adds the correction to the page's
`skewDegrees` and the renderer straightens the page **after** the quarter
turn (`PageRotation::deskewImage`: smooth resample, paper-white fill in the
bares corners). The correction composes (the check measures the render as
it stands) and is **self-stabilizing**: the next check of a straightened
page measures below the threshold. A recognition the page carried is
dropped — its blocks were read from the unstraightened pixels. The skew
rides the `.llocr` project next to the turn.

### 4.2 The sideways mark

`DocumentPage::orientationProblem` takes `Sideways`, the thumbnail and the
panel turn red («На боку»), and the status line asks the user to turn the
page. The mark clears on any turn of the page.

## 5. Stage 3 — the markup pass

The layout models carry the direction the pixel check cannot see: MinerU
ends every block line with `<|rotate_up|/left|/right|/down|>`, TeleOCR
carries the same word in the third `<orientation>` tag. The direction names
where the block's text **top** points (0 = up, 90 = right, 180 = down,
270 = left).

### 5.1 Keeping the tokens

`MinerULayoutParser` and `TeleOcrLayoutParser` keep the token on each box
(`BoundingBox::orientation`), zipping the reply's lines onto the parsed
boxes in order and refusing the zip when the lists do not line up. The
page-level vote is independent of that zip — it reads the reply's lines
directly into `OcrPage::orientationVote` and `orientationVoteShare`
(ADR 245's first version lost real replies to a misaligned zip; the vote
must not depend on what the box list does).

### 5.2 The vote and its two bars

`orientationVote` (in `parsers/DetTokenParserBase.h`) picks the winning
non-up direction and requires:

* **consistency** — the winner took ≥ 9/10 of the blocks that voted against
  up (a split vote is no advice at all);
* **backing** — the non-up votes are ≥ 6/10 of **all** lines (stray headers
  and page numbers the model reads upright on a sideways page do not sink
  the advice; a page whose blocks disagree half and half stays silent).

The turn that levels the page is the opposite of the winner (`(360 −
winner) % 360`, right-voting pages read as one left turn). The controller
accepts the vote only for a page the pixel check **already marked
sideways** — the axis is the measured part, the direction is the model's
word.

### 5.3 The automatic turn and the second pass (ADR 246)

With **Settings → Output → «Поворачивать боковую страницу после разметки
автоматически»** on (default), `applyLayout` turns the page itself:
`turnPage` (which clears the mark and drops the just-read sideways boxes)
queues the page for a **second markup pass** that fires when the batch
ends. One auto-turn per page per run (`m_autoTurnedPages`, cleared when the
user starts a markup run): the second pass reads the straightened page, and
its vote has no marked-sideways page to act on — no loops.

With the setting off, the vote degrades to a one-click button beside the
red mark («Повернуть влево / вправо / Развернуть»,
`currentPageSuggestedTurn`); the click is the ordinary `rotatePage`.

### 5.4 What the tokens are worth

Measured on the ten-page corpus, both sideways turns each (MinerU): the
direction is right on **14 of 20** sideways pages — text pages 4/4,
formula pages 6/8, table-dominated pages 4/8 (the model systematically
reads a sideways table the other way). This is why the direction is not an
automatic turn by itself and was a suggestion first (ADR 245); ADR 246
promoted it to an automatic turn with the axis gate plus the vote bars in
front of it, because the user asked for the automation and the failure
mode is visible and hand-recoverable.

## 6. The open stage: 180°

Nothing today answers "upside down":

* the projection profiles are **invariant** under a half-turn — the pixel
  check cannot see it in principle;
* the layout tokens vote `up` on every 180° page (the `<|rotate_down|>`
  token exists in MinerU's vocabulary and was never emitted in 40 replies);
* block recognition on an upside-down crop produces **fluent, confident
  garbage** — nothing fails, nothing warns.

The 180° page therefore sails through unmarked. The planned fix is a tiny
four-class orientation classifier (PaddleOCR's PP-LCNet_x1_0_doc_ori:
0/90/180/270, 99.06 % top-1, ~7 MB ONNX, Apache-2.0) run locally next to
the pixel check; it closes the hole and adds a second, independent
direction voice. Until then the failure is visible to the user and one
manual click away.

## 7. Measurements

Corpus: the project's ten rendered pages (prose, two columns, formulas of
three difficulties, tables of three) × four turns —
`OCRtest/ocr_models/materials/renders`, gated by
`test_page_orientation::theCorpusPagesKeepTheirAxis` (requires ≥ 8/10 pages
read and zero wrong; `LLOCR_CORPUS_RENDERS` points at the renders).

| What | Result |
| --- | --- |
| Axis on the 20 sideways turns | read **18/20, wrong 0** (the 2 refusals are the full-page table → "no text") |
| Axis on the 20 upright turns | read 19/20, `sideways` never claimed |
| 3° skew (synthetic) | axis survives, correction reported within ±1.5° of the truth |
| Blank / photo / null | refused (`textLike = 0`) |
| MinerU direction (sideways turns) | 14/20 right; user's real page: 6 blocks, 6×`left`, unanimous |
| MinerU on 180° turns | `up` on all — no signal |
| ToC page (user report) | upright after the variation tie-break (rows cv 1.84 vs cols 1.40) |

Timing: the pixel check is tens of milliseconds per page (UI thread);
closing 6–14 ms, sweep 40–150 ms depending on ink.

## 8. Constants (`app/PageOrientation` unless noted)

| Constant | Value | Role |
| --- | --- | --- |
| `kWorkingEdge` | 1200 px | working copy long side |
| `kMinInkFraction` / `kMaxInkFraction` | 0.004 / 0.45 | blank / photo gate (global Otsu) |
| `kMarginFraction` | 0.02 | frame strip dropped |
| `kSauvolaRadius` / `kSauvolaK` / `kSauvolaR` | 15 / 0.25 / 128 | local threshold |
| `kCropInkFraction` | 0.01 | bbox bin floor |
| `kCloseLength` | 25 px | closing bar |
| `kMinLineComponentLength` / `kLineAspect` / `kMinLineComponents` | 25 px / 8 / 10 | line-shaped refusal |
| `kSkewSweepDegrees` / `kSkewStepDegrees` | 15° / 0.5° | skew sweep |
| `kMinLinePitch` / `kMaxLinePitch` | 12 / 60 px | autocorrelation lag window |
| `kMinPeriodicity` | 0.15 | firmness floor (noise ≤ 0.13) |
| `kLineProfileVariation` | 0.65 | alternation qualification; also the tie-break |
| `kAutoDeskewDegrees` (`OrientationController`) | 0.5° | apply-straightening floor |
| `kDeskewEpsilon` (`PageRotation`) | 0.05° | render straightening floor |
| vote bars (`DetTokenParserBase::orientationVote`) | 9/10 of non-up, 6/10 of all | consistency + backing |
| `orientationVoteShare` floor (`AppController`) | 50 | controller-side re-check |

## 9. Surfaces

* **Settings → Output**: «Проверять ориентацию страницы»
  (`recognition/orientationCheck`, on) and «Поворачивать боковую страницу
  после разметки автоматически» (`recognition/orientationAutoTurn`, on,
  visible while the check is on).
* **UI**: the red «На боку» / «Ориентация не определяется» status
  (`PageStatus`, thumbnails, panel), the suggestion button beside the mark
  (`WorkPanel`), the manual ±90 buttons with their confirm dialog
  (`PageToolBar`), status lines for straightening and auto-turning in the
  footer.
* **Persistence** (`.llocr`, backward-compatible): `rotation`, `skewDegrees`,
  `suggestedTurn`, `userOriented`, `orientationProblem`. `orientationVote`
  is not persisted — it is consumed when the markup lands.
* **Debug**: set `LLOCR_ORIENTATION_DEBUG=1` to trace the gates, both
  profiles and the decision to stderr.
* **Calibration stand**: `OCRtest/orientation_eval/` — the 40 stored MinerU
  replies over the corpus's four turns, the user-report replies, the
  analysis scripts, and the probe
  (`build/.../orient_probe.exe page.png` prints the verdict for the page
  and its three turns).

## 10. File map and tests

| File | Role |
| --- | --- |
| `src/app/PageOrientation.h/.cpp` | the two-stage detector |
| `src/app/OrientationController.h/.cpp` | the pre-recognition gate, signals |
| `src/app/PageRotation.h/.cpp` | quarter turns, `deskewImage`, sizes |
| `src/app/AppController.cpp` | `applyPageDeskew`, `applyLayout` (vote, auto-turn, second pass), `runAfterOrientationCheck`, `turnPage` |
| `src/app/DocumentModel.h/.cpp` | `rotation`, `skewDegrees`, `suggestedTurn`, `displaySize`, `setSkew`, the render |
| `src/parsers/DetTokenParserBase.h` | `orientationDegrees`, `orientationVote` |
| `src/parsers/MinerULayoutParser.cpp`, `TeleOcrLayoutParser.cpp` | token extraction and the zip |
| `src/core/OcrResult.h` | `BoundingBox::orientation`, `OcrPage::orientationVote(+Share)` |
| `src/app/ProjectStore.*`, `ProjectController.cpp` | persistence |
| `tests/test_page_orientation.cpp` | the detector: 13 slots incl. corpus + skew |
| `tests/test_page_rotation.cpp` | turns, straightening, render composition |
| `tests/test_layout.cpp` | token extraction, the vote, its bars |
| `tests/test_app_import.cpp` | end-to-end: mark, auto-turn + re-markup, suggestion mode |

## 11. Decision history

* **ADR 218** — rotation is a page property applied in the render.
* **ADR 221** — axis by picture, up/down by decision model.
* **ADR 242** — the model check removed: the picture alone marks, nothing
  turns automatically. The principle: a wrong turn costs more than no turn.
* **ADR 243** — the two-stage pixel detector (this §3), the periodicity
  rewrite, the corpus evidence; the 180° hole recorded.
* **ADR 244** — the skew is applied, not reported (this §4.1).
* **ADR 245** — the markup tokens are kept, the vote, the suggestion.
* **ADR 246** — the vote moved off the zipped boxes, recalibrated bars, the
  automatic turn with the second pass, the variation tie-break for
  contents pages.
