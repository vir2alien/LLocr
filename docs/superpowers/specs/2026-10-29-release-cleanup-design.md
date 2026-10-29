# Release cleanup — the design

**Date:** 2026-10-29
**Status:** approved for planning
**Scope:** remove the models and the features that did not earn their place, keep
the work in `archive/`, and leave a release whose every task has a model that can
actually run it.

## 1. The goal in one sentence

The product ships six tasks and seventeen models. After this cleanup it ships
four tasks and eight models, every task has at least one model that can serve
it, and everything that was cut is preserved in `archive/` with the code, the
profiles and the documentation that made it work.

## 2. Why the three removals belong in one change

The `pageOcr` task — the single-pass page recognition — is carried by exactly
four profiles: `lightonocr`, `chandra-ocr-2`, `lfm25-vl-3b`, `unlimited-ocr`.
All four are in the removal set. After the model removal the task would have
**no model at all** and the release would ship a toolbar button that cannot do
anything.

Removing the task itself is therefore not a separate feature cut: it is what
makes the model removal coherent. The two-step protocol (layout → block
recognition) is the pipeline the release keeps, and it is the pipeline every
surviving OCR model can serve.

The same reasoning applies to the decision task. `clef-flash`, `d3-flash` and
`d3-mini` all speak the text request (`systemone_single_question_v7`): a state
object, one `noul` question, a crop. They are replaced by `minicpm-jev`, which
speaks the visual diff (`systemone_visual_diff_v1`): the crop, the candidate
rendered back into an image, and the candidate text. The two protocols do not
share a request builder, so the text request goes entirely, and with it the
whole auxiliary answer set (`has_text`, `readable`, the diagnosis classes) that
only the text protocol produced.

## 3. The confirmed decisions

| # | Decision | Answer |
|---|----------|--------|
| 1 | Where the archive lives | `archive/` in the repository root — under git, outside the CMake build |
| 2 | Where the single-pass code goes | Into the archive as well — the archive is self-contained |
| 3 | The default OCR model | MinerU — the only survivor that covers the whole pipeline (layout + blocks) |
| 4 | Legacy settings | A full settings reset; every piece of migration code is deleted from the source |
| 5 | The reset's boundaries | `runtime/rootDir` and `runtime/modelsDir` are preserved |
| 6 | Legacy `.llocr` projects | Open as they are; the stored mode value stays |

## 4. The task matrix

Before the cleanup — six tasks, seventeen profiles:

| Task | Models |
|------|--------|
| `pageOcr` | lightonocr, chandra-ocr-2, lfm25-vl-3b, unlimited-ocr *(role default)* |
| `rawPageOcr` | glm-ocr *(role default)*, chandra-ocr-2, lightonocr, minicpm-ocr |
| `blockRecognition` | glm-ocr *(role default)*, mineru, paddleocr-vl, lightonocr, lfm25-vl-3b, minicpm-ocr, teleocr |
| `decision` | clef-flash *(role default)*, d3-flash, d3-mini |
| `layout` | teleocr *(role default)*, mineru |
| `fix` | qwen3.5-4b *(role default)*, gemma-4-e4b, gemma-4-12b, qwen3.5-9b |

After the cleanup — four tasks, eight profiles:

| Task | Models | Role default |
|------|--------|--------------|
| `layout` | mineru | **mineru** |
| `blockRecognition` | mineru, glm-ocr, paddleocr-vl | **mineru** |
| `decision` | minicpm-jev | **minicpm-jev** |
| `fix` | qwen3.5-4b, gemma-4-e4b, gemma-4-12b, qwen3.5-9b | qwen3.5-4b |

MinerU takes the `layout` default from `teleocr` and the `blockRecognition`
default from `glm-ocr`: the rationale for the choice (one model that closes the
whole pipeline) only holds if the default is the same model for both steps. A
fresh install then needs one OCR model instead of two.

`minicpm-jev` takes the `decision` default from `clef-flash`, and
`resources/profiles/requests/decision-requests.json` — the shared text-decision
prompt profile that only the three removed models referenced — moves to the
archive.

## 5. The archive

