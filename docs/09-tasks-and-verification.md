# 09. Tasks and the verification pipeline

> Current state (Oct 2026). The staged rework program that introduced this
> architecture is complete (its decisions are recorded as ADRs in
> `docs/07-glossary.md`); the release cleanup then cut nine models and the
> single-pass page tasks (ADR 251–255). This document describes what ships
> today and why. Russian mirror: `docs/09-tasks-and-verification.ru.md`.

## 1. The four tasks

Every model profile declares which tasks it serves (`roles.<task>`); the same
string ids run through `ConnectionRole`, the settings (`tasks/<task>/…`) and
QML. The list lives once in `src/core/ConnectionRole.h` (ADR 215, 252).

| Task id | UI name | What it does |
|---|---|---|
| `layout` | Разметка | Page markup: the model returns positioned, text-less blocks |
| `blockRecognition` | Распознавание блока | OCR of one block crop (plain transcription) |
| `decision` | Проверка | Binary verdict "the text matches the image" — the visual diff |
| `fix` | Исправление блока | A universal model confirms / corrects / refuses the candidate |

One managed llama-server serves all tasks; the model is (re)loaded on a task
switch (`RuntimeController::serverRunsRole`). Recognition and verification
are mutually exclusive at the `AppController` level, so a switch can never
kill a running job.

The `pageOcr` / `rawPageOcr` single-pass page tasks were cut (ADR 252) — a
page's text is assembled from block recognition, after either manual markup
(`layout`) or a model markup pass.

### The crop the model sees (ADR 287)

The block tasks send a crop of the block's box, cut from the page render
(300 DPI for PDF). Before the image is encoded into the request, the model
role's crop policy may resample it (`src/models/CropScaling.{h,cpp}`):

- the **glyph stage** (fix/read): `cropGlyphHeightByBlock` maps block labels
  to a target letter height — the measured letter height of the crop. A crop
  with smaller letters is stretched up, never down; labels outside the map
  pass untouched. The shipped qwen3.5 fix roles target 32 px for
  `equation`/`formula`: at the 300-DPI crop's ~18 px letters small print
  loses accents (an overline), at 32 px it reads cleanly.
- the **vision-token budget** (blockRecognition): `cropMaxVisionTokens`
  bounds the crop's estimated prompt tokens; a crop over the budget shrinks
  until it fits. mineru ships 3072 — a dense table at the page's resolution
  would overflow the slot the server caps at `n_ctx_train`.

Both stages are profile data: a role without the fields sends the crop
exactly as before, and decision/layout are untouched.

## 2. Model → task matrix

Nine profiles ship in `resources/profiles/models/`; cut profiles live in
`archive/models/profiles/` (ADR 251). Exactly one profile per task carries
`"default": true` — it feeds auto-selection (ADR 169).

| Profile | Tasks | Default for |
|---|---|---|
| `mineru.json` | layout, blockRecognition | layout, blockRecognition |
| `glm-ocr.json` | blockRecognition | — |
| `minicpm-jev.json` | decision | decision |
| `qwen3.5-4b.json` | fix | fix |
| `qwen3.5-9b.json` | fix | — |
| `gemma-4-e4b.json` | fix | — |
| `gemma-4-12b.json` | fix | — |
| `qwen3-vl-4b.json` | fix | — |
| `qwen3-vl-8b.json` | fix | — |

Principle: **OCR models only OCR, decision only verifies, universal models
only fix.** A profile never combines incompatible tasks. Model manuals:
`docs/13-model-mineru.md`, `docs/14-model-glm-ocr.md`,
`docs/17-model-minicpm-jev.md`, `docs/15-model-gemma-4.md`,
`docs/16-model-qwen3.5.md`.

## 3. The two text layers (ADR 164)

Every text block carries two independent layers:

- **text** — `box.text`, the OCR result; any recognition result (page, block,
  re-recognition, read) lands here and **erases** the correction and both
  verdicts;
- **correction** — `box.correctedText`, the fix model's output.

The **effective text** is the correction when present, else the text. Each
layer carries its own verdict (`checkStatus`/`confidence` vs
`correctedCheckStatus`/`correctedConfidence`). There is no automatic
promotion of the correction into the text: export, page-text rebuild and the
preview always use the effective text. The transitions are pure functions in
`src/core/BoxTransitions.*`, covered by `test_box_transitions`.

## 4. The fix protocol (`full_block`, ADR 181/192)

Request to a fix model (assembled in `src/models/FixModel.cpp`):

- `system` = the shared verifier prompt (`resources/profiles/requests/fix-requests.json`);
- `user` = `[block-type prompt, candidate, image]` — the candidate framed
  `OCR candidate:\n<CANDIDATE>\n…\n</CANDIDATE>`; a stored correction is
  never re-sent, the candidate is the block's own effective text;
- every parameter of the task's request profile goes into the body as a
  top-level field (`VisionChatModel::buildRoot`).

The reply is exactly one of:

