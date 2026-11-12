# Gemma 4 26B-A4B support + MoE memory layout — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `gemma-4-26b-a4b` (fix role, not default) to the shipped catalog and teach the memory-estimate UI the MoE RAM/VRAM split: minimum RAM, VRAM floor for partial offload, and the actual weights layout against the machine's card.

**Architecture:** The estimator (`ModelMemoryEstimator`) gains `cpuOffload`-aware numbers (`vramFloorBytes`, `requiredRamBytes`, a `splitForVramBudget` helper) driven by two new profile `memory` fields. `RuntimeController::estimateModelMemory` exposes them plus `overRamBudget`; `ModelQuantModel` carries per-quant labels; two QML spots render them. A new profile JSON + catalog-test row ship the model; launch layer pins `n-gpu-layers auto`, `ubatch-size 2048`, `ctx-size 8192`, `fit-target 512`.

**Tech Stack:** Qt6/C++ (llocr_core `ModelProfiles`, llocr_runtime estimator/controller/quant model), QML, QtTest, GGUF resource glob.

**Spec:** `docs/superpowers/specs/2026-11-12-gemma-4-26b-moe-memory-design.md` (approved 2026-11-12).

## Global Constraints

- Numbers are **GiB** (2^30). `formatBytes` (`src/runtime/` helper used by both bridges) renders them; never hardcode a font size — QML text uses `Theme.footnoteSize` / `Theme.helpColor` / `Theme.error`.
- RAM-error threshold is **1.0 × systemRamBytes** (not 0.9): 0.9 fires on the owner's working 16 GB machine (spec anti-goal). Existing `overBudget` (0.9×) and `overVramBudget` semantics are unchanged for models without `cpuOffload`.
- UI strings are English `qsTr` in code + Russian translations appended to `resources/i18n/llocr_ru.ts` (wired at `src/CMakeLists.txt:615`). Translation `source` text must match the qsTr string byte-for-byte.
- Profile launch values: JSON string → `LaunchValueKind::Text` (emitted verbatim), number → Number, absent → Flag (`src/core/LaunchProfile.cpp:115-131`). `"n-gpu-layers": "auto"` is therefore legal.
- Profile files are globbed into the resource by `src/CMakeLists.txt:453` — no qrc edit needed.
- `minBuild` of the new profile: **b12137** (older builds parse `--n-gpu-layers auto` as 0 via atoi).
- Model values (spec §Числа модели): weights 14 249 047 104; mmproj 1 193 058 784; draft 251 939 328; draftKv 4 096 B/token; draftContext 346 051 051; kvPerToken 20 480; kvFixed 629 145 600; wsPerToken 220 216; wsCtx 2; vision 616 980 B/token; visionTokens 1120; ctx 8192; ubatch 2048.
- Role: fix only, alias `llocr-fix`, **no `"default": true`** anywhere in the profile.
- Build/test tree: `build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release` (JOM, Release); Qt DLLs must be on PATH: `export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH"`.

## Review Focus

