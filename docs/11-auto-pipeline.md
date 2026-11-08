# 11. Auto mode: the «markup → recognition → verification → fix → verification» pipeline

> What the Auto pass button in the header's «Target pages» group does, how
> the pipeline is built and why it is split into stages. Decisions —
> ADR 257 (the stages), ADR 291 (the rerun modes).

## 1. What it is

The Auto pass button in the «Target pages» group (ADR 256 — a stub until
then) starts an **auto run**: the full processing cycle over the selected
pages without the operator's help. The scope is the pages selected in the
thumbnail panel; an empty selection means the whole document (the range field
next to the button spells that choice out in words).

The run consists of five stages:

| # | Stage | Task | What it does |
|---|-------|------|--------------|
| 1 | **Markup** | `layout` | Finds the blocks on the page. The blocks have no text yet. |
| 2 | **Block recognition** | `blockRecognition` | Transcribes every block: crop → text. |
| 3 | **Verification** | `decision` | Judges the blocks **enabled in the verification settings** (and the duplicate suspects): does the text match the image. |
| 4 | **Fix** | `fix` | Sends the problem blocks (Mismatch, Review) to a universal model: OK / FIX / REVIEW. |
| 5 | **Verification of the corrected** | `decision` | Judges the **corrected** layer: is the fix right. |

The run goes **stage by stage, not page by page**: first all pages are marked
up, then all recognized, then verified, then fixed, then verified again. The
reason is the cost of a model switch (ADR 252): the managed llama-server
holds one loaded model, and a transition between tasks (`layout` →
`blockRecognition` → `decision` → `fix`) reloads it. A page-by-page run would
give 4 model switches per page instead of 4 per document.

The starting point is the rerun question (ADR 291): when the scope already
carries results, the button offers «Переделать всё» — the full cycle
described here — or «Только где не сделано» — the **continue mode**. In the
continue mode the markup stage filters its scope to the pages without markup
(a fully-marked scope skips the stage with «Нет страниц для разметки»),
while the recognition/verification/fix stages keep the full scope and skip
the done blocks through their collectors.

## 2. The stage boundary

A stage is an object with one contract (`src/app/AutoPipelineStage.h`):

```cpp
class AutoPipelineStage : public QObject
{
    Q_OBJECT
public:
    using Scope = QList<int>;

    AutoPipelineStage(QString title, QObject *parent = nullptr);

    QString title() const;

    // Empty — the stage can run; otherwise — why it is skipped.
    virtual QString skipReason(const Scope &pages) const = 0;
    // Start the work; the end — the finished() signal, an early refusal — failed().
    virtual void start(const Scope &pages) = 0;
    virtual void stop() = 0;
    virtual int progressDone() const { return 0; }
    virtual int progressTotal() const { return 0; }

signals:
    void finished();
    void failed(const StatusMessage &message);
};
```

Three properties of the contract:

* **A stage knows nothing about its neighbours.** Markup does not know about
  verification, verification does not know about the fix. The only link is
  the `finished()` signal.
* **A stage says itself whether it can run.** `skipReason()` returns an empty
  string when the stage is ready, otherwise the reason («no model for this
  task», «nothing to recognize»). The reason goes to the footer: the user
  sees what was skipped and why.
* **A stage knows how to stop.** `stop()` shuts down the current work; the
  pipeline does not go further after that.

The pipeline (`src/app/AutoPipelineController.h`) holds a **list of stages**
and walks it in order:

```cpp
m_autoPipeline->setStages({
    m_autoMarkupStage,        // 1
    m_autoRecognitionStage,   // 2
    m_autoVerificationStage,  // 3
    m_autoFixStage,           // 4
    m_autoRecheckStage,       // 5 — the same AutoVerificationStage, another title
});
```

Adding a stage means adding a class and one line to this list. Neither the
orchestrator, nor the facade, nor QML change.

## 3. The failure and skip semantics

| Case | What happens |
|------|--------------|
| A stage cannot run (no model, nothing to do) | The stage is skipped, the reason goes to the footer, the run continues. |
| The model answered with an error on a separate block | The queue marks the block and moves to the next one — this is how the manual queues already work. The run does not fall. |
| A stage failed as a whole | The reason goes to the footer, the run goes to the next stage. |
| The user pressed stop | The current stage shuts down, the following ones do not start. |

The footer's final line lists what was skipped and why. That is a decision
made for the user: a run that reached the end with two skipped stages is more
useful than a run that stopped at the first «no model».

## 4. What the «verification of the corrected» stage does

It is the **same** verification stage as stage 3 — there is no separate code
for it. The separation happens in block collection: the queue takes the
blocks whose **effective layer** (ADR 164) has no verdict.

* Before the fix the effective layer is the OCR text: when the text has a
  verdict (Ok / Mismatch), the block is already answered and does not take
  part in the fifth stage.
* The fix puts the corrected text into the correction layer and **resets its
  verdict** (`BoxTransitions::fixApplied`): the correction layer becomes the
  effective one, and the block is «without a verdict» again — which is what
  the fifth stage judges.
* A block the fix could not read (REVIEW) gets `correctedCheckStatus =
  Review` — it is answered and waits for the operator; the fifth stage does
  not touch it.

So the fifth stage is exactly the same `checkPagesEnabledBlocks(pages)` call
as the third: the collector finds «the blocks without a verdict on the
effective layer» by itself.

## 5. The interface

**The Auto pass button** (the «Target pages» group of the header):