- `OK` — nothing to fix; the block keeps its bytes;
- `FIX` + the **complete corrected block** — `FixDiffGate` compares it with
  the candidate: equivalent (re-spelled) blocks keep the candidate's own
  bytes; an empty reply, `FIX none` or a reply cut by the token budget
  (`finish_reason: length`) demotes to REVIEW; anything else lands on Review
  as a deviation;
- `REVIEW` — the image is unreadable/damaged; red status, no correction
  stored.

The fix model may think first: the profile declares `reasoning: true`, the
budget is the global setting `check/fixReasoningBudgetTokens` injected per
request (ADR 193/268), and `RequestBudget` clips the thinking before the
answer.

## 5. The read protocol (`read_block`, ADR 259–267)

The same fix models serve the read task («Перераспознать») with another
contract: the request is exactly the image, the system prompt and the block
prompt — **the recognized text never travels**. The reply is `FIX` (full
block) or `REVIEW` (`OK` is outside the schema and lands on Review with the
reason). The gate is the same `FixDiffGate`, so a read that reproduces the
text keeps the block's bytes and drops a stale Mismatch. Outcomes are its own
statuses `Read` / `ReadFixed` / `ReadDamaged`; a run ends with its tally
(«Прочитано блоков: переписано — N, подтверждено — M, не прочитано — K»).
Prompt texts live in `resources/profiles/requests/read-requests.json`; a fix
role may override them (`readSystemPrompt`/`readBlockPrompts`) — the Qwen3-VL
family does, because it reads the shared wording as correction-verification
(ADR 267).

## 6. The decision task (visual diff, ADR 235/254)

The decision model (MiniCPM-OCR Jev) is a binary classifier, not a
generator: the request POSTs to `/v1/systemone` with exactly two images
(the crop, then the candidate rendered back into an image by
`src/app/CandidateRenderer.cpp`) and the candidate text as the `state`
string; the answer is one probability P(consistent). `match ≥ threshold` →
Ok, else Mismatch; a crop the model cannot read goes to Review through the
fix task. Thresholds are per block type (`matchThresholdByBlock` in the
profile → role → setting) and are **measured**, not guessed (ADR 184/258):
text 0.92, equation 0.87, table 0.4, base 0.5 on the OCR_TEST corpus.
A formula reaches the decision in its canonical form (`core/LatexText`,
ADR 194), so both tasks judge the same bytes.

## 7. Formulas (ADR 194–196)

- The canonical LaTeX form is computed at recognition time and carried by the
  block — panel, export, checker and fixer see the same bytes.
- A formula whose LaTeX fails `LatexText::check` is rejected right after
  recognition: status «неверный синтаксис» (red), the panel names what is
  broken, and the block is excluded from verification and fix until it is
  recognized again.
- A markdown code fence inside a formula is carrier markup — unwrapped; a
  leftover backtick is its own defect.
- A formula split into several «$$…$$» fragments renders as several display
  blocks; the invisible `\left.`/`\right.` delimiter pairs with any bracket.

## 8. Block and page statuses

**Block** — the frame carries the state (ADR 238): no frame = markup only,
gray = recognized, red = verified with a problem (Mismatch, Review, invalid
syntax), green = verified clean (Ok, approved, hand-written). The frame
follows the **effective** layer; the confidence number (option
`check/showConfidence`) is printed in the frame's color. The fixer's states
never speak in the frame — it is not a verifier (ADR 240). Reader states do:
green — a read that reproduced the text (the independent re-read is itself a
verification, ADR 261); a read that **replaced** the text is a fresh
re-recognition — the block goes gray until the deciding model judges it
(ADR 289); red — could not be read. «Утвердить блок» stores
`BoxCheckStatus::Approved` and can be revoked (ADR 185/187); a hand-written
text is `BoxCheckStatus::Edited` (ADR 187). Moving or resizing a block erases
its contents — it is markup again (ADR 241).

**Page** — two independent values (ADR 165):

- the persisted recognition mode (`PageRecognitionMode`): none / markup only /
  recognized / markup+recognized — «только разметка» flips to «с разметкой»
  automatically once every non-image block has text;
- the computed verification aggregate over the enabled block types + duplicate
  suspects: «проверено» only when every subject block has a verdict on its
  effective layer; any Mismatch/Review → «есть ошибки».

## 9. Interface

Header (`MainWindow/Header.qml`, ADR 270/171): «Документ» (project menu,
import, export), «Целевые страницы» (page-range field + «Авто-проход»),
«Разметка и OCR» (Разметить / Распознать блоки), «Проверки и AI-коррекция»
(Проверить / Исправить / Перераспознать), «Настройки». Everything acts on
the thumbnail selection (empty selection = whole document).

- «Проверить» runs the decision pass over the scoped blocks
  (`checkScopeEnabledBlocks()`) — verify and fix are separate actions; the
  automatic check/fix switches are gone (ADR 178).
- «Исправить» carries the `scopedPendingFixCount` badge (ADR 188) and runs
  the fix task over the problem blocks of the whole scope.