```
archive/
  README.md                  what is here, how to bring a model back
  models/
    profiles/                the 9 removed profile JSONs, verbatim
    docs/                    the manuals of the removed models (md + ru.md)
  parsers/                   the 6 removed parsers
  single-pass/               RecognitionController.{h,cpp}
  decision-text/             decision-requests.json + the decision scripts
```

The archive is inside the repository, so it is versioned, and it is outside
every CMake target, so it is never compiled, never packaged and never seen by
CI. `src/CMakeLists.txt` globs `resources/profiles/models/*.json` — the archive
is not under that path, so it cannot leak into the binary.

**The rule for what goes in.** Whole files that exist only for the removed
features, plus the per-model resources verbatim, plus a manifest.

Code that was interleaved with shared code is **not** duplicated into the
archive. The removal lands as one commit, and the commit it lands on is tagged
`archive/pre-cleanup` — a tag on the **parent**, so the tree before the removal
is reachable by name. `git show archive/pre-cleanup:src/app/RecognitionController.cpp`
returns any removed hunk in full. Copying twenty shared files into the archive
to hold a copy of code that git already holds would rot faster than it helps.

### 5.1 The manifest

`archive/README.md` carries, for every removed item:

- the file it came from and the symbol that was taken out;
- what that symbol did;
- what has to happen to bring it back — which profile, which parser
  registration, which role, which enum value.

Plus one section per removed model: the profile, the parser, the prompt
vocabulary, the memory calibration, the measured numbers, and the reason the
model was cut.

### 5.2 The archive contents in detail

**Profiles (9).** `lightonocr.json`, `chandra-ocr-2.json`, `lfm25-vl-3b.json`,
`unlimited-ocr.json`, `minicpm-ocr.json`, `teleocr.json`, `clef-flash.json`,
`d3-flash.json`, `d3-mini.json`.

**Parsers (6 pairs).** `ChandraParser`, `Lfm25VlParser` + `Lfm25VlDrift`,
`LightonOcrParser`, `TeleOcrLayoutParser`, `UnlimitedOcrParser`.