* is inactive when there is no document, no layout or recognition model
  (`Controller.autoPipelineAvailable`), or another job is running
  (`pipelineIdle`);
* turns into a stop button during the run — the same trick the Mark up,
  Verify and Fix buttons use;
* asks before starting when the scope carries results (ADR 291):
  «Переделать всё» restarts the markup from scratch — the blocks and the
  texts of the pages are replaced; «Только где не сделано» marks up only
  the pages without markup and fills in what is missing.

**The status line in the footer** (`TaskStatus.qml`) shows the current stage
and its progress: `Auto 3/5: Verification 12 / 40`. An empty progress
(markup's is per page, verification's is per item) is shown without numbers.
Between the stages the title is empty — the line shows only `Auto 3/5`.

## 6. The place for future models

The requirement: «different blocks recognized by different models» must
become possible without rewriting the pipeline.

The runtime constraint: **one task — one model**. `ConnectionRole`
(`core/ConnectionRole.h`) is a model slot, and switching a role reloads the
server. So «different blocks — different models» in the app's terms is
**different tasks**, not different models inside one task.

What is already ready for it:

1. **A stage is the unit of work.** An additional task = an additional stage
   in the list. The orchestrator, the facade and QML do not change.
2. **Block collection is separated from execution.** `BlockWorkList`
   collects the lists (`unrecognizedBlocks`, `enabledBlocks`,
   `fixableBlocks`) and the role queues run them (`RecheckQueue::run`,
   `DecisionQueue::run`, `RemedyQueue::runFix` — ADR 286). A stage decides
   itself which blocks go into which run.
3. **The block-type filter.** The collectors take an optional type filter:
   today it is empty (all types), tomorrow a task's stage hands out the
   filter «these types are mine». The filter is a
   `std::function<bool(const QString &blockType)>`.

To add «recognize formulas with model A, text with model B» one needs to:
add a new task to `core/ConnectionRole.h` and to the model profiles, add a
«block type → task» table to the settings, and add a stage that collects its
blocks with the filter and runs the queue twice — once per task. No existing
line of the pipeline changes.

## 7. The cost of a run

* **Model switches.** The managed server reloads the model at the task
  boundary: `layout` → `blockRecognition` → `decision` → `fix` → `decision`.
  That is 4–5 reloads per run. The external runtime (ADR 226) reloads
  nothing: every task has its own address there.
* **External runtime.** Every task has its own endpoint, key and model alias
  (ADR 226) — the run goes through the same stages, switching between
  addresses.
* **Time.** Stages 3 and 5 judge every block with a separate request; on a
  heavy document that is the longest part.

## 8. Verification

| Test | What it checks |
|------|----------------|
| `tests/test_auto_pipeline.cpp` | The order of the stages; skipping a stage with a reason; continuing after a failure; stopping; the skip report. |
| `tests/test_verification_queue.cpp` | Block collection: «without text», «by the types from the settings», «waiting for the fix», «the correction layer without a verdict»; the type filter. |
| `tests/test_app_import.cpp` | The facade's run: the button is available, the auto run starts and stops. |

The full check is `scripts/check.sh` (build + ctest + clang-format + qmllint).

## 9. Decisions

1. **The run goes stage by stage, not page by page** — the cost of a model
   switch.
2. **A stage's failure does not drop the run** — the reasons are collected
   and shown in the footer.
3. **No model — the stage is skipped**, not blocking the start.
4. **Markup from scratch is the redo mode** (ADR 291): a full cycle erases
   the results; the continue mode marks up only the unmarked pages and the
   stages skip done blocks.
5. **The fifth stage is the same verification stage**: the collector itself
   finds the blocks without a verdict on the effective layer.
6. **The modularity boundary is the stage contract**, not a set of buttons.

## 10. What was left out on purpose

Postponed deliberately (the next step, not a defect):

* **Different models for different block types.** The seam is ready
  (collection separated from execution in `BlockWorkList`, the type filter),
  but a new task still needs its own queue: a new task in
  `core/ConnectionRole.h` + a new queue + a new stage.
* **Duplication in the queue stages.** The three queue stages repeat the
  connect/stop/progress tail of each other: a private base would remove
  that. (The collectors themselves folded into `BlockWorkList`, and the
  double collection of `skipReason`/`start` is gone — the reason's list is
  cached, ADR 286.)
* **Export is not closed by the pipeline.** The export button does not
  account for the markup stage (manual markup behaves the same), so a
  half-processed snapshot can be exported during a run. Project saving is
  closed.
* **A second markup after stop.** A repeated markup pass (for a turned page)
  starts even after the run was stopped.
* **The location hints in `resources/i18n/llocr_ru.ts`** diverge from
  `lupdate` across the whole file (a long-standing divergence, not a
  consequence of the auto mode); the lookup goes by context and text, and
  `lrelease` builds the file.
* **The run's cost on real models is not measured** — all tests go through
  the mock server, where there is no model reload.

## 11. Links

- The four tasks and the verification pipeline: `docs/09-tasks-and-verification.md`.
- Model manuals: `docs/13-model-mineru.md` (layout, blockRecognition),
  `docs/17-model-minicpm-jev.md` (decision), `docs/16-model-qwen3.5.md` (fix).
- Glossary: `docs/07-glossary.md` (ADR 257 — the auto mode, ADR 291 — the
  rerun modes).
- Russian mirror: `docs/11-auto-pipeline.ru.md`.