- «Перераспознать» runs the read task over every text block of the selected
  pages except user-owned ones (ADR 260/266); the count rides the button
  (ADR 264).
- While a run is active, the started action's button becomes the stop
  control; the mapping is by flags/phases (ADR 171).

Page toolbar: page navigation, block-add buttons (T / image / table / Σ /
code + a ▾ menu of every type, ADR 177), rotation. The selected block
carries a floating mini-toolbar (recognize / verify / fix / delete). The
block panel is the version inspector (ADR 185–187): two cards, in-place
diff tinting (`core/TextDiff.h`), approve/revoke, hand edit.

Availability: buttons requiring a task's model go inactive when the model is
not selected/downloaded (managed: file missing; external: alias empty) —
computed by `AvailabilityPresenter` (ADR 213/214).

The auto pipeline chains the five stages (markup → recognition → decision →
fix → decision of the corrected); see `docs/11-auto-pipeline.md`.

## 10. Settings

- Per-task model/quant selection lives in the one «Модели» window (ADR 168):
  rows `task | model ▾ | quant ▾ | ⚙`, Launch/Request tabs behind ⚙, a
  files-on-disk block with guarded delete.
- Settings keys: `tasks/<task>/{modelPath, mmprojPath, draftPath,
  requestProfileId, modelName}` — blockRecognition's launch files keep the
  legacy `check/` prefix (ADR 146). There is no settings migration: a schema
  mismatch resets the settings once (ADR 253).
- `check/` keeps the behavior keys: `showConfidence`, and the reasoning
  budgets `check/fixReasoningBudgetTokens` / `check/readReasoningBudgetTokens`
  (ADR 268, edited in Настройки → Проверка together with the block-type
  filter).
- `decision/matchThreshold` remains the base threshold; per-type thresholds
  come from the model profile.
- Request texts are shared per task (`fix-requests.json`, `read-requests.json`,
  ADR 182); a role may override the wording (ADR 267).

## 11. Where things live in the code

| What | Where |
|---|---|
| Task list, enum, name mapping | `src/core/ConnectionRole.h` |
| Model profiles, `roles` schema | `src/core/ModelProfiles.{h,cpp}`, `resources/profiles/models/*.json` |
| Shared request texts | `resources/profiles/requests/fix-requests.json`, `read-requests.json` |
| Per-task settings dispatch | `src/config/SettingsStore.{h,cpp}` |
| Request/launch profile stores | `src/config/RequestProfileStore.{h,cpp}`, `src/runtime/LaunchProfileStore.{h,cpp}` |
| Server start, model reload per task | `src/runtime/RuntimeController.cpp`, `src/runtime/ServerLaunchConfig.cpp` |
| Models window, install queue | `src/runtime/ModelInstaller.{h,cpp}`, `src/runtime/ModelQuantModel.cpp`, `src/runtime/ModelInstallTransaction.cpp` |
| Block state, transitions | `src/core/OcrResult.h`, `src/core/BoxTransitions.{h,cpp}` |
| Formula canonical form / syntax | `src/core/LatexText.{h,cpp}` |
| Crop preprocessing (glyph, token budget) | `src/models/CropScaling.{h,cpp}` |
| Fix + read clients | `src/app/FixController.{h,cpp}`, `src/models/FixModel.cpp` |
| Decision client | `src/app/DecisionController.{h,cpp}`, `src/models/DecisionModel.cpp` |
| Candidate render for visual diff | `src/app/CandidateRenderer.cpp` |
| Role queues (decision / recheck / remedy) | `src/app/BlockQueue.{h,cpp}`, `src/app/BlockRunGate.{h,cpp}`, `src/app/DecisionQueue.{h,cpp}`, `src/app/RecheckQueue.{h,cpp}`, `src/app/RemedyQueue.{h,cpp}` (ADR 286) |
| Block collection | `src/app/BlockWorkList.{h,cpp}` |
| Block OCR / layout clients | `src/app/CheckController.{h,cpp}`, `src/models/BlockRecognitionModel.cpp`, `src/app/LayoutController.{h,cpp}` |
| Availability, facade | `src/app/AvailabilityPresenter.{h,cpp}`, `src/app/AppController.{h,cpp}` |
| Project `.llocr` | `src/app/ProjectStore.{h,cpp}` |
| QML | `src/qml/MainWindow/…`, `src/qml/Settings/…`, `src/qml/RoleProfiles.qml` |
| Verification settings (filter, budgets) | `src/app/VerificationPromptStore.{h,cpp}` |

## 12. Related documents

- `docs/07-glossary.md` — the full ADR log (the decisions above carry their numbers).
- `docs/13-model-mineru.md`, `docs/14-model-glm-ocr.md` — OCR models.
- `docs/17-model-minicpm-jev.md` — the decision model and its thresholds.
- `docs/15-model-gemma-4.md`, `docs/16-model-qwen3.5.md` — the fix models.
- `docs/11-auto-pipeline.md` — the automatic five-stage pipeline.
- `docs/19-testing.md` — the test suite and the evaluation stands behind the numbers above.