- **A machine where the model fits VRAM entirely** (≥ 18.2 GiB card): the split line must not appear and the dropdown must still read one VRAM requirement. Test: Task 4's `cpuOffloadFalseLeavesEstimateUnchanged` + Task 4's split test with a large budget (`weightsInVram == weightsBytes`, `weightsInRam == 0`) pins `weightsInVram` clamping.
- **16 GB RAM machine** (the owner's): `overRamBudget` must be **false** (0.9× would fire on this exact machine; the threshold is 1.0×). Test: Task 1 pins `requiredRamBytes` byte-exactly; the single comparison line `e.requiredRamBytes > e.systemRamBytes` in Task 2 has no unit test today (the controller bridge was never unit-tested) — it is a named review point for the branch review.
- **A non-offload model** (qwen3.5-4b): dropdown rows and LaunchTab sentences must render exactly as today (no `cpuOffload` keys visible). Test: Task 1's `cpuOffloadFalseLeavesEstimateUnchanged` + Task 3 keeps `quantItemText`'s old branch.
- **GPU-less machine** (`primaryGpuMemoryBytes() == 0`): `splitForVramBudget(0)` must put everything in RAM (`ramRequired = totalBytes + headroom`), not under-report. Test: Task 1's `cpuOffloadSplitWithoutCard`.
- **The empty-quant / unknown-model path** (a profile with `cpuOffload` but no `memory.valid`, or a model file outside the catalog): floors/ram labels must stay empty rather than render `RAM ≥ 0 B`. Test: Task 1 asserts floor/ram stay 0 when `cpuOffload` is false; Task 3's quantMap emits empty labels when `vramFloorBytes == 0`.

---

### Task 1: Estimator — `cpuOffload` floor, RAM requirement, split helper

**Files:**
- Modify: `src/runtime/ModelMemoryEstimator.h`
- Modify: `src/runtime/ModelMemoryEstimator.cpp` (`estimateMemory`, anonymous namespace constants)
- Modify: `src/core/ModelProfiles.h` (`struct Memory`)
- Modify: `src/core/ModelProfiles.cpp` (`readMemory`, `shapeFromProfile` lives in estimator — copy fields there)
- Test: `tests/test_model_memory_estimator.cpp`

**Interfaces:**
- Consumes: existing `ModelMemoryShape`, `ModelMemoryBudget`, `estimateMemory` (unchanged signatures).
- Produces (later tasks rely on exact names):
  - `ModelProfiles::Memory` fields: `bool cpuOffload = false;` `qint64 ramHeadroomBytes = 0;`
  - `ModelMemoryShape` fields: same two names; `shapeFromProfile` copies them.
  - `ModelMemoryEstimate` fields: `qint64 vramFloorBytes = 0;` `qint64 requiredRamBytes = 0;`
  - `struct MemorySplit { qint64 weightsInVramBytes; qint64 weightsInRamBytes; qint64 ramRequiredBytes; };`
  - `MemorySplit splitForVramBudget(const ModelMemoryEstimate &estimate, qint64 vramBudgetBytes);` — free function in `namespace llocr`.
  - Constant: `kRamHeadroomBytes = 2LL * 1024 * 1024 * 1024` (used when `ramHeadroomBytes == 0`).

- [ ] **Step 1: Write the failing tests** in `tests/test_model_memory_estimator.cpp` (private slots, same style as `countsTheVisionEncoderAndTheOverhead`):

```cpp
// The Gemma 4 26B-A4B numbers (spec 2026-11-12): a MoE model that runs with
// llama.cpp's fit splitting the weights between the card and RAM.
static ModelMemoryShape moe26Shape()
{
    ModelMemoryShape shape;
    shape.weightsBytes = 14249047104LL;
    shape.projectorBytes = 1193058784LL;
    shape.draftBytes = 251939328LL;
    shape.draftKvBytesPerToken = 4096;
    shape.draftContextBytes = 346051051LL;
    shape.kvBytesPerToken = 20480;
    shape.kvBytesFixed = 629145600LL;
    shape.workingSetBytesPerToken = 220216;
    shape.workingSetContextBytes = 2;
    shape.visionBytesPerToken = 616980;
    shape.nLayer = 30;
    shape.nKvLayers = 5;
    shape.nHead = 16;
    shape.nKvHead = 8;
    shape.headDim = 128;
    shape.nEmbd = 2816;
    shape.cpuOffload = true;
    return shape;
}

void TestModelMemoryEstimator::cpuOffloadAddsFloorAndRamFloor()
{
    ModelMemoryBudget budget;
    budget.ctxSize = 8192;
    budget.ubatchTokens = 2048;
    budget.visionTokens = 1120;
    const ModelMemoryEstimate e = estimateMemory(moe26Shape(), budget);

    // draft context, KV, working set, vision — pinned by the spec
    QCOMPARE(e.draftContextBytes, 4096LL * 8192 + 346051051LL);
    QCOMPARE(e.kvCacheBytes, 20480LL * 8192 + 629145600LL);
    QCOMPARE(e.computeBufferBytes, 220216LL * 2048 + 8192LL * 2048 * 2);
    QCOMPARE(e.visionBufferBytes, 616980LL * 1120);
    // the floor: everything but the main weights, with the same margin rule
    const qint64 base = e.totalBytes - e.modelBytes;
    QCOMPARE(e.vramFloorBytes, base + 64LL * 1024 * 1024 + base * 8 / 100);
    QCOMPARE(e.vramFloorBytes, 4167972279LL);
    // RAM floor: the weights plus the headroom (default constant when 0)
    QCOMPARE(e.requiredRamBytes, e.modelBytes + 2LL * 1024 * 1024 * 1024);
    QCOMPARE(e.requiredRamBytes, 16396530752LL);
    QCOMPARE(e.requiredVramBytes, 19556943151LL);
}

void TestModelMemoryEstimator::cpuOffloadSplitsWeightsByCardBudget()
{
    ModelMemoryBudget budget;
    budget.ctxSize = 8192;
    budget.ubatchTokens = 2048;
    budget.visionTokens = 1120;
    const ModelMemoryEstimate e = estimateMemory(moe26Shape(), budget);

    // the owner's card: 6 GiB, the app's budget is 0.9 ×
    const MemorySplit s = splitForVramBudget(e, 5798205849LL);
    QCOMPARE(s.weightsInVramBytes, 1630233570LL);
    QCOMPARE(s.weightsInRamBytes, e.modelBytes - s.weightsInVramBytes);
    QCOMPARE(s.ramRequiredBytes, s.weightsInRamBytes + 2LL * 1024 * 1024 * 1024);

    // a card that holds everything: nothing stays in RAM
    const MemorySplit big = splitForVramBudget(e, e.vramFloorBytes + e.modelBytes);
    QCOMPARE(big.weightsInVramBytes, e.modelBytes);
    QCOMPARE(big.weightsInRamBytes, 0LL);

    // no card at all: everything in RAM, buffers included
    const MemorySplit none = splitForVramBudget(e, 0);
    QCOMPARE(none.weightsInVramBytes, 0LL);
    QCOMPARE(none.ramRequiredBytes, e.totalBytes + 2LL * 1024 * 1024 * 1024);
}

void TestModelMemoryEstimator::cpuOffloadFalseLeavesEstimateUnchanged()
{
    ModelMemoryShape shape = moe26Shape();
    shape.cpuOffload = false;
    ModelMemoryBudget budget;
    budget.ctxSize = 8192;
    budget.ubatchTokens = 2048;
    budget.visionTokens = 1120;
    const ModelMemoryEstimate e = estimateMemory(shape, budget);
    QCOMPARE(e.vramFloorBytes, 0LL);
    QCOMPARE(e.requiredRamBytes, 0LL);
    QCOMPARE(e.requiredVramBytes, e.totalBytes + e.overheadBytes);
    const MemorySplit s = splitForVramBudget(e, 5798205849LL);
    QCOMPARE(s.weightsInVramBytes, 0LL);
    QCOMPARE(s.weightsInRamBytes, 0LL);
    QCOMPARE(s.ramRequiredBytes, 0LL);
}
```

Add the three slot declarations to the class in `tests/test_model_memory_estimator.h`.

- [ ] **Step 2: Run to verify they fail**

Run: `export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH" && cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release --target test_model_memory_estimator && ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release -R model_memory_estimator --output-on-failure`
Expected: FAIL — `Memory` has no member `cpuOffload` (compile error counts as the failing state).

- [ ] **Step 3: Implement**

`ModelProfiles.h::Memory`: add the two fields with the spec's comments (cpuOffload — the model runs with llama.cpp splitting the weights between VRAM and RAM; ramHeadroomBytes — OS/app reserve on top of the weights, 0 = the estimator's constant). `readMemory`: `memory.cpuOffload = object.value(QStringLiteral("cpuOffload")).toBool(false);` and the `ramHeadroomBytes` double→qint64 read like the neighbors. `ModelMemoryShape`: same two fields; `shapeFromProfile` copies them.