`DetTokenParserBase` and `DetTokenFormat` **stay in the product**: MinerU's
layout parser derives from `DetTokenParserBase`, and `OtslTable` (the fix
task's table format) uses `DetTokenFormat`. `DetTokenFormat.cpp` and
`OtslTable.cpp` carry LFM-specific regexes behind generic comments; those
branches stay until a check proves no surviving model reaches them.

**Manuals.** `docs/13-lfm25-vl-3b.md`, `docs/15-teleocr.md`,
`docs/18-clef-flash.md`, `docs/21-d3.md`, `docs/22-unlimited-ocr.md`,
`docs/23-lightonocr.md`, `docs/28-chandra-ocr-2.md`, `docs/30-minicpm-ocr.md`
and their `.ru.md` mirrors; `docs/UnlimitedOCR.md`; the study
`docs/17-d1-verification-study.md` (a comparison of two removed models).

**Decision scripts.** `scripts/decision-eval/` (8 modules + 3 configs) and
`scripts/decision-calibrate.py` — the calibration harness built around the
three removed decision models; with the visual-diff protocol it has no target.

`scripts/probe-ocr-reply.py` **stays**: it is a generic probe whose mention of
Chandra is a comment.

**Bench corpus.** `OCRtest/` stays where it is — it is a measurement bench with
its own history, not shipped code. Its reports name the removed models; that is
history, and it is not edited.

## 6. The change set

### 6.1 The role list: six → four

`src/core/ConnectionRole.h` — the enum loses `PageOcr` and `RawPageOcr`, the
`connectionRoles` array becomes a four-element `std::array`, and
`connectionRoleCount` follows. The `static_assert` at line 37 demands that the
enum stay dense, so the remaining values are renumbered and `BlockRecognition`
becomes 0.

`connectionRoleName()` loses two cases; `connectionRoleFromName()` loses the
`"ocr"` alias and its fallback.

> **Trap.** `ModelInstaller::applyTaskChoices` and `queueTaskInstalls` rely on
> that fallback to *filter out* unknown task ids. Re-pointed at a surviving
> role, an unknown id would resolve to a real role and silently enter the
> install queue. The filter must be changed to test membership in
> `connectionRoleIds()` explicitly.

Every dispatch table keyed by the role follows automatically or is edited by
hand:

| File | What |
|------|------|
| `src/config/SettingsStore.cpp:35-85` | `roleSettings()` — the six-row table loses two rows; the `static_assert` on `connectionRoleCount` follows |
| `src/config/RequestProfileStore.cpp:16-46` | `userFileName()` / `legacyFileName()` lose `requestPage.json` / `requestRawPage.json` |
| `src/config/RequestProfileStore.cpp:64-139` | the per-role `activeProfileId()` switch |
| `src/runtime/ModelInstaller.cpp:116-249` | `quantModel()`, `presetsForRole()`, `reloadPresetsInternal()` |
| `src/runtime/ModelInstaller.cpp:345` | `activeTaskCount()` |
| `src/runtime/RuntimeController.cpp:343-385` | `modelBuildError()` loses its two page-specific messages |
| `src/runtime/RuntimeController.cpp:698-708` | `beginRoleSwitch()` — the six-way status ternary becomes four-way |
| `src/runtime/RuntimeController.h:91,149,191,196,210` | the default role arguments (`"pageOcr"`, `PageOcr`) are re-pointed |
| `src/runtime/ModelInstallTransaction.h:114` | `m_pendingRole = "pageOcr"` |
| `src/runtime/ServerLaunchConfig.h:19` | the default role argument |
| `src/runtime/ModelQuantModel.cpp:95-106` | the "all" list resolves the first of `{pageOcr, rawPageOcr, layout}` — with the two gone it falls back to the first alphabetical role |
| `src/runtime/ModelQuantModel.cpp:453-462` | the legacy-entry vision heuristic — the `pageOcr` branch disappears |
| `src/runtime/ModelRegistry.cpp:35-43` | `roleFromIndex()` — the legacy `"ocr"` tag |
| `src/parsers/BlockStyle.cpp:106` | `kStyledRoles[] = {"pageOcr","layout","blockRecognition"}` |
| `src/core/ModelProfiles.cpp:731-755` | `visionTokensFor()` — the "page" set |
| `src/main.cpp:97-119` | six `RequestProfileStore` singletons → four |

### 6.2 The single-pass recognition

**Deleted.**

- `src/app/RecognitionController.{h,cpp}` → archive.
- `src/app/AppController` — `recognizeCurrentPage`, `recognizeAllPages`,
  `recognizeCurrent`, `recognizeAll`, `recognizeScopePages`, `applyRawResult`,
  `canRecognize`, `pageOcrModelAvailable`, `rawPageOcrModelAvailable`, the
  `m_recognition` member and the three connects to it.
- `src/app/AvailabilityPresenter` — `canRecognize`, `pageOcrModel`,
  `rawPageOcrModel` from `Flags`, `Fingerprint` and the getters.
- `ParserOptions::ignoreLayoutMarkup` — the flag exists only for the raw page
  task (`AppController.cpp:913` was its only writer).
- `src/qml/MainWindow/Header.qml:208-250` — the whole
  `ToolGroup { title: qsTr("Recognize") }` with «С разметкой» / «Без разметки»,
  and `Header.qml:51` `pageRecognitionTask`.

**Re-homed — this is the part that must not be missed.**

`AppController::busy()` returns `m_recognition.busy()`. It is read in eleven QML
files (`TaskStatus.qml:38`, `Footer.qml:40,260,292`,
`BlockEditPanel.qml:246,257,286,309`, `ThumbDelegate.qml:150,151,178,195`,
`Header.qml:94,171,190,262,284,329,461,473,498`) and by the verification queue's
"do not start while recognizing" guard (`AppController.cpp:79`).

With the controller gone, `busy()` becomes `m_layout.busy() ||
m_verify.checkBusy()` — the layout pass and the verification queue are the two
jobs that can now be running. `AppController::stop()` loses
`m_recognition.stop()`; its only callers were the two removed buttons.

### 6.3 The settings keys that must move

`modelRecipeId` is the removed page task's key
(`tasks/pageOcr/requestProfileId`), but it is read by **surviving** code:

| Reader | What it needs |
|---------|---------------|
| `AppController::effectiveParserId()` (`AppController.cpp:248-249`) | the OCR model's profile, to resolve the `auto` parser |
| `AppController::parserOptions().modelId` (`AppController.cpp:272`) | the OCR model's id, in the request |
| `SelfTestController::runSelfTestRequest` (`SelfTestController.cpp:50`) | the model to self-test with |
| `LaunchProfileStore::activeProfile()` (`LaunchProfileStore.cpp:214`) | the OCR model's launch parameters |
| `ModelInstaller::syncProfileToActiveModel()` (`ModelInstaller.cpp:278`) | the profile to select when a model is activated |

The same is true of `launchModelPath` / `launchMmprojPath` / `launchDraftPath`
(`tasks/pageOcr/*`): the managed server still loads the OCR model.

**The rule.** No key keeps a `pageOcr` or `rawPageOcr` name. Every one of them
is either re-homed to a surviving namespace or deleted:

| Old key | New home |
|---------|----------|
| `tasks/pageOcr/requestProfileId` | `model/recipeId` — shared, next to `model/name` |
| `tasks/pageOcr/modelPath` / `mmprojPath` / `draftPath` | `launch/modelPath` / `mmprojPath` / `draftPath` — shared, the managed server's model |
| `tasks/pageOcr/baseUrl`, `provider/apiKey/pageOcr` | `tasks/blockRecognition/baseUrl`, `provider/apiKey/blockRecognition` |
| `tasks/rawPageOcr/*` (all) | deleted — no surviving task needs them |

Because the settings are reset (§6.6), the re-homing needs no migration: the
old values are gone before anything reads them.

`model/name` and `parser/id` keep their meaning and their keys — they already
are shared keys.

### 6.4 The decision task

**`src/models/DecisionModel.cpp`** — `buildRequestBody` keeps the visual-diff
branch only: two images, the bare candidate text as `state`, the `match` noul
question. The text branch goes: the `state` object with `block_type`,
`recognized_text` and `notation`, the question with `instructions` and
`criteria`, and the single-image `images` array.

**`src/core/DecisionRequest.h`** — `blockType`, `notation`, `question`,
`criteriaTrue`, `criteriaFalse` go; `image`, `renderImage`, `recognizedText`
and `visualDiff` stay.

**`src/core/DecisionResult.h`** — `hasText`, `readable`, `diagnosis` and
`topDiagnosis()` go. `routedStatus()` reduces to the threshold split:
`probability >= matchThreshold ? Ok : Mismatch`.

> **Behaviour change.** The text protocol could answer "this crop is
> unreadable" (`readable`, `has_text`) and route the block to Review without
> calling the fixer. The visual-diff protocol answers only `match`, so in the
> decision-only «Проверить» flow an unreadable crop now lands on **Mismatch**
> and goes to the fixer, which answers **REVIEW** for a crop it cannot read.
> This is the path `docs/31` already describes for MiniCPM-Jev.

**`src/app/VerificationQueueController`** — `decisionQuestion()`,
`decisionNotation()`, `decisionCriteriaTrue()`, `decisionCriteriaFalse()`,
`withDocumentLanguage()` and the fallback question go; `judgeBlock()` goes;
the visual-diff call becomes the only request.

**`src/core/ModelProfiles`** — `statePromptFor`, `questionFor`,
`criteriaTrueFor`, `criteriaFalseFor`, `decisionBlockFor` and the
`ModelProfiles::DecisionBlock` struct go, together with the role fields
`statePrompt`, `question`, `criteriaTrue`, `criteriaFalse` and
`decisionBlocks`. `blockPromptFor` and `systemPromptFor` **stay** — the fix
and block-recognition roles use them.

### 6.5 The models and the parsers

- Nine profile JSONs deleted; `tests/CMakeLists.txt` loses its nine explicit
  entries.
- Six parser file pairs deleted; `src/CMakeLists.txt` loses twelve source
  entries.
- `src/parsers/ParserFactory.cpp` — five ids and five branches go.
- `src/config/SettingsStore.h:428-429` — `kDefaultModelName` becomes
  `"MinerU2.5-Pro-2605-1.2B"`, `kDefaultModelRecipeId` becomes `"mineru"`.
- `resources/i18n/llocr_ru.ts` — the `ChandraParser` context goes; the
  `RoleProfiles`, `Header`, `PageStatus` and `RuntimeController` strings named
  in §7 go.

**Checks that the removal cannot break a survivor.**

- `ModelProfiles::visionTokensFor()` — the "page" set is
  `{pageOcr, rawPageOcr, layout, all}`. **The layout role must stay in it**,
  or the layout pass gets the crop budget and its memory estimate collapses.
- `BlockStyle::kStyledRoles[]` — with `pageOcr` gone the lookup falls to
  `layout`, then `blockRecognition`. Every surviving model's styled-label
  vocabulary must be declared under one of those two roles, or its labels lose
  their styling.
- `BoxListModel.cpp:185` — the picture labels `image|chart|figure|char` were
  Unlimited-OCR's and TeleOCR's vocabulary. Check that MinerU, GLM-OCR and
  PaddleOCR-VL do not need `figure` or `char`.
- `ModelRegistry` — the `roles` tags are persisted in `index.json`. An
  installed model tagged `pageOcr` or `rawPageOcr` matches no role any more and
  vanishes from every per-task list. The roles must be re-derived from the
  profile at scan time, not read back from the stored tag.

### 6.6 The settings reset

`SettingsStore::applyStartupMigration()` and everything it calls are deleted:
`resolveStoredModelId`, `migrateSecret`, the `kRetired*` constants, the
`taskKeyMoves` table, the `det_tokens` parser resolution and the retired-parser
resolution.

In its place — a one-time reset. A new key `settings/schemaVersion` with the
current value; when the stored value is missing or older, the settings are
cleared and the current version is written. Every key then falls back to the
code's defaults: MinerU, four tasks, the new namespaces.

**What survives the reset.** `runtime/rootDir` and `runtime/modelsDir` are read
before the wipe and written back — they point at the user's disk, not at the
product's schema. Without them the app would forget its installed runtime and
models, the exact symptom `AGENTS.md` records for a settings reset.

**What does not.** The OS secret store is untouched: the API key and the
Hugging Face token live outside `QSettings` (ADR 227) and survive.

> **Accepted consequence.** `migrateSecret` is deleted, so a user still on a
> build from before the secret store loses the plaintext copy of their API key
> instead of having it moved. This follows from "delete everything responsible
> for migration" and is a deliberate trade of one legacy user's key for a
> release with no migration code at all.

### 6.7 The QML surface

| File | Change |
|------|--------|
| `src/qml/MainWindow/Header.qml` | the «Распознать» group (208-250) and `pageRecognitionTask` (51) go |
| `src/qml/Settings/RuntimeTabExternal.qml:23` | `taskIds` → `["blockRecognition","decision","layout","fix"]` |
| `src/qml/RoleProfiles.qml` | the `pageOcr` (18-25) and `rawPageOcr` (26-33) rows go; the `entry()` fallback (71) is re-pointed |
| `src/qml/Settings/RequestTab.qml:14` | the default `role` → `"blockRecognition"` |
| `src/qml/Settings/LaunchTab.qml:13` | the default `role` → `"blockRecognition"` |
| `src/qml/Common/TaskModelsTable.qml:451` | the popup's default `task` → `"blockRecognition"` |
| `src/qml/PageStatus.qml:19-24` | the mode-2 branch stays — it is the legacy state (§6.8) |

Everything that is task-parameterised rather than task-named adapts by itself:
`TaskModelsTable`'s `taskIds` comes from `Settings.taskRoles`, and
`ModelInstaller::defaultTaskChoices()` iterates `connectionRoleIds()`.

### 6.8 The page recognition modes

`PageRecognitionMode` keeps its four values — `None`, `MarkupOnly`, `RawOcr`,
`MarkupOcr` — because the value is part of the `.llocr` file format. What
changes is who produces them:

| Value | After the cleanup |
|--------|-------------------|
| `None` | a fresh page |
| `MarkupOnly` | the layout pass (`AppController::applyLayout`) and a hand-added block |
| `RawOcr` | **nothing** — a legacy value, produced only by `ProjectStore.cpp:382` when a v1 project is opened |
| `MarkupOcr` | the two-step path completes (`recognitionModeAfterBlockText`) |

A legacy `.llocr` project that was saved in the raw mode opens as it is, and
`PageStatus.qml` still shows «Распознано (без разметки)» for it. The i18n
string stays.

### 6.9 The documentation

| File | Change |
|------|--------|
| `docs/07-glossary.md` | new ADRs: the archive, the four-task matrix, the settings reset, the protocol change |
| `docs/20-model-task-rework.md` | the six-task plan becomes a four-task plan; the stage table is re-stated |
| `docs/27-memory-estimate.md` (+ `.ru.md`) | the calibration loses the removed models |
| `docs/03-architecture.md`, `04-components.md`, `01-overview.md`, `06-dev-setup.md`, `02-tech-stack.md` | the removed models are named in the prose |
| `docs/19-glm-ocr.md`, `24-mineru.md`, `25-paddleocr-vl.md`, `29-external-runtime-tasks-and-secrets.md`, `31-minicpm-jev.md`, `32-orientation-pipeline.md` (+ `.ru.md` mirrors) | the removed models are named in the prose |
| `docs/ext-runtime-models.md`, `docs/tofix-findings.md` | the removed models are named |
| `AGENTS.md` | the document map, the per-task model table, the task defaults |

The rule for the shared documents: a mention of a removed model becomes a
mention of the archive, not a deletion. The manuals move; the architecture
documents point at the archive.

## 7. The tests

### 7.1 The tests that break and how

| Test | What it pins | The fix |
|------|--------------|---------|
| `test_app_import.cpp` | ~50 call sites of `recognizeCurrent()` — the page-OCR path is how the suite produces a recognized page with blocks | Switch to the layout + block path. The mock server already speaks all three: `layoutContent` answers the layout prompt (MinerU's `<|box_start|>` markup), the plain `transcription` answers the block transcription, and the `systemone` branch answers the decision. The `RecognitionController` construction sites (540, 641) and the `AppController` ctor arity (495, 3487) change with it |
| `test_model_preset_catalog.cpp` | `shippedProfilesAreTheOnesTheDocsDescribe` — the exact role set of every profile; `roleDefaultsFollowTheShippedProfiles` — one default per task; `lightonOcrShipsOnlyTheTrainedModes`; `verifierSystemPromptFollowsTheModel` | Re-state the eight surviving profiles' role sets and the four defaults; the LightOnOCR case goes to the archive |
| `test_model_installer.cpp` | `quantRowsListTheModelsOfTheRole`, `defaultTaskChoicesFollowRoleDefaults` (count 6 → 4), `defaultTaskChoicesFitTheMemoryBudget`, `allModelsListEveryProfileAndEntry` | Re-state the four task lists |
| `test_settings_store.cpp` | `theTaskListIsTheSingleSource` (the six ids, the `"ocr"` alias, the fallback), `modelKeysMigrateIntoTheTaskNamespace`, `activatingAModelSelectsItsFamilyProfile`, `aStoredRetiredParserIdIsResolved`, `aModelIdFromTheRetiredIdSpaceIsResolved` | The four ids and the new fallback; the migration cases go — the reset replaces them; add a case that the reset clears the settings and keeps the runtime paths |
| `test_ocr_models.cpp` | `theModelSetIsTheCatalog` (the default id and the registry), `everyModelDeclaresARegisteredParser`, `lfm25ModelContract`, `glmOcrModelContract`, `minicpmOcrModelContract`, `chandraModelContract` | The default is MinerU, the registry is the `blockRecognition` list; the removed models' contracts go to the archive |
| `test_det_parser.cpp` | 14 Chandra cases, 4 TeleOCR cases, 2 LightOnOCR cases, 3 LFM cases, `parsersAreNamedAfterTheirModel`, `modelStylesDoNotBleedIntoOtherModels` | The removed cases go; the shared ones stay |
| `test_ensure_connection.cpp` | `everyRoleHasItsOwnConnection`, `externalCheckRoleUsesCheckModelName`, `taskModelAvailabilityFollowsTheTaskSettings`, `managedCheckRoleSwitchesServer`, `switchingTheOcrModelRaisesTheRestartBanner`, `restartBringsBackTheStartedRole` | Re-point the role arguments; the "the other role" in the switch tests becomes a surviving one |
| `test_runtime_tab_external_qml.cpp` | `theSixTasksHaveARow` (six rows), `theTaskEndpointAndAliasAreSaved` (row 0 is `pageOcr`) | Four rows; row 0 is `blockRecognition` |
| `test_task_models_table_qml.cpp` | the six registered singletons, the fake's `rawPageOcrQuantModels`, the fake's `estimateModelMemory` default role, the hardcoded six-task list | Four singletons; the fakes follow the new signatures |
| `test_launch_profile.cpp` | the inline `pageOcr` fixtures, `modelParametersDoNotLeakBetweenModels` | Re-roled fixtures |
| `test_launch_tab_qml.cpp` | the fake's `estimateModelMemory` default role | Signature-coupled; follows the new default |
| `test_request_profile.cpp` | the inline fixtures built on the `pageOcr` role | Re-roled fixtures |
| `test_model_registry.cpp` | `persistsRoles`, `migratesLegacyCheckRole` | The roles are re-derived from the profile; the legacy tag mapping follows |
| `test_decision_model.cpp` | `requestBodyCarriesStateImageAndQuestion`, the v6 answer-set parsing, `routedStatus` | The text request and the v6 answers go; the visual-diff request is pinned |
| `test_model_memory_estimator.cpp` | `aPageFillsTheImageBudgetAndACropDoesNot` — the "page" set of `visionTokensFor` | The layout role stays in the page set; the case is re-stated |
| `test_page_status.cpp` | `recognitionModeAfterBlockText` | Unchanged — the mode enum is unchanged |
| `test_project_store.cpp` | the legacy mode derivation | Unchanged — the legacy value stays |

### 7.2 The tests that must be added

- The settings reset: a stale schema version clears the settings, keeps
  `runtime/rootDir` and `runtime/modelsDir`, leaves the OS secret store
  alone, and does not wipe again on the next start.
- The four-task role list: `connectionRoleIds()` returns exactly the four, the
  round-trip holds, and an unknown id does not resolve to a task that the
  install queue would accept.
- The visual-diff decision request: the wire carries two images, the bare
  state and the `match` noul, and no `instructions` or `criteria` ever.
- Every shipped model declares a registered parser, for the `blockRecognition`
  registry.

### 7.3 The verification commands

```sh
cmake --build build/win-msvc2022 -j 4
ctest --test-dir build/win-msvc2022 -j 4      # the Qt DLLs must be on PATH
scripts/check.sh --format-only                 # clang-format + qmllint
```

`scripts/check.sh` is the full gate (configure → build → ctest → clang-format
→ qmllint); `--format-only` is the tier CI can run without Qt.

## 8. The order of the work

The order matters, because the compiler finds most of the work for us.

1. **`ConnectionRole`** — drop the two enum values. The `static_assert`s in
   `SettingsStore` and the `std::array` sizes fail one by one, and each
   failure names a dispatch table that has to be edited.
2. **The settings keys** — re-home the keys of §6.3, delete the migration
   code, add the schema-version reset.
3. **`RecognitionController` and the AppController surface** — delete the
   class and its call sites; re-home `busy()` and `stop()`.
4. **The QML rows and buttons** — the «Распознать» group, the `taskIds` list,
   the dispatch table, the default roles.
5. **The decision protocol** — `DecisionModel`, `DecisionRequest`,
   `DecisionResult`, `VerificationQueueController`, the dead
   `ModelProfiles` accessors.
6. **The profiles and the parsers** — delete the JSONs, the parser files, the
   factory branches, the CMake entries, the i18n context; repoint the
   defaults.
7. **The archive** — move the files, write the manifest.
8. **The tests** — the fixes of §7.1 and the additions of §7.2.
9. **The documentation** — the ADRs and the document pass.

Steps 1-6 are one commit; the archive and the documentation follow.

## 9. What this does not change

- The verification and fix pipeline stays two-layered; only the decision
  protocol is narrowed.
- The project format (`.llocr`) is unchanged; the mode value stays.
- The external runtime keeps one row per task; there are four rows now.
- The Hugging Face search in Settings → Models is unchanged — a user can
  still install any GGUF, but only the eight shipped profiles carry a role, so
  only they can serve a task.
- The managed runtime, the download flow, the update check and the secret
  store are unchanged.
