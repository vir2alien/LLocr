# Release Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a release with four tasks and eight models instead of six tasks and seventeen, with every removed model, its parser, its profile and its documentation preserved in `archive/`.

**Architecture:** The change is one atomic refactor — the `pageOcr` task is carried by exactly four profiles and all four are removed, so the task cannot outlive them. The work is therefore cut along the seams the compiler enforces: the role list, the settings keys, the feature surface, the decision protocol. Each task below ends with a green build and a green `ctest`; the tasks are ordered so that no task leaves the tree broken.

**Tech Stack:** Qt 6.10.3 (MSVC 2022 64-bit, `C:/Qt/6.10.3/msvc2022_64`), C++17, QML, CMake, ctest.

**Design:** `docs/superpowers/specs/2026-10-29-release-cleanup-design.md` — the approved design. This plan implements it; where the two differ, the design wins and the plan is wrong.

---

## Global Constraints

- **Build tree:** reuse `build/win-msvc2022` (the `dev-win` preset: Ninja, Debug). Never re-configure from scratch. `VCPKG_ROOT` is not a shell variable and vcpkg does not participate on Windows only for zlib — `C:/vcpkg` supplies it.
- **The Qt DLLs must be on `PATH` to run any built exe** — otherwise ctest fails with exit code `0xc0000135`.
- **The archive is outside every CMake target.** `archive/` is a sibling of `src/`, `tests/` and `resources/`; no target globs it, so it is never compiled, never packaged and never seen by CI.
- **No migration code.** The settings are reset once and every piece of migration machinery is deleted from the source (design §6.6).
- **`PageRecognitionMode` keeps its four values.** The enum is part of the `.llocr` file format; `RawOcr` becomes legacy-only (design §6.8).
- **Comments only where the code cannot be understood from its surroundings** — never narrate what the code says, never leave a TODO.
- **UI text sizes come from `Theme.*`** — never hardcode `font.pointSize` in QML.
- **No secrets in `QSettings`** — the API key and the Hugging Face token live in the OS secret store (ADR 227). The reset does not touch the secret store.
- **Windows specifics:** octal literals are `0755u`; Windows API constants need an explicit `#include <windows.h>`; path comparisons normalize `/` vs `\` with `QDir::fromNativeSeparators` + `QDir::cleanPath`.
- **Every step that touches a test class that constructs `SettingsStore` keeps `TestSettingsIsolation m_settingsIsolation;` as the first member** (`tests/testsettings.h`) — without it the test overwrites the user's real profile.

### The verification cycle (Windows)

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

The format gate (the `--format-only` tier of `scripts/check.sh`):

```powershell
Get-ChildItem src,tests -Recurse -Include *.cpp,*.h | ForEach-Object { clang-format --dry-run --Werror $_.FullName }
```

---

## File Structure

**Created**

| Path | Responsibility |
|------|----------------|
| `archive/README.md` | the manifest: what was cut, why, and how to bring it back |
| `archive/models/profiles/*.json` | the nine removed model profiles, verbatim |
| `archive/models/docs/*.md` | the removed manuals and the removed study |
| `archive/parsers/*.{h,cpp}` | the six removed parsers |
| `archive/single-pass/RecognitionController.{h,cpp}` | the single-pass controller |
| `archive/decision-text/decision-requests.json` | the text-decision prompt profile |
| `archive/decision-text/scripts/` | the decision calibration harness |

**Deleted** — nine profiles, six parser pairs, `src/app/RecognitionController.{h,cpp}`, `resources/profiles/requests/decision-requests.json`, `scripts/decision-eval/`, `scripts/decision-calibrate.py`.

**Modified** — the role list, the settings store, the AppController surface, the availability presenter, the decision model, the model profiles, the QML task tables, the i18n file, the CMake files, the affected tests, the documentation.

**The layer rule** (AGENTS.md): `llocr_core` → `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` → `llocr_app` → `llocr`. A layer may only use what it links. `LaunchProfileStore` lives in `llocr_runtime`; `RequestProfileStore` in `llocr_config`.

---

## Task 1: Tag the pre-cleanup tree and open the archive

**Files:**
- Create: `archive/README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: the git tag `archive/pre-cleanup` and the archive's manifest skeleton. Every later task appends its section to `archive/README.md` in the same commit that moves the files, so the manifest is never a placeholder.

- [ ] **Step 1: Tag the tree before the removal**

The tag must point at the commit **before** the first removal commit, so the whole pre-cleanup source is reachable by name.

```powershell
git tag archive/pre-cleanup
git rev-parse archive/pre-cleanup
```

Expected: a sha, and `git log -1 archive/pre-cleanup --format=%s` prints the design commit's subject.

- [ ] **Step 2: Write the manifest's preamble**

Create `archive/README.md`:

```markdown
# The archive

Everything that was cut from LLocr before the release, kept whole.

## Why

The release ships four tasks and eight models. Nine models and two of the six
tasks did not earn their place: the decision models that worked markedly worse
and slower than MiniCPM-OCR Jev, and the OCR models whose quality, speed,
licence or language support did not hold up.

The material is kept here, not deleted, because the work is worth reusing: a
model that was wrong for this release is not wrong forever.

## What is here

- `models/profiles/` — the model profiles, verbatim
- `models/docs/` — the manuals of the removed models
- `parsers/` — the parsers that existed only for the removed models
- `single-pass/` — the single-pass page recognition controller
- `decision-text/` — the text-decision prompt profile and the calibration harness

## How to bring a model back

1. Copy its profile from `models/profiles/` back to `resources/profiles/models/`.
   The loader globs that directory: a new model is a new file, no list to maintain.
2. Copy its parser from `parsers/` back to `src/parsers/` and add the two lines
   to `src/CMakeLists.txt`.
3. Register the parser id in `src/parsers/ParserFactory.cpp`.
4. Add the profile's file to `LLOCR_MODEL_PROFILE_FILES` in `tests/CMakeLists.txt`.
5. Give the profile a role. The roles are `layout`, `blockRecognition`,
   `decision` and `fix` — see `src/core/ConnectionRole.h`.
6. Restore the task's settings row in `roleSettings()` in
   `src/config/SettingsStore.cpp` and its case in
   `connectionRoleName()`.

## The code that was interleaved with the shared code

The removal was one commit, and the tree before it is the tag
`archive/pre-cleanup`:

    git show archive/pre-cleanup:src/app/RecognitionController.cpp

Every file that the removal touched is reachable there in full — the removed
hunks included.
```

- [ ] **Step 3: Commit**

```powershell
git add archive/README.md
git commit -m "chore: open the archive for the cut models"
```

---

## Task 2: Cut the nine models out of the product

**Files:**
- Delete: `resources/profiles/models/{lightonocr,chandra-ocr-2,lfm25-vl-3b,unlimited-ocr,minicpm-ocr,teleocr,clef-flash,d3-flash,d3-mini}.json` → move to `archive/models/profiles/`
- Delete: `src/parsers/{ChandraParser,LightonOcrParser,Lfm25VlParser,Lfm25VlDrift,TeleOcrLayoutParser,UnlimitedOcrParser}.{h,cpp}` → move to `archive/parsers/`
- Modify: `src/parsers/ParserFactory.cpp`
- Modify: `src/CMakeLists.txt:144-163`
- Modify: `tests/CMakeLists.txt:20-38`
- Modify: `resources/profiles/models/mineru.json`
- Modify: `resources/profiles/models/minicpm-jev.json`
- Modify: `src/config/SettingsStore.h:428-429`
- Modify: `src/models/OcrModel.cpp`
- Modify: `resources/i18n/llocr_ru.ts`
- Modify: `tests/test_model_preset_catalog.cpp`, `tests/test_ocr_models.cpp`, `tests/test_det_parser.cpp`, `tests/test_model_installer.cpp`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: the tag from Task 1.
- Produces: eight profiles — `glm-ocr`, `mineru`, `paddleocr-vl`, `minicpm-jev`, `qwen3.5-4b`, `qwen3.5-9b`, `gemma-4-e4b`, `gemma-4-12b`. MinerU is the default for `layout` and `blockRecognition`; MiniCPM-Jev is the default for `decision`; Qwen3.5-4B stays the default for `fix`.

- [ ] **Step 1: Move the nine profiles into the archive**

```powershell
New-Item -ItemType Directory -Force -Path archive/models/profiles | Out-Null
git mv resources/profiles/models/lightonocr.json       archive/models/profiles/
git mv resources/profiles/models/chandra-ocr-2.json    archive/models/profiles/
git mv resources/profiles/models/lfm25-vl-3b.json       archive/models/profiles/
git mv resources/profiles/models/unlimited-ocr.json     archive/models/profiles/
git mv resources/profiles/models/minicpm-ocr.json      archive/models/profiles/
git mv resources/profiles/models/teleocr.json           archive/models/profiles/
git mv resources/profiles/models/clef-flash.json        archive/models/profiles/
git mv resources/profiles/models/d3-flash.json           archive/models/profiles/
git mv resources/profiles/models/d3-mini.json           archive/models/profiles/
```

`src/CMakeLists.txt:442` globs `resources/profiles/models/*.json`, so the profiles leave the binary with the move. `tests/CMakeLists.txt` names them explicitly — Step 5 fixes that.

- [ ] **Step 2: Move the six parsers into the archive and drop them from the build**

```powershell
New-Item -ItemType Directory -Force -Path archive/parsers | Out-Null
foreach ($n in @('ChandraParser','LightonOcrParser','Lfm25VlParser','Lfm25VlDrift','TeleOcrLayoutParser','UnlimitedOcrParser')) {
  git mv "src/parsers/$n.h"   archive/parsers/
  git mv "src/parsers/$n.cpp" archive/parsers/
}
```

Delete the twelve lines from `src/CMakeLists.txt` (144-145, 148-153, 156-157, 162-163):

```cmake
    parsers/ChandraParser.h
    parsers/ChandraParser.cpp
    parsers/UnlimitedOcrParser.h
    parsers/UnlimitedOcrParser.cpp
    parsers/Lfm25VlParser.h
    parsers/Lfm25VlParser.cpp
    parsers/TeleOcrLayoutParser.h
    parsers/TeleOcrLayoutParser.cpp
    parsers/LightonOcrParser.h
    parsers/LightonOcrParser.cpp
    parsers/Lfm25VlDrift.h
    parsers/Lfm25VlDrift.cpp
```

`DetTokenParserBase`, `DetTokenFormat` and `OtslTable` **stay**: MinerU's layout parser derives from `DetTokenParserBase`, and `OtslTable` (the fix task's tables) uses `DetTokenFormat`.

- [ ] **Step 3: Unregister the five parsers**

In `src/parsers/ParserFactory.cpp`, `registeredIds()` returns seven ids today — `raw`, `unlimited-ocr`, `lfm2.5-vl`, `teleocr-layout`, `mineru-layout`, `lightonocr`, `chandra-ocr`. Five of them go:

```cpp
QStringList ParserFactory::registeredIds()
{
    return {QStringLiteral("raw"),
            QStringLiteral("mineru-layout")};
}
```

`create()` loses the five matching branches — the `unlimited-ocr`, `lfm2.5-vl`, `teleocr-layout`, `lightonocr` and `chandra-ocr` arms — and keeps `mineru-layout` and `raw`:

```cpp
std::unique_ptr<IOutputParser> ParserFactory::create(const QString &parserId, const ParserOptions &options)
{
    if (parserId == QLatin1String("mineru-layout"))
        return std::make_unique<MinerULayoutParser>(options);
    if (parserId == QLatin1String("raw"))
        return std::make_unique<RawParser>(options);
    ...
```

The `auto` id, the unknown-id fallback to `raw` and `selectableDisplayNames()` stay as they are.

- [ ] **Step 4: Make MinerU and MiniCPM-Jev the defaults**

In `resources/profiles/models/mineru.json`, give both surviving roles the default flag:

```json
  "roles": {
    "layout": {
      "parser": "mineru-layout",
      "default": true
    },
    "blockRecognition": {
      "parser": "raw",
      "default": true
    }
  },
```

In `resources/profiles/models/minicpm-jev.json`, the decision role becomes the default:

```json
  "roles": {
    "decision": {
      "alias": "llocr-decision",
      "protocol": "systemone_visual_diff_v1",
      "default": true,
      "matchThreshold": 0.5,
      ...
    }
  },
```

Keep every other key of both roles exactly as it is — `matchThreshold`, `matchThresholdByBlock`, `blockAliases`, `launch`, `request` and `_note` stay.

In `resources/profiles/models/glm-ocr.json`, the `blockRecognition` role loses its default flag (MinerU took it). The `rawPageOcr` role stays in this task; Task 5 removes it with the role itself.

- [ ] **Step 5: Fix the test resources list**

In `tests/CMakeLists.txt`, the `LLOCR_MODEL_PROFILE_FILES` list becomes the eight survivors:

```cmake
set(LLOCR_MODEL_PROFILE_FILES
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/qwen3.5-4b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/qwen3.5-9b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/gemma-4-e4b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/gemma-4-12b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/glm-ocr.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/mineru.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/paddleocr-vl.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/minicpm-jev.json"
)
```

- [ ] **Step 6: Re-point the app's default model**

In `src/config/SettingsStore.h:428-429`:

```cpp
    static constexpr const char *kDefaultModelName = "MinerU2.5-Pro-2605-1.2B";
    static constexpr const char *kDefaultModelRecipeId = "mineru";
```

In `src/models/OcrModel.cpp`, the app's OCR model is no longer the page task's model but the block recognition model. `defaultId()` and `registeredIds()` move from the `pageOcr` role to `blockRecognition`:

```cpp
QString OcrModel::defaultId()
{
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
}

QStringList OcrModel::registeredIds()
{
    return ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("blockRecognition"));
}
```

`AppController::effectiveParserId()` resolves the parser for the OCR model with the `pageOcr` role — that role name becomes `blockRecognition` (Task 5 removes the role; until then the string is simply re-pointed):

```cpp
QString AppController::effectiveParserId() const
{
    return effectiveParserIdFor(m_settings.modelRecipeId(), QStringLiteral("blockRecognition"));
}
```

- [ ] **Step 7: Drop the ChandraParser translation context**

In `resources/i18n/llocr_ru.ts`, delete the whole `<context>` element whose `<name>` is `ChandraParser`. Leave the other contexts untouched.

- [ ] **Step 8: Re-state the tests that pin the model set**

`tests/test_model_preset_catalog.cpp`:

- `shippedProfilesAreTheOnesTheDocsDescribe` — the expected id list becomes the eight survivors.
- `roleDefaultsFollowTheShippedProfiles` — one default per role: `layout` → `mineru`, `blockRecognition` → `mineru`, `decision` → `minicpm-jev`, `fix` → `qwen3.5-4b`.
- `lightonOcrShipsOnlyTheTrainedModes` — delete the case.
- `verifierSystemPromptFollowsTheModel` — keep, re-pointed at a surviving model if it names a removed one.

`tests/test_ocr_models.cpp`:

- `theModelSetIsTheCatalog` — the default id is `mineru`, the registry is the `blockRecognition` list.
- `everyModelDeclaresARegisteredParser` — unchanged; it is the guard that catches a profile whose parser was unregistered.
- `lfm25ModelContract`, `glmOcrModelContract`, `minicpmOcrModelContract`, `chandraModelContract` — delete the three that name removed models, keep `glmOcrModelContract`.

`tests/test_det_parser.cpp` — delete the fourteen Chandra cases, the four TeleOCR cases, the two LightOnOCR cases and the three LFM cases. Keep every shared case. `parsersAreNamedAfterTheirModel` and `modelStylesDoNotBleedIntoOtherModels` stay.

`tests/test_model_installer.cpp` — `allModelsListEveryProfileAndEntry` is re-stated against the eight profiles; `defaultTaskChoicesFollowRoleDefaults` still counts six tasks in this task (the roles die in Task 5).

- [ ] **Step 9: Build and run the tests**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

Expected: the build succeeds and `ctest` is green. If `test_ocr_models::everyModelDeclaresARegisteredParser` fails, a surviving profile still names a removed parser id — fix the profile.

- [ ] **Step 10: Append the manifest section and commit**

Append to `archive/README.md`:

```markdown
## The removed models

| Model | Roles it served | Why it was cut |
|-------|-----------------|----------------|
| LightOnOCR-3-4B | pageOcr, blockRecognition, rawPageOcr | low quality, low speed, loops on complex documents |
| Chandra-OCR-2 | pageOcr, rawPageOcr | licence, low speed |
| LFM2.5-VL-3B | pageOcr, blockRecognition | low quality |
| Unlimited-OCR | pageOcr | low quality, loops on complex documents |
| MiniCPM-OCR 1.0 | blockRecognition, rawPageOcr | no Russian support |
| TeleOCR (NaviDC-OCR) | blockRecognition, layout | no Russian support, markup weaker than MinerU's |
| Cloudflare clef-flash | decision | markedly worse and slower than MiniCPM-OCR Jev |
| d3-flash | decision | markedly worse and slower than MiniCPM-OCR Jev |
| d3-mini | decision | markedly worse and slower than MiniCPM-OCR Jev |
```

```powershell
git add -A archive resources src tests
git commit -m "feat: cut the nine models that did not earn their place"
```

---

## Task 3: Retire the single-pass page recognition

**Files:**
- Delete: `src/app/RecognitionController.{h,cpp}` → move to `archive/single-pass/`
- Modify: `src/app/AppController.{h,cpp}`
- Modify: `src/app/AvailabilityPresenter.{h,cpp}`
- Modify: `src/parsers/ParserOptions.h`
- Modify: `src/qml/MainWindow/Header.qml`
- Modify: `tests/test_app_import.cpp`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: the eight-profile catalog from Task 2.
- Produces: `AppController::busy()` keeps its signature and now reports the layout pass and the verification queue. The «Распознать» tool group is gone; the «Разметка» and «Распознавание блоков» groups stay.

- [ ] **Step 1: Move the controller into the archive**

```powershell
New-Item -ItemType Directory -Force -Path archive/single-pass | Out-Null
git mv src/app/RecognitionController.h   archive/single-pass/
git mv src/app/RecognitionController.cpp archive/single-pass/
```

Delete the two lines from `src/CMakeLists.txt` (325-326).

- [ ] **Step 2: Delete the AppController's single-pass surface**

In `src/app/AppController.h` and `src/app/AppController.cpp`, remove:

- the `m_recognition` member and the `#include "RecognitionController.h"`;
- `recognizeCurrentPage()`, `recognizeAllPages()`, `recognizeCurrent()`, `recognizeAll()`, `recognizeScopePages()`;
- `applyRawResult()`;
- `canRecognize()`, `pageOcrModelAvailable()`, `rawPageOcrModelAvailable()` and their `Q_PROPERTY` declarations;
- the `recognitionFinished` / `recognitionFailed` / `recognitionProgress` signals that only the controller raised, if no other emitter uses them — check every connection before removing a signal.

- [ ] **Step 3: Re-home `busy()` and `stop()`**

`AppController::busy()` returned `m_recognition.busy()`. It is read in eleven QML files and by the verification queue's "do not start while recognizing" guard. The two jobs that can now be running are the layout pass and the verification queue:

```cpp
bool AppController::busy() const
{
    return m_layout.busy() || m_verify.checkBusy();
}
```

`AppController::stop()` loses the `m_recognition.stop()` call — the controller is gone and nothing else used it.

- [ ] **Step 4: Trim the availability presenter**

In `src/app/AvailabilityPresenter.h` and `.cpp`, remove `canRecognize`, `pageOcrModel` and `rawPageOcrModel` from `Flags`, from `Fingerprint` and from the getters. Whatever consumes them in the QML goes with them — grep for `canRecognize` in `src/qml` and remove the bindings.

- [ ] **Step 5: Drop the raw-page parser flag**

`ParserOptions::ignoreLayoutMarkup` existed only for the raw page task. Delete the field from `src/parsers/ParserOptions.h` and its only writer in `AppController.cpp` (the `applyRawResult` path).

- [ ] **Step 6: Delete the «Распознать» tool group**

In `src/qml/MainWindow/Header.qml`:

- delete the whole `ToolGroup` whose `title` is `qsTr("Recognize")` — the one with the «С разметкой» and «Без разметки» actions (lines 208-250);
- delete the `pageRecognitionTask` property (line 51);
- delete every binding that reads `pageRecognitionTask`.

The «Разметка» group (the layout task) and the «Распознавание блоков» group (the block recognition task) stay.

- [ ] **Step 7: Re-point the test suite to the two-step path**

`tests/test_app_import.cpp` calls `recognizeCurrent()` in about fifty places — that was how the suite produced a recognized page with blocks. The mock server already speaks the surviving protocol: `layoutContent` answers the layout prompt with MinerU's `<|box_start|>` markup, and the plain `transcription` answers the block transcription.

Add a helper next to the mock server and use it in place of `recognizeCurrent()`:

```cpp
// The single-pass page OCR is gone: a page becomes recognized through the
// layout pass and then the block transcription.
void recognizePage(AppController &controller)
{
    controller.recognizeScopeBlocks();
}
```

`AppController::recognizeScopeBlocks()` (`src/app/AppController.h:377`) takes no argument and runs the block recognition over the whole document; it runs the layout pass first when the page has no markup.

The `RecognitionController` construction sites (around lines 540 and 641) and the `AppController` constructor arity (around 495 and 3487) change with the class's removal. Every test that constructed it must drop it from the argument list.

- [ ] **Step 8: Build and run the tests**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

Expected: green. The failures that show up here are the honest ones: a test that asserted a page's blocks came from a single request now has to assert they came from the two-step path.

- [ ] **Step 9: Append the manifest section and commit**

Append to `archive/README.md`:

```markdown
## The single-pass page recognition

The app used to recognize a whole page in one request — with the layout markup
(`pageOcr`) or without it (`rawPageOcr`). Every model that served `pageOcr` is
cut above, so the task had no model left and the button could not do anything.

The controller is in `single-pass/`. The task names live in the enum
`ConnectionRole` and in the settings keys `tasks/pageOcr/*` and
`tasks/rawPageOcr/*`; the toolbar group was «Распознать».
```

```powershell
git add -A archive src tests
git commit -m "feat: retire the single-pass page recognition"
```

---

## Task 4: Re-home the settings keys and replace the migration with a reset

**Files:**
- Modify: `src/config/SettingsStore.{h,cpp}`
- Modify: `src/app/AppController.cpp`
- Modify: `src/runtime/ModelInstaller.cpp`
- Modify: `src/runtime/LaunchProfileStore.cpp`
- Modify: `src/app/SelfTestController.cpp`
- Modify: `tests/test_settings_store.cpp`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: the eight-profile catalog from Task 2.
- Produces: `SettingsStore::modelRecipeId()` reads the key `model/recipeId`; `SettingsStore::resetToDefaults()` — the new public method — clears the settings and restores the code's defaults, keeping `runtime/rootDir` and `runtime/modelsDir`.

**The key map.** The `pageOcr` role's settings row is deleted; the `blockRecognition` role absorbs what it owned.

| Old key | New key | What it holds |
|---------|---------|---------------|
| `tasks/pageOcr/requestProfileId` | `model/recipeId` | the OCR model's profile id |
| `tasks/pageOcr/modelPath` | `tasks/check/modelPath` | the OCR model's launch path |
| `tasks/pageOcr/mmprojPath` | `tasks/check/mmprojPath` | the OCR model's mmproj |
| `tasks/pageOcr/draftPath` | `tasks/check/draftPath` | the OCR model's draft model |
| `tasks/pageOcr/baseUrl` | `tasks/check/baseUrl` | the OCR task's external endpoint |
| `provider/apiKey/pageOcr` | `provider/apiKey/check` | the OCR task's key |
| `tasks/pageOcr/modelName` | `model/name` | the managed server's alias (already shared) |
| `tasks/rawPageOcr/*` | — | deleted |
| `tasks/check/requestProfileId` | unchanged | the block request's request profile |

`model/recipeId` and `tasks/check/requestProfileId` are different things — a model profile and a request profile — and they never share a key.

- [ ] **Step 1: Move the OCR model's identity to the shared namespace**

In `src/config/SettingsStore.h`, change the key constant:

```cpp
    static constexpr const char *kModelRecipeId = "model/recipeId";
```

In `src/config/SettingsStore.cpp`, the `modelRecipeId()` accessor reads that key instead of `tasks/pageOcr/requestProfileId`.

- [ ] **Step 2: Delete the migration machinery**

In `src/config/SettingsStore.cpp`, remove:

- `applyStartupMigration()` and its call from the constructor;
- `resolveStoredModelId()`;
- `migrateSecret()`;
- every `kRetired*` key constant and the loop that removes them;
- the task-key move table and the code that applies it;
- the `det_tokens` parser resolution;
- the retired-parser resolution.

The `clearSecrets()` and `clearTaskConnections()` methods stay — they are user-facing reset actions, not migration.

- [ ] **Step 3: Add the one-time reset**

In `src/config/SettingsStore.h`, add the key and the version:

```cpp
    static constexpr const char *kSettingsSchemaVersion = "settings/schemaVersion";
    static constexpr int kCurrentSettingsSchemaVersion = 1;
```

In `src/config/SettingsStore.cpp`, add:

```cpp
void SettingsStore::resetToDefaults()
{
    const QString rootDir = m_settings.value(kRuntimeRootDir).toString();
    const QString modelsDir = m_settings.value(kRuntimeModelsDir).toString();

    m_settings.clear();
    m_settings.setValue(kRuntimeRootDir, rootDir);
    m_settings.setValue(kRuntimeModelsDir, modelsDir);
    m_settings.setValue(kSettingsSchemaVersion, kCurrentSettingsSchemaVersion);
    m_settings.sync();
}
```

`runtime/rootDir` and `runtime/modelsDir` point at the user's disk, not at the product's schema. Wiping them would make the app forget its installed runtime and models — the exact symptom AGENTS.md records for a settings reset.

The OS secret store is **not** touched: the API key and the Hugging Face token live outside `QSettings` (ADR 227) and survive the reset.

- [ ] **Step 4: Trigger the reset once**

In the `SettingsStore` constructor, before anything reads a value:

```cpp
    if (m_settings.value(kSettingsSchemaVersion).toInt() != kCurrentSettingsSchemaVersion)
        resetToDefaults();
```

The reset runs once: the version it writes makes the next start skip it.

- [ ] **Step 5: Re-point the five readers of the OCR model's identity**

| File | Line | Change |
|------|------|--------|
| `src/app/AppController.cpp` | 248-249 | `effectiveParserIdFor(m_settings.modelRecipeId(), QStringLiteral("blockRecognition"))` |
| `src/app/AppController.cpp` | 272 | `options.modelId = m_settings.modelRecipeId();` — unchanged, the key moved |
| `src/runtime/ModelInstaller.cpp` | 278 | `m_settings.selectModelProfile(e.repo, QStringLiteral("blockRecognition"))` |
| `src/runtime/ModelInstaller.cpp` | 257 | `selections.modelPath = m_settings.checkLaunchModelPath();` |
| `src/runtime/LaunchProfileStore.cpp` | 214 | the active profile is read from `m_settings.checkLaunchModelPath()` |
| `src/app/SelfTestController.cpp` | 50 | the self-test model is `m_settings.modelRecipeId()` — the key moved, the read stays |

- [ ] **Step 6: Re-state the settings tests**

`tests/test_settings_store.cpp`:

- `modelKeysMigrateIntoTheTaskNamespace` — delete; the reset replaces it.
- `aStoredRetiredParserIdIsResolved` — delete; the reset replaces it.
- `aModelIdFromTheRetiredIdSpaceIsResolved` — delete; the reset replaces it.
- `activatingAModelSelectsItsFamilyProfile` — re-pointed at `blockRecognition`.

Add two cases:

```cpp
    void aStaleSchemaVersionResetsTheSettings()
    {
        m_settings->setModelRecipeId(QStringLiteral("mineru"));
        m_settings->setValue("settings/schemaVersion", 0);

        SettingsStore fresh;
        QVERIFY(fresh.modelRecipeId().isEmpty());
        QCOMPARE(fresh.value("settings/schemaVersion").toInt(), 1);
    }

    void theResetKeepsTheRuntimePaths()
    {
        m_settings->setRuntimeRootDir(QStringLiteral("/tmp/llocr-runtime"));
        m_settings->setRuntimeModelsDir(QStringLiteral("/tmp/llocr-models"));
        m_settings->setValue("settings/schemaVersion", 0);

        SettingsStore fresh;
        QCOMPARE(fresh.runtimeRootDir(), QStringLiteral("/tmp/llocr-runtime"));
        QCOMPARE(fresh.runtimeModelsDir(), QStringLiteral("/tmp/llocr-models"));
    }
```

`runtimeRootDir()` and `runtimeModelsDir()` are the accessors (`src/config/SettingsStore.h:230-233`); the keys they read are `runtime/rootDir` and `runtime/modelsDir` (`src/config/SettingsStore.h:511-512`).

- [ ] **Step 7: Build and run the tests**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

- [ ] **Step 8: Append the manifest section and commit**

Append to `archive/README.md`:

```markdown
## The settings migration

The removal deleted every piece of settings migration: the retired-key list,
the task-key move table, the retired-model resolution, the retired-parser
resolution and the plaintext-secret move.

The settings are reset once instead — the key `settings/schemaVersion` decides
whether the file predates the reset. `runtime/rootDir` and `runtime/modelsDir`
survive it: they point at the user's disk, not at the schema.
```

```powershell
git add -A archive src tests
git commit -m "feat: reset the settings instead of migrating them"
```

---

## Task 5: Shrink the task list from six to four

**Files:**
- Modify: `src/core/ConnectionRole.h`
- Modify: `src/config/SettingsStore.cpp:35-85`
- Modify: `src/config/RequestProfileStore.cpp`
- Modify: `src/runtime/{ModelInstaller,ModelQuantModel,ModelRegistry,RuntimeController}.{h,cpp}`
- Modify: `src/runtime/ModelInstallTransaction.h`, `src/runtime/ServerLaunchConfig.h`
- Modify: `src/parsers/BlockStyle.cpp:106`
- Modify: `src/core/ModelProfiles.cpp:731-755`
- Modify: `src/main.cpp:97-119`
- Modify: `resources/profiles/models/glm-ocr.json`
- Modify: `src/qml/Settings/RuntimeTabExternal.qml:23`
- Modify: `src/qml/RoleProfiles.qml`
- Modify: `src/qml/Settings/RequestTab.qml:14`, `src/qml/Settings/LaunchTab.qml:13`
- Modify: `src/qml/Common/TaskModelsTable.qml:451`
- Modify: `tests/{test_settings_store,test_runtime_tab_external_qml,test_task_models_table_qml,test_request_profile,test_model_registry,test_ensure_connection,test_launch_profile}.cpp`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: the settings reset from Task 4.
- Produces: `connectionRoleIds()` returns exactly `["blockRecognition", "decision", "layout", "fix"]`. `ConnectionRole::BlockRecognition` is the enum value 0.

- [ ] **Step 1: Shrink the enum**

In `src/core/ConnectionRole.h`:

```cpp
enum class ConnectionRole {
    BlockRecognition,
    Decision,
    Layout,
    Fix,
};

// The app's four tasks, in the order the settings and the task table show
// them. The list is the single source: the settings dispatch, the runtime's
// per-task state and the QML task table read it.
inline constexpr std::array<ConnectionRole, 4> connectionRoles = {
    ConnectionRole::BlockRecognition,
    ConnectionRole::Decision,
    ConnectionRole::Layout,
    ConnectionRole::Fix,
};
```

`connectionRoleName()` loses the `PageOcr` and `RawPageOcr` cases, and its fallback becomes `BlockRecognition`. `connectionRoleFromName()` loses the `"ocr"` alias and its fallback becomes `BlockRecognition`:

```cpp
inline ConnectionRole connectionRoleFromName(const QString &name)
{
    for (const ConnectionRole role : connectionRoles) {
        if (connectionRoleName(role) == name)
            return role;
    }
    return ConnectionRole::BlockRecognition;
}
```

The install queue's filter — `task != connectionRoleName(connectionRoleFromName(task))` — keeps working: a task survives only when it is exactly a canonical role name, so an unknown id is dropped whatever the fallback is.

- [ ] **Step 2: Fix the role table**

In `src/config/SettingsStore.cpp`, `roleSettings()` becomes a four-row table. The `BlockRecognition` row is the one the `pageOcr` row used to be — it owns the OCR model's identity and launch path:

```cpp
const RoleSettings &roleSettings(ConnectionRole role)
{
    static const RoleSettings table[] = {
        // BlockRecognition: the OCR model — its identity and its launch path
        // are the ones the page task used to own.
        {{&SettingsStore::checkLaunchModelPath, &SettingsStore::setCheckLaunchModelPath},
         {&SettingsStore::checkLaunchMmprojPath, &SettingsStore::setCheckLaunchMmprojPath},
         {&SettingsStore::checkLaunchDraftPath, &SettingsStore::setCheckLaunchDraftPath},
         &SettingsStore::modelRecipeId,
         &SettingsStore::setModelRecipeId,
         &SettingsStore::modelName,
         &SettingsStore::setModelName},
        {{&SettingsStore::decisionLaunchModelPath, &SettingsStore::setDecisionLaunchModelPath},
         ...
```

The `static_assert` on `std::size(table) == connectionRoleCount` covers the row count — it is the check that the table was updated.

- [ ] **Step 3: Delete the raw page task's accessors**

In `src/config/SettingsStore.{h,cpp}`, delete every accessor and key constant of the two removed roles:

- `rawPageOcrLaunchModelPath`, `rawPageOcrLaunchMmprojPath`, `rawPageOcrLaunchDraftPath`, `rawPageOcrRequestProfileId`, `rawPageOcrModelName` and their setters;
- the `pageOcr` row's launch-path accessors, now owned by the `check` ones.

The `provider/apiKey/pageOcr` and `tasks/pageOcr/baseUrl` keys go with them; the `check` ones already exist.

- [ ] **Step 4: Re-point the runtime's per-task state**

| File | What |
|------|------|
| `src/runtime/ModelInstaller.cpp:116-249` | `quantModel()`, `presetsForRole()`, `reloadPresetsInternal()` — the `"pageOcr"` literals become `"blockRecognition"` |
| `src/runtime/ModelInstaller.cpp:345` | `activeTaskCount()` |
| `src/runtime/RuntimeController.cpp:343-385` | `modelBuildError()` loses its two page-specific messages |
| `src/runtime/RuntimeController.cpp:698-708` | `beginRoleSwitch()` — the six-way status ternary becomes four-way |
| `src/runtime/RuntimeController.h:91,149,191,196,210` | the default role arguments (`"pageOcr"`, `PageOcr`) become `"blockRecognition"`, `ConnectionRole::BlockRecognition` |
| `src/runtime/ModelInstallTransaction.h:114` | `m_pendingRole = "blockRecognition"` |
| `src/runtime/ServerLaunchConfig.h:19` | the default role argument |
| `src/runtime/ModelQuantModel.cpp:95-106` | the "all" list resolves the first of `{pageOcr, rawPageOcr, layout}` — with the two gone it falls back to the first alphabetical role |
| `src/runtime/ModelQuantModel.cpp:453-462` | the legacy-entry vision heuristic loses its `pageOcr` branch |
| `src/runtime/ModelRegistry.cpp:35-43` | `roleFromIndex()` — the legacy `"ocr"` tag maps to nothing; the roles are re-derived from the profile |

- [ ] **Step 5: Drop the tags of the removed roles from the registry**

`src/runtime/ModelRegistry.cpp:35-43` maps a stored tag to a role name. Today it renames the legacy `"check"` to `blockRecognition` and the legacy `"ocr"` to `pageOcr`; every other tag is returned unchanged, so a model tagged `pageOcr` or `rawPageOcr` would keep a role that no longer exists.

The mapping becomes: a canonical role name passes, a legacy tag is renamed, and anything else is dropped:

```cpp
QString roleFromIndex(const QString &s)
{
    // Index files written before the role renames carry the old tags.
    if (s == QLatin1String("check"))
        return QStringLiteral("blockRecognition");
    if (connectionRoleIds().contains(s))
        return s;
    return QString();
}
```

The legacy `"ocr"` mapping goes with `pageOcr`: the tag could name any of the four removed page models, so it must not silently become a surviving task. An entry whose only tag was `"ocr"` ends up with no role and drops out of the per-task lists until the user re-activates it from Settings → Models, which writes the correct tag.

In `entryFromJson` (around line 96), skip the empty tags:

```cpp
    const QJsonArray roles = o.value(QStringLiteral("roles")).toArray();
    for (const QJsonValue &v : roles) {
        const QString role = roleFromIndex(v.toString());
        if (!role.isEmpty())
            e.roles << role;
    }
```

- [ ] **Step 6: Keep the layout role in the page budget**

In `src/core/ModelProfiles.cpp:731-755`, `visionTokensFor()` decides whether a role gets the page's image budget or the crop's. The "page" set is `{pageOcr, rawPageOcr, layout, all}` — **the `layout` role must stay in it**, or the layout pass gets the crop budget and its memory estimate collapses.

- [ ] **Step 7: Fix the styled-role lookup**

In `src/parsers/BlockStyle.cpp:106`, `kStyledRoles[] = {"pageOcr", "layout", "blockRecognition"}` becomes `{"layout", "blockRecognition"}`. Every surviving model's styled-label vocabulary must be declared under one of those two roles, or its labels lose their styling — check the three survivors' profiles while you are there.

- [ ] **Step 8: Delete the two request-profile stores**

In `src/config/RequestProfileStore.cpp`, `userFileName()` and `legacyFileName()` lose the `requestPage.json` and `requestRawPage.json` entries, and the per-role `activeProfileId()` switch loses its two cases.

In `src/main.cpp:97-119`, the six `RequestProfileStore` singletons become four — `RequestProfilesBlockRecognition`, `RequestProfilesDecision`, `RequestProfilesLayout`, `RequestProfilesFix`.

- [ ] **Step 9: Re-point the QML task tables**

| File | Change |
|------|--------|
| `src/qml/Settings/RuntimeTabExternal.qml:23` | `readonly property var taskIds: ["blockRecognition", "decision", "layout", "fix"]` |
| `src/qml/RoleProfiles.qml` | the `pageOcr` and `rawPageOcr` rows go; the `entry()` fallback re-points at `blockRecognition` |
| `src/qml/Settings/RequestTab.qml:14` | `property string role: "blockRecognition"` |
| `src/qml/Settings/LaunchTab.qml:13` | `property string role: "blockRecognition"` |
| `src/qml/Common/TaskModelsTable.qml:451` | the popup's default `task` becomes `"blockRecognition"` |

- [ ] **Step 10: Drop the raw role from the surviving profile**

In `resources/profiles/models/glm-ocr.json`, delete the `rawPageOcr` role object. GLM-OCR now serves `blockRecognition` only.

- [ ] **Step 11: Re-state the tests**

| Test | Change |
|------|--------|
| `test_settings_store.cpp` | `theTaskListIsTheSingleSource` — the four ids, no `"ocr"` alias, the fallback is `blockRecognition` |
| `test_runtime_tab_external_qml.cpp` | `theSixTasksHaveARow` becomes `theFourTasksHaveARow`; `theTaskEndpointAndAliasAreSaved` — row 0 is `blockRecognition` |
| `test_task_models_table_qml.cpp` | four registered singletons; the fake's `rawPageOcrQuantModels` goes; the fake's `estimateModelMemory` default role is `blockRecognition` |
| `test_request_profile.cpp` | the inline fixtures built on the `pageOcr` role are re-roled |
| `test_model_registry.cpp` | `persistsRoles` re-stated; `migratesLegacyCheckRole` follows the new mapping |
| `test_ensure_connection.cpp` | `everyRoleHasItsOwnConnection` — four roles; the "the other role" in the switch tests becomes a surviving one |
| `test_launch_profile.cpp` | the inline `pageOcr` fixtures are re-roled; `modelParametersDoNotLeakBetweenModels` stays |
| `test_launch_tab_qml.cpp` | the fake's `estimateModelMemory` default role follows the new default |

Add one case to `test_model_installer.cpp` — the install queue must not accept a task that is not a real one:

```cpp
    void anUnknownTaskNeverEntersTheInstallQueue()
    {
        QVariantList choices;
        QVariantMap choice;
        choice.insert(QStringLiteral("task"), QStringLiteral("pageOcr"));
        choice.insert(QStringLiteral("key"), QStringLiteral("mineru"));
        choice.insert(QStringLiteral("quantId"), QStringLiteral("Q4_K_M"));
        choices.append(choice);

        installer.queueTaskInstalls(choices);
        QCOMPARE(installer.taskQueueCount(), 0);
    }
```

The queue's filter is `task != connectionRoleName(connectionRoleFromName(task))` — a task survives only when it is exactly a canonical role name, so `pageOcr` is dropped whatever the fallback is. Use the real accessor name for the queue's size; read it from `src/runtime/ModelInstaller.h`.

- [ ] **Step 12: Build and run the tests**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

- [ ] **Step 13: Append the manifest section and commit**

Append to `archive/README.md`:

```markdown
## The tasks that are gone

The app had six tasks; it ships four. `pageOcr` and `rawPageOcr` are cut — the
single-pass page recognition. The remaining four are `layout`,
`blockRecognition`, `decision` and `fix`.

The role list lives in `src/core/ConnectionRole.h`; the settings dispatch in
`roleSettings()` in `src/config/SettingsStore.cpp`.
```

```powershell
git add -A archive src tests resources
git commit -m "feat: the task list is four tasks"
```

---

## Task 6: Narrow the decision task to the visual diff

**Files:**
- Modify: `src/models/DecisionModel.{h,cpp}`
- Modify: `src/core/DecisionRequest.h`
- Modify: `src/core/DecisionResult.h`
- Modify: `src/app/VerificationQueueController.cpp`
- Modify: `src/core/ModelProfiles.{h,cpp}`
- Delete: `resources/profiles/requests/decision-requests.json` → move to `archive/decision-text/`
- Move: `scripts/decision-eval/`, `scripts/decision-calibrate.py` → `archive/decision-text/scripts/`
- Modify: `tests/test_decision_model.cpp`, `tests/test_model_preset_catalog.cpp`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: the four-task list from Task 5; `minicpm-jev` as the only decision model.
- Produces: one decision protocol. `DecisionModel::buildRequestBody` carries two images, the bare candidate text and the `match` noul question; the wire never carries `instructions` or `criteria`.

- [ ] **Step 1: Move the text-decision material into the archive**

```powershell
New-Item -ItemType Directory -Force -Path archive/decision-text/scripts | Out-Null
git mv resources/profiles/requests/decision-requests.json archive/decision-text/
git mv scripts/decision-eval archive/decision-text/scripts/
git mv scripts/decision-calibrate.py archive/decision-text/scripts/
```

`scripts/probe-ocr-reply.py` stays — it is a generic probe whose mention of Chandra is a comment.

- [ ] **Step 2: Trim the request**

In `src/core/DecisionRequest.h`, delete the fields the text protocol used: `blockType`, `notation`, `question`, `criteriaTrue`, `criteriaFalse`. Keep `image`, `renderImage`, `recognizedText` and `visualDiff`.

- [ ] **Step 3: Trim the result**

In `src/core/DecisionResult.h`, delete `hasText`, `readable`, `diagnosis` and `topDiagnosis()`.

`routedStatus()` reduces to the threshold split — the visual-diff protocol answers only `match`:

```cpp
DecisionResult::Status DecisionResult::routedStatus(double matchThreshold) const
{
    return probability >= matchThreshold ? Status::Ok : Status::Mismatch;
}
```

Read the exact member names from the file before editing; the probability member keeps its name.

- [ ] **Step 4: Trim the request builder**

In `src/models/DecisionModel.cpp`, `buildRequestBody` keeps the visual-diff branch only — two images, the bare candidate text as `state`, the `match` noul question. The text branch goes: the `state` object with `block_type` / `recognized_text` / `notation`, the question with `instructions` / `criteria`, and the single-image `images` array.

- [ ] **Step 5: Trim the verification queue**

In `src/app/VerificationQueueController.cpp`, delete `decisionQuestion()`, `decisionNotation()`, `decisionCriteriaTrue()`, `decisionCriteriaFalse()`, `withDocumentLanguage()` and the fallback question. `judgeBlock()` goes; the visual-diff call becomes the only request.

The consequence is deliberate and matches `docs/31`: the text protocol could answer "this crop is unreadable" and route a block to Review without calling the fixer. The visual-diff protocol answers only `match`, so in the decision-only «Проверить» flow an unreadable crop lands on **Mismatch** and goes to the fixer, which answers **REVIEW** for a crop it cannot read.

- [ ] **Step 6: Delete the dead profile accessors**

In `src/core/ModelProfiles.{h,cpp}`, delete `statePromptFor`, `questionFor`, `criteriaTrueFor`, `criteriaFalseFor`, `decisionBlockFor` and the `ModelProfiles::DecisionBlock` struct, together with the role fields `statePrompt`, `question`, `criteriaTrue`, `criteriaFalse` and `decisionBlocks`.

`blockPromptFor` and `systemPromptFor` **stay** — the fix and block-recognition roles use them.

In the same file, `Role::emptyThreshold` and `Role::readableThreshold` become dead: only the text protocol had them. Delete both fields and their readers.

- [ ] **Step 7: Re-state the tests**

`tests/test_decision_model.cpp`:

- `requestBodyCarriesStateImageAndQuestion` — re-stated for the visual diff: the request carries two images, the state is the bare candidate text, and the question is the `match` noul.
- the v6 answer-set parsing cases — deleted.
- `routedStatus` — re-stated: the split is the threshold alone.

`tests/test_model_preset_catalog.cpp`:

- the cases that pin the decision models' thresholds and prompts — re-stated against `minicpm-jev`, whose `matchThreshold` is `0.5` and whose `matchThresholdByBlock` covers `text`, `equation` and `table`.

- [ ] **Step 8: Build and run the tests**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
```

- [ ] **Step 9: Append the manifest section and commit**

Append to `archive/README.md`:

```markdown
## The text decision protocol

The decision task used two protocols. The text one (`systemone_single_question_v7`)
sent a state object, one `noul` question and a crop, and the model answered
`match`, `has_text` and `readable`. It was spoken by clef-flash, d3-flash and
d3-mini.

The remaining model, MiniCPM-OCR Jev, speaks the visual diff
(`systemone_visual_diff_v1`): the crop, the candidate rendered back into an
image, and the candidate text. The state is bare, the question is the `match`
noul, and the answer is a single probability.

The prompt profile and the calibration harness that the text protocol needed
are in `decision-text/`. `scripts/decision-calibrate.py` still measures
thresholds — point it at the visual diff to recalibrate MiniCPM-Jev.
```

```powershell
git add -A archive src tests resources scripts
git commit -m "feat: the decision task speaks the visual diff only"
```

---

## Task 7: The documentation pass

**Files:**
- Move: `docs/{13-lfm25-vl-3b,15-teleocr,18-clef-flash,21-d3,22-unlimited-ocr,23-lightonocr,28-chandra-ocr-2,30-minicpm-ocr}.md` and their `.ru.md` mirrors → `archive/models/docs/`
- Move: `docs/UnlimitedOCR.md`, `docs/17-d1-verification-study.md` → `archive/models/docs/`
- Modify: `docs/07-glossary.md`, `docs/20-model-task-rework.md`, `docs/27-memory-estimate.md` (+ `.ru.md`)
- Modify: `docs/{01-overview,02-tech-stack,03-architecture,04-components,06-dev-setup}.md`
- Modify: `docs/{19-glm-ocr,24-mineru,25-paddleocr-vl,29-external-runtime-tasks-and-secrets,31-minicpm-jev,32-orientation-pipeline}.md` (+ their `.ru.md` mirrors)
- Modify: `docs/ext-runtime-models.md`, `docs/tofix-findings.md`
- Modify: `AGENTS.md`
- Modify: `archive/README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: no new ADR number is invented — the next free number after the highest in `docs/07-glossary.md`.

- [ ] **Step 1: Move the removed manuals into the archive**

```powershell
New-Item -ItemType Directory -Force -Path archive/models/docs | Out-Null
foreach ($n in @('13-lfm25-vl-3b','15-teleocr','18-clef-flash','21-d3','22-unlimited-ocr','23-lightonocr','28-chandra-ocr-2','30-minicpm-ocr')) {
  git mv "docs/$n.md" archive/models/docs/
  if (Test-Path "docs/$n.ru.md") { git mv "docs/$n.ru.md" archive/models/docs/ }
}
git mv docs/UnlimitedOCR.md archive/models/docs/
git mv docs/17-d1-verification-study.md archive/models/docs/
```

- [ ] **Step 2: Record the decisions in the glossary**

In `docs/07-glossary.md`, add the ADRs to the table. Four entries, each one line, in the established format:

- the archive — what was cut, where it went, how to bring it back;
- the four-task matrix — `pageOcr` and `rawPageOcr` are cut, the six-task plan becomes a four-task plan;
- the settings reset — the migration code is deleted, the settings are reset once, the runtime paths survive;
- the decision protocol — the text protocol is cut, the visual diff is the only one.

Read the table's existing format before writing the rows and use the next free ADR numbers.

- [ ] **Step 3: Re-state the task plan**

In `docs/20-model-task-rework.md`, the six-task plan becomes a four-task plan: the stage table loses the stages that belonged to the removed tasks, and the "six tasks" wording becomes "four tasks".

- [ ] **Step 4: Re-state the memory calibration**

In `docs/27-memory-estimate.md` and its `.ru.md` mirror, the calibration table loses the nine removed models. Every surviving model keeps its calibrated numbers untouched.

- [ ] **Step 5: Fix the prose**

In `docs/{01-overview,02-tech-stack,03-architecture,04-components,06-dev-setup}.md`, in `docs/{19-glm-ocr,24-mineru,25-paddleocr-vl,29-external-runtime-tasks-and-secrets,31-minicpm-jev,32-orientation-pipeline}.md` and their `.ru.md` mirrors, in `docs/ext-runtime-models.md` and in `docs/tofix-findings.md`:

- a mention of a removed model becomes a mention of the archive, not a deletion;
- a mention of the six tasks becomes four;
- a mention of the single-pass page recognition is removed.

Search for each removed model's name and each removed role's name — do not rely on memory of what was written.

- [ ] **Step 6: Update the project's own instructions**

In `AGENTS.md`:

- the documentation map loses the entries of the moved manuals and points at `archive/models/docs/`;
- the per-task model table shows the four tasks and the eight models;
- the task defaults show MinerU for `layout` and `blockRecognition`, MiniCPM-Jev for `decision`, Qwen3.5-4B for `fix`.

- [ ] **Step 7: Check that nothing points at a moved file**

```powershell
Select-String -Path docs\*.md, AGENTS.md, src\*.md -Pattern '13-lfm25-vl-3b|15-teleocr|18-clef-flash|21-d3|22-unlimited-ocr|23-lightonocr|28-chandra-ocr-2|30-minicpm-ocr|17-d1-verification-study|UnlimitedOCR\.md'
```

Expected: no hits outside `archive/`. A hit in `docs/` is a dangling reference — fix it.

- [ ] **Step 8: Commit**

```powershell
git add -A docs AGENTS.md archive
git commit -m "docs: the four-task release — the glossary, the manuals and the archive"
```

---

## Task 8: The release gate

**Files:**
- Modify: `archive/README.md`
- Create: nothing.

**Interfaces:**
- Consumes: everything above.
- Produces: the archive's index, complete.

- [ ] **Step 1: Run the full verification**

```powershell
cmake --build build/win-msvc2022 -j 4
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure
Get-ChildItem src,tests -Recurse -Include *.cpp,*.h | ForEach-Object { clang-format --dry-run --Werror $_.FullName }
```

Expected: the build succeeds, `ctest` is green, and clang-format reports nothing. If clang-format is not on `PATH`, install LLVM or skip with that note — never claim the gate passed.

The QML lint:

```powershell
$env:QT_QML_OUTPUT_DIR = "build/src"
Get-ChildItem src/qml -Recurse -Filter *.qml | ForEach-Object { & C:\Qt\6.10.3\msvc2022_64\bin\qmllint --max-warnings 0 -I build/src $_.FullName }
```

Expected: no warnings above the demoted categories in `.qmllint.ini`.

- [ ] **Step 2: Check that no survivor lost a label**

`src/app/BoxListModel.cpp:185` carries the picture labels `image|chart|figure|char`. `figure` and `char` came from TeleOCR and Unlimited-OCR — both cut. Look at the three surviving block-recognition profiles (`glm-ocr`, `mineru`, `paddleocr-vl`) and at `resources/profiles/labels.json`: if none of them emits `figure` or `char`, the two labels go; if one does, they stay.

- [ ] **Step 3: Verify the archive is complete**

```powershell
Get-ChildItem archive -Recurse -File | Select-Object -ExpandProperty FullName
```

Expected: nine profiles in `archive/models/profiles/`, six parsers in `archive/parsers/`, the controller in `archive/single-pass/`, the prompt profile and the scripts in `archive/decision-text/`, the manuals in `archive/models/docs/`.

Cross-check against the manifest table in `archive/README.md`: every removed model named there has its profile in the archive.

- [ ] **Step 4: Verify the tree is clean**

```powershell
git status --short
git log --oneline -10
```

Expected: a clean tree and the commits of Tasks 1-8.

- [ ] **Step 5: Write the archive's index**

Append to `archive/README.md`:

```markdown
## The index

| Model | Profile | Parser | Manual |
|-------|---------|--------|--------|
| LightOnOCR-3-4B | `models/profiles/lightonocr.json` | `parsers/LightonOcrParser.*` | `models/docs/23-lightonocr.md` |
| Chandra-OCR-2 | `models/profiles/chandra-ocr-2.json` | `parsers/ChandraParser.*` | `models/docs/28-chandra-ocr-2.md` |
| LFM2.5-VL-3B | `models/profiles/lfm25-vl-3b.json` | `parsers/Lfm25VlParser.*`, `parsers/Lfm25VlDrift.*` | `models/docs/13-lfm25-vl-3b.md` |
| Unlimited-OCR | `models/profiles/unlimited-ocr.json` | `parsers/UnlimitedOcrParser.*` | `models/docs/22-unlimited-ocr.md` |
| MiniCPM-OCR 1.0 | `models/profiles/minicpm-ocr.json` | — | `models/docs/30-minicpm-ocr.md` |
| TeleOCR (NaviDC-OCR) | `models/profiles/teleocr.json` | `parsers/TeleOcrLayoutParser.*` | `models/docs/15-teleocr.md` |
| Cloudflare clef-flash | `models/profiles/clef-flash.json` | — | `models/docs/18-clef-flash.md` |
| d3-flash | `models/profiles/d3-flash.json` | — | `models/docs/21-d3.md` |
| d3-mini | `models/profiles/d3-mini.json` | — | `models/docs/21-d3.md` |

The single-pass controller is in `single-pass/`; the text-decision prompt
profile and the calibration harness are in `decision-text/`.
```

- [ ] **Step 6: Commit**

```powershell
git add -A archive
git commit -m "docs: the archive index"
```

---

## Self-Review

**Spec coverage.** Every section of the design maps to a task: the archive (§5) → Tasks 1, 2, 3, 6; the role list (§6.1) → Task 5; the single-pass feature (§6.2) → Task 3; the settings keys (§6.3) → Task 4; the decision protocol (§6.4) → Task 6; the models and parsers (§6.5) → Task 2; the settings reset (§6.6) → Task 4; the QML surface (§6.7) → Tasks 3 and 5; the page modes (§6.8) → unchanged by design, verified in Task 8; the documentation (§6.9) → Task 7; the tests (§7) → the test steps of Tasks 2, 3, 4, 5 and 6.

**Type consistency.** `connectionRoleIds()` returns four ids in every task after Task 5. `modelRecipeId()` keeps its name and moves its key in Task 4, so the readers re-pointed in Task 4 Step 5 and the `roleSettings` row in Task 5 Step 2 agree on `model/recipeId`. `resetToDefaults()` is introduced in Task 4 and used by the constructor in the same task.

**Placeholder scan.** No step says "implement the rest", "handle the edge cases" or "similar to Task N". Every step names the files, the symbols and the assertions.

**Known risks the plan does not resolve by itself.** These are decisions the implementer makes against the code as it stands, and each is called out at the point of action: the runtime-path accessors are named in Task 4 Step 6; the registry's own signatures are named in Task 5 Step 5; the ADR numbers in Task 7 Step 2; the install queue's size accessor in Task 5 Step 11.

**Spec items carried into the plan.** The two "checks that the removal cannot break a survivor" from the design's §6.5 that are not code deletions — the `visionTokensFor` page set and the styled-role lookup — are steps in Task 5 (Steps 6 and 7). The picture-label check (`BoxListModel.cpp:185`) is Task 8 Step 2. The three new test cases the design's §7.2 asks for are Task 4 Step 6 (two reset cases), Task 5 Step 11 (the install-queue case) and Task 6 Step 7 (the visual-diff request).