`ModelMemoryEstimator.h`: `ModelMemoryEstimate` gains `vramFloorBytes`, `requiredRamBytes`; add the `MemorySplit` struct and `splitForVramBudget` declaration. `estimateMemory` after `requiredVramBytes`:

```cpp
if (shape.cpuOffload) {
    const qint64 base = e.totalBytes - e.modelBytes;
    e.vramFloorBytes = base + kServerOverheadBytes + base * kOverheadShare / 100;
    const qint64 headroom = shape.ramHeadroomBytes > 0 ? shape.ramHeadroomBytes : kRamHeadroomBytes;
    e.requiredRamBytes = e.modelBytes + headroom;
}
```

`splitForVramBudget` (cpp, namespace llocr):

```cpp
MemorySplit splitForVramBudget(const ModelMemoryEstimate &estimate, qint64 vramBudgetBytes)
{
    MemorySplit s;
    if (!estimate.vramFloorBytes || !estimate.requiredRamBytes)
        return s;
    const qint64 headroom = estimate.requiredRamBytes - estimate.modelBytes;
    if (vramBudgetBytes <= 0) {
        s.weightsInRamBytes = estimate.modelBytes;
        s.ramRequiredBytes = estimate.totalBytes + headroom;
        return s;
    }
    s.weightsInVramBytes = qBound<qint64>(0, vramBudgetBytes - estimate.vramFloorBytes, estimate.modelBytes);
    s.weightsInRamBytes = estimate.modelBytes - s.weightsInVramBytes;
    s.ramRequiredBytes = s.weightsInRamBytes + headroom;
    return s;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: same ctest command as Step 2.
Expected: PASS (all three new tests; the whole target stays green).

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ModelMemoryEstimator.h src/runtime/ModelMemoryEstimator.cpp src/core/ModelProfiles.h src/core/ModelProfiles.cpp tests/test_model_memory_estimator.h tests/test_model_memory_estimator.cpp scripts/measure-memory.sh
git commit -m "feat: the estimator knows the MoE split — VRAM floor, RAM floor, card layout"
```

(Include the already-applied `scripts/measure-memory.sh` `UBACK`→`UBATCH` fix in this commit — it is the measurement tool this feature's numbers came from.)

### Task 2: RuntimeController — expose the split to the LaunchTab

**Files:**
- Modify: `src/runtime/RuntimeController.cpp` (`estimateModelMemory`, around line 1090)

**Interfaces:**
- Consumes: Task 1's `vramFloorBytes`, `requiredRamBytes`, `splitForVramBudget`, `ModelProfiles::Memory::cpuOffload`.
- Produces (QML reads these keys): `cpuOffload` (bool), `vramFloorLabel`, `requiredRamLabel`, `splitInVramLabel`, `splitInRamLabel` (formatted strings, empty when not cpuOffload), `ramLabel` (this machine's RAM via `formatBytes(e.systemRamBytes)`), `overRamBudget` (bool).

- [ ] **Step 1: Extend the map** after the existing `vramLabel` insert:

```cpp
const bool cpuOffload = profile && profile->memory.valid && profile->memory.cpuOffload;
out.insert(QStringLiteral("cpuOffload"), cpuOffload);
const HardwareReport::MemorySplit split = ... // see below
```

Concretely: after `overVramBudget`, compute

```cpp
const qint64 vramBudget = hardware.primaryGpuMemoryBytes() > 0 ? hardware.memoryBudgetBytes() : 0;
const llocr::MemorySplit s = splitForVramBudget(e, vramBudget);
out.insert(QStringLiteral("splitInVramLabel"), cpuOffload ? formatBytes(s.weightsInVramBytes) : QString());
out.insert(QStringLiteral("splitInRamLabel"), cpuOffload ? formatBytes(s.weightsInRamBytes) : QString());
out.insert(QStringLiteral("vramFloorLabel"), cpuOffload ? formatBytes(e.vramFloorBytes) : QString());
out.insert(QStringLiteral("requiredRamLabel"), cpuOffload ? formatBytes(e.requiredRamBytes) : QString());
out.insert(QStringLiteral("ramLabel"), formatBytes(e.systemRamBytes));
out.insert(QStringLiteral("overRamBudget"),
           cpuOffload && valid && e.systemRamBytes > 0 && e.requiredRamBytes > e.systemRamBytes);
```

Keep `overVramBudget` and `overBudget` exactly as they are (raw semantics; QML decides what to show).

- [ ] **Step 2: Build and run the estimator tests** (the bridge itself has no unit test today — keep it a formatting shim over Task 1's logic).

Run: `cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release && ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release -R model_memory --output-on-failure`
Expected: build OK, tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/runtime/RuntimeController.cpp
git commit -m "feat: the launch tab receives the MoE split and the RAM budget"
```

### Task 3: ModelQuantModel — per-quant split labels

**Files:**
- Modify: `src/runtime/ModelQuantModel.cpp` (`requiredVram` → `requirementFor`, `Quant` struct fields, `quantMap`)
- Modify: `src/runtime/ModelQuantModel.h` (if `Quant` is declared there; otherwise the cpp-local struct)
- Test: existing suite must stay green (no dedicated quant-model test exists; Task 6's profile exercises the row).

**Interfaces:**
- Consumes: Task 1's estimate fields via `estimateMemory`.
- Produces (QML keys): quant map gains `cpuOffload` (bool), `ramLabel` (string), `vramFloorLabel` (string); `vramLabel` stays the full-VRAM requirement.

- [ ] **Step 1: Implement.** Rename `requiredVram(profile, weightsBytes, projectorBytes)` → `ModelMemoryEstimate requirementFor(const ModelProfiles::Profile &profile, qint64 weightsBytes, qint64 projectorBytes) const` (body identical, but return the estimate instead of one number). In `buildRows` (line ~287):

```cpp
const ModelMemoryEstimate req = requirementFor(profile, quant.size, projectorBytes);
quant.vramBytes = req.requiredVramBytes;
quant.vramFloorBytes = req.vramFloorBytes;
quant.ramBytes = req.requiredRamBytes;
quant.cpuOffload = profile.memory.valid && profile.memory.cpuOffload;
```

`Quant` gains the three fields (defaults 0/false). `quantMap` adds:

```cpp
item.insert(QStringLiteral("cpuOffload"), q.cpuOffload);
item.insert(QStringLiteral("ramLabel"), q.cpuOffload && q.ramBytes > 0 ? ::llocr::formatBytes(q.ramBytes) : QString());
item.insert(QStringLiteral("vramFloorLabel"), q.cpuOffload && q.vramFloorBytes > 0 ? ::llocr::formatBytes(q.vramFloorBytes) : QString());
```

- [ ] **Step 2: Build + full test run**

Run: `cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release && ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release -R "model_" --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/runtime/ModelQuantModel.cpp src/runtime/ModelQuantModel.h
git commit -m "feat: quant rows carry the RAM and split-VRAM labels"
```

### Task 4: QML — dropdown row and LaunchTab lines, RU translations

**Files:**
- Modify: `src/qml/Common/TaskModelsTable.qml` (`quantItemText`, ~line 214)
- Modify: `src/qml/Settings/LaunchTab.qml` (labels after line 128)
- Modify: `resources/i18n/llocr_ru.ts` (new `<message>` blocks near the existing "Video memory" strings, ~line 1513)

**Interfaces:**
- Consumes: Task 2's map keys (`cpuOffload`, `splitInVramLabel`, `splitInRamLabel`, `vramFloorLabel`, `requiredRamLabel`, `ramLabel`, `overRamBudget`), Task 3's quant keys.

- [ ] **Step 1: `quantItemText`** — replace the tail:

```qml
if (q.cpuOffload === true && q.ramLabel.length > 0)
    s += " · " + qsTr("RAM ≥ %1 · VRAM ≥ %2 (split) / %3 (full)").arg(q.ramLabel).arg(q.vramFloorLabel).arg(q.vramLabel)
else if (q.vramLabel.length > 0)
    s += " · " + qsTr("VRAM ≥ %1").arg(q.vramLabel)
```

- [ ] **Step 2: LaunchTab** — after the existing estimate label (line ~128), add:

```qml
LLOLabel {
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    font.pixelSize: Theme.footnoteSize
    color: Theme.helpColor
    visible: root.memoryEstimate.cpuOffload === true && root.memoryEstimate.overVramBudget === true
    text: qsTr("Partial offload: the card holds %1 of the weights, %2 stay in RAM — RAM ≥ %3 of %4 on this machine.")
          .arg(root.memoryEstimate.splitInVramLabel)
          .arg(root.memoryEstimate.splitInRamLabel)
          .arg(root.memoryEstimate.requiredRamLabel)
          .arg(root.memoryEstimate.ramLabel)
}
```

The existing red `overVramBudget` label gets `visible: root.memoryEstimate.overVramBudget === true && root.memoryEstimate.cpuOffload !== true`, and a new red label:

```qml
LLOLabel {
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    font.pixelSize: Theme.footnoteSize
    color: Theme.error
    visible: root.memoryEstimate.overRamBudget === true
    text: qsTr("Not enough RAM for partial offload: the model needs %1, this machine has %2.")
          .arg(root.memoryEstimate.requiredRamLabel)
          .arg(root.memoryEstimate.ramLabel)
}
```

- [ ] **Step 3: RU translations** in `resources/i18n/llocr_ru.ts` — three new `<message>` blocks next to the existing "Video memory" ones (line ~1513), `<source>` byte-identical to the qsTr strings, Russian translations:

- `RAM ≥ %1 · VRAM ≥ %2 (split) / %3 (full)` → `RAM ≥ %1 · VRAM ≥ %2 (частично) / %3 (полностью)`
- `Partial offload: the card holds %1 of the weights, %2 stay in RAM — RAM ≥ %3 of %4 on this machine.` → `Частичная выгрузка: карта держит %1 весов, %2 остаются в памяти — нужно RAM ≥ %3 из %4 на этой машине.`
- `Not enough RAM for partial offload: the model needs %1, this machine has %2.` → `Для частичной выгрузки не хватает RAM: модели нужно %1, на этой машине %2.`

- [ ] **Step 4: Verify** — build + qmllint + the full unit suite:

Run: `cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release && ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release --output-on-failure -E "test_app"` (full green expected; `scripts/check.sh` is the final gate in Task 7).
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/qml/Common/TaskModelsTable.qml src/qml/Settings/LaunchTab.qml resources/i18n/llocr_ru.ts
git commit -m "feat: the model UI shows the RAM floor and the split VRAM layout"
```

### Task 5: The `gemma-4-26b-a4b` profile + catalog tests

**Files:**
- Create: `resources/profiles/models/gemma-4-26b-a4b.json`
- Modify: `tests/test_model_preset_catalog.cpp` (the `expected` table, ~line 96)
- Test: `tests/test_model_preset_catalog.cpp`

**Interfaces:**
- Consumes: the 12B profile as the template (`resources/profiles/models/gemma-4-12b.json`), Task 1's `Memory` fields.
- Produces: catalog id `gemma-4-26b-a4b`; preset (repo `unsloth/gemma-4-26B-A4B-it-qat-GGUF`) with quant `q4_k_xl`, mmproj `mmproj-F16.gguf`, mtp `mtp-gemma-4-26B-A4B-it.gguf`.

- [ ] **Step 1: Add the catalog-test row** to `expected` (after the `gemma-4-12b` line):

```cpp
{"gemma-4-26b-a4b", "fix", "b12137", "q4_k_xl", "spec-type,spec-draft-n-max,temp,parallel,no-warmup,jinja,reasoning-budget,image-min-tokens,ubatch-size,ctx-size,n-gpu-layers,fit-target"},
```

Extend the comment above the table: ctx-size is listed deliberately for this model (precedent `minicpm-jev` — a model whose memory profile pins the context); `n-gpu-layers`/`fit-target` likewise pinned by the model's split layout.

- [ ] **Step 2: Write the profile JSON** — copy `gemma-4-12b.json` and change:

- `id`: `"gemma-4-26b-a4b"`; `title`: `"Gemma 4 26B-A4B QAT"`; `minBuild`: `"b12137"`; `license` unchanged (same repo family).
- `files`: repo `unsloth/gemma-4-26B-A4B-it-qat-GGUF`; quants: one entry `{"id": "q4_k_xl", "file": "gemma-4-26B-A4B-it-qat-UD-Q4_K_XL.gguf", "sha256": "", "sizeBytes": 14249047104}`; `mmproj`: `{"sizeBytes": 1193058784, "id": "f16", "file": "mmproj-F16.gguf", "sha256": "", "_note": "Mandatory: without the projector the model sees no image."}`; `mtp`: `{"id": "q4_0", "file": "mtp-gemma-4-26B-A4B-it.gguf", "sha256": ""}`.
- `memory` block (spec §Числа модели, exact values):

```json
"memory": {
  "nLayer": 30, "nKvLayers": 5, "nHead": 16, "nKvHead": 8, "headDim": 128,
  "nEmbd": 2816, "nFf": 2112,
  "nMoeLayers": 30, "nExpertUsed": 8, "nExpertFf": 704,
  "visionTokens": 1120, "visionBytesPerToken": 616980,
  "draftBytes": 251939328, "draftKvBytesPerToken": 4096, "draftContextBytes": 346051051,
  "workingSetBytesPerToken": 220216, "workingSetContextBytes": 2,
  "kvBytesPerToken": 20480, "kvBytesFixed": 629145600,
  "cpuOffload": true
}
```

- `roles.fix.launch` — the 12B rows with these changes: `spec-draft-n-max` value **2**; **no** `ubatch-size 2048` description copy — write the 26B one ("Encoder-free vision: the whole image must fit one ubatch; the default 512 aborts the server on the first image decode"); append three rows (renumber `order`):

```json
{ "order": 8, "name": "ctx-size", "value": 8192,
  "description": "The split layout is tuned for this window: 16384 spends ~600 MiB more VRAM for nothing (measured b12137). Overrides the platform's 16384." },
{ "order": 9, "name": "n-gpu-layers", "value": "auto",
  "description": "Let llama.cpp's --fit split the 14 GB of weights between VRAM and RAM; the platform's 99 aborts the load with an out-of-memory on a 6 GB card. Requires build b12100+ ('auto' parsed as 0 by older atoi)." },
{ "order": 10, "name": "fit-target", "value": 512,
  "description": "The fit margin llama.cpp keeps free on the card; 512 is the measured layout for a 6 GB card (bigger margins leave fewer expert layers in VRAM and slow the prefill)." }
```

`roles.fix.request` — identical to the 12B's (temperature 0, repeat/presence/frequency penalties, `stream false`, `cache_prompt false`, `stop ["</CANDIDATE>"]`).
No `"default": true` anywhere.

- [ ] **Step 3: Run the catalog tests**

Run: `ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Release -R model_preset_catalog --output-on-failure`
Expected: FAIL first if the JSON is wrong (run Step 1 alone before Step 2 if you want the red state), PASS after Step 2.

- [ ] **Step 4: Commit**

```bash
git add resources/profiles/models/gemma-4-26b-a4b.json tests/test_model_preset_catalog.cpp
git commit -m "feat: ship gemma-4-26b-a4b — the MoE fix model with the split launch layer"
```

### Task 6: Docs — manuals, memory doc, ADR

**Files:**
- Modify: `docs/15-model-gemma-4.md` + `docs/15-model-gemma-4.ru.md`
- Modify: `docs/18-memory-estimate.md` + `docs/18-memory-estimate.ru.md`
- Modify: `docs/07-glossary.md` + `docs/07-glossary.ru.md`

- [ ] **Step 1: docs/15** — retitle to cover the family ("Gemma 4 E4B / 12B / 26B-A4B QAT (fix)"), add a 26B-A4B block following the 12B pattern: what it is (MoE: 30 blocks, 128 experts × 8 active, encoder-free vision; card: OmniDocB — absent from the card table, say "no card benchmark — added for the owner's quality measurements, see the design doc"), files table, the launch command (the split config: auto/2048/8192/fit-target 512), the request section ("identical to the 12B — same prompts, same budgets, `spec-draft-n-max 2`"), budgets (add row: RAM ≥ 16 GiB · VRAM ≥ 3.9 GiB split / 18.2 GiB full; ubatch 2048 mandatory; image floor 1120), limitations (partial offload is the expected mode on ≤ 8 GiB cards; first request runs at ~half speed until the weights reach the RAM cache; minBuild b12137 — `auto` breaks on older builds). RU mirror likewise.

- [ ] **Step 2: docs/18** — §6 calibration table gets the row:

```
| `gemma-4-26b-a4b` | 5 | 8 × 128 | 616 980 B/token (1120) | 220 216 B/token + 2 B | 251 939 328 / 346 051 051 | 629 145 600 |
```

and the requirements table gets `gemma-4-26b-a4b | q4_k_xl | 13.27 | 1138 | 760 | 462 | 659 | 240 | 362 | 16.81 | **18.21 GiB**` (values in GiB, buffers in MiB — weights 13.27, proj 1113, KV 760, work 462, vision 659, draft 240, draft ctx 362). Add a short §6.1 "Split models": the `cpuOffload` flag, the two floors (`vramFloor` = buffers + margin; `requiredRam` = weights + 2 GiB headroom), the `splitForVramBudget` rule and the 1.0× RAM threshold, the measured anchor (6 GiB card → 5.05 GiB used). RU mirror likewise.

- [ ] **Step 3: ADR 312** in `docs/07-glossary.md` (the models/theme group nearest the model-catalog ADRs — the builder finds it by neighbors): "ADR 312: Gemma 4 26B-A4B ships as a non-default fix model; the memory estimate learns the MoE split (`cpuOffload`: VRAM floor, RAM floor at 1.0× RAM, card layout in the launch tab); the launch layer pins `n-gpu-layers auto`, `ubatch-size 2048` (encoder-free vision aborts on the default), `ctx-size 8192`, `fit-target 512`." Add the mirror to `docs/07-glossary.ru.md`.

- [ ] **Step 4: Verify** — the docs build has no gate; the catalog doc-name test (`shippedProfilesAreTheOnesTheDocsDescribe`) already covers id/roles. Run the full check:

Run: `scripts/check.sh`
Expected: build + ctest + clang-format + qmllint all green.

- [ ] **Step 5: Commit**

```bash
git add docs/15-model-gemma-4.md docs/15-model-gemma-4.ru.md docs/18-memory-estimate.md docs/18-memory-estimate.ru.md docs/07-glossary.md docs/07-glossary.ru.md
git commit -m "docs: the 26B-A4B manual, the split-model memory numbers, ADR 312"
```

### Task 7: Install + smoke on the owner's machine

**Files:**
- Copy (no repo change): `C:\AI\Models\gemma-4-26B-A4B-it-qat-GGUF\{gemma-4-26B-A4B-it-qat-UD-Q4_K_XL.gguf,mmproj-F16.gguf,mtp-gemma-4-26B-A4B-it.gguf}` → `C:\Users\Dmitriy\AppData\Roaming\llocr\LLM OCR\models\unsloth__gemma-4-26B-A4B-it-qat-GGUF\`

- [ ] **Step 1: Copy the three files** (the app's repo-dir layout `ModelInstallTransaction::repoDirName`; skips a 14 GB re-download — the machine has ~17 GB free).

- [ ] **Step 2: Smoke the estimate** — build the app, run it, open Settings → Launch for the fix role, pick the 26B. Expected: the LaunchTab shows the full breakdown sentence plus the partial-offload line with ~1.5 GiB on the card / ~11.8 GiB in RAM / RAM ≥ 16 GiB of 15.8 GiB, and **no red VRAM error**; the models window dropdown row shows `RAM ≥ 16 GiB · VRAM ≥ 3.9 GiB (split) / 18.2 GiB (full)`.

- [ ] **Step 3: Smoke the server** — start the model. Expected: the server loads with `--n-gpu-layers auto --ubatch-size 2048 --ctx-size 8192 --fit-target 512`; a fix request on a block completes (~55 s on the 6 GB card); no GGML assert on the image.

- [ ] **Step 4: Report** — record what the UI showed and the server's actual VRAM (`nvidia-smi`) in the PR description; flag any deviation from the estimate larger than 10 %.

## Self-review notes

- Spec coverage: profile ✓ (Task 5), estimator floors/split ✓ (Task 1), controller keys ✓ (Task 2), quant labels ✓ (Task 3), dropdown + LaunchTab + RU ✓ (Task 4), wizard untouched ✓ (no task — by decision), docs+ADR ✓ (Task 6), install/smoke ✓ (Task 7), measure-script fix ✓ (committed in Task 1).
- Type consistency: `MemorySplit` field names and `splitForVramBudget(estimate, budget)` signature identical in Tasks 1/2/3; quant map keys identical in Tasks 3/4.
- The exact expected byte counts in Task 1 come from the spec's measured values; each has a comment tying it to the formula term.
