# Авто-режим: конвейер «разметка → распознавание → проверка → правка → проверка» — план реализации

> **Для агентных исполнителей:** обязательный под-скилл: `superpowers:subagent-driven-development` (рекомендуется) или `superpowers:executing-plans`. Шаги отмечены чекбоксами (`- [ ]`).

**Цель:** кнопка «Распознать N стр.» в блоке «Авто» главного меню запускает полный авто-прогон по выбранным страницам: разметка → распознавание блоков → проверка → исправление → проверка исправленных.

**Архитектура:** конвейер — список этапов (`AutoPipelineController`), каждый этап — объект с одним контрактом (`AutoPipelineStage`: `skipReason` / `start` / `stop` / `finished` / `failed`). Механику этапов дают уже существующие `LayoutController` и `VerificationQueueController`; фасад (`AppController`) отдаёт конвейеру разметку и список страниц. Добавление этапа = один класс + одна строка в списке.

**Стек:** Qt 6 / C++17 / QML, CMake, QtTest. Слой `llocr_app` (`src/app/`), тесты в `tests/`.

**Проектные решения:** `docs/33-auto-pipeline.md` (ADR 257).

## Ограничения проекта

- Слой `llocr_app` не ходит в сеть и не трогает файлы сам; ключи — только через `SettingsStore`.
- Секреты — в системном хранилище (ADR 227); в этот план они не входят.
- Строки UI — через `Theme.*`; шрифты в QML не задаются вручную.
- Комментарии — только когда из кода не следует «почему»; ADR-ссылки приветствуются.
- Переводы: русские строки дописываются в `resources/i18n/llocr_ru.ts` вручную (контекст = имя класса).
- Проверка: `scripts/check.sh` (сборка → ctest → clang-format → qmllint). Готовый каталог сборки — `build/` (Unix Makefiles, Qt `~/Qt/6.10.3/macos`), **не** переконфигурировать с нуля.
- Формат: `clang-format` (файл `.clang-format` в корне), 4 пробела, строки ≤ 120.
- Каждая задача заканчивается коммитом; сообщения — по образцу журнала: `feat: …`, `fix: …`, `docs: …`.

---

### Задача 1: Контракт этапа и конвейер

**Файлы:**
- Создать: `src/app/AutoPipelineStage.h`
- Создать: `src/app/AutoPipelineController.h`
- Создать: `src/app/AutoPipelineController.cpp`
- Создать: `tests/test_auto_pipeline.cpp`
- Изменить: `src/CMakeLists.txt` (цель `llocr_app`)
- Изменить: `tests/CMakeLists.txt`

**Интерфейсы:**
- Производит: `llocr::AutoPipelineStage` (контракт), `llocr::AutoPipelineController` (оркестратор) — их используют задачи 3 и 4.
- Потребляет: ничего, кроме `core/StatusMessage.h`.

**Шаг 1: написать падающий тест**

`tests/test_auto_pipeline.cpp` — тест на фейковых этапах (без Qt-модели документа):

```cpp
#include <QSignalSpy>
#include <QtTest>

#include "app/AutoPipelineController.h"

using namespace llocr;

namespace {

// A stage that records the order and ends when the test tells it to.
class FakeStage : public AutoPipelineStage
{
    Q_OBJECT
public:
    FakeStage(QString id, QString title, QStringList *log)
        : AutoPipelineStage(id, title), m_log(log)
    {
    }

    QString skipReason(const Scope &) const override { return m_skipReason; }
    void start(const Scope &) override
    {
        m_started = true;
        m_log->append(id());
        if (m_endImmediately)
            emit finished();
    }
    void stop() override
    {
        m_stopped = true;
        emit finished();
    }
    int progressDone() const override { return m_done; }
    int progressTotal() const override { return m_total; }

    void setSkipReason(const QString &reason) { m_skipReason = reason; }
    void setEndImmediately(bool immediate) { m_endImmediately = immediate; }
    void setProgress(int done, int total) { m_done = done; m_total = total; }
    void fail(const StatusMessage &message) { emit failed(message); }
    bool started() const { return m_started; }
    bool stopped() const { return m_stopped; }

private:
    QStringList *m_log;
    QString m_skipReason;
    bool m_endImmediately = true;
    bool m_started = false;
    bool m_stopped = false;
    int m_done = 0;
    int m_total = 0;
};

}  // namespace

class TestAutoPipeline : public QObject
{
    Q_OBJECT

private slots:
    void theStagesRunInOrder();
    void aSkippedStageIsPassedOver();
    void aFailedStageDoesNotStopTheRun();
    void theRunStopsOnRequest();
    void anEmptyScopeIsRefused();
    void theProgressComesFromTheRunningStage();
};
```

Тело тестов:

```cpp
void TestAutoPipeline::theStagesRunInOrder()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0, 1}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().first().toBool(), true);
}

void TestAutoPipeline::aSkippedStageIsPassedOver()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    second.setSkipReason("no model");
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy statusSpy(&controller, &AutoPipelineController::statusRequested);

    QVERIFY(controller.start());
    QCOMPARE(log, QStringList({"markup"}));           // the skipped stage never ran
    QVERIFY(statusSpy.count() >= 1);                  // the reason reached the status line
    QVERIFY(statusSpy.first().first().value<QString>().contains("no model"));
}

void TestAutoPipeline::aFailedStageDoesNotStopTheRun()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});
    QSignalSpy finishedSpy(&controller, &AutoPipelineController::finished);

    QVERIFY(controller.start());
    first.fail(StatusMessage::literal("the model refused"));
    // The failed stage reports itself and the pipeline moves on.
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"markup", "recognition"}));
    QCOMPARE(finishedSpy.count(), 1);
}

void TestAutoPipeline::theRunStopsOnRequest()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setEndImmediately(false);
    FakeStage second("recognition", "Recognition", &log);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first, &second});

    QVERIFY(controller.start());
    controller.stop();
    QVERIFY(first.stopped());
    QCoreApplication::processEvents();
    QCOMPARE(log, QStringList({"markup"}));   // the second stage never started
    QVERIFY(!controller.isRunning());
}

void TestAutoPipeline::anEmptyScopeIsRefused()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    AutoPipelineController controller({[] { return QList<int>{}; }, [] { return true; }});
    controller.setStages({&first});
    QVERIFY(!controller.start());
    QVERIFY(log.isEmpty());
}

void TestAutoPipeline::theProgressComesFromTheRunningStage()
{
    QStringList log;
    FakeStage first("markup", "Markup", &log);
    first.setProgress(2, 5);
    AutoPipelineController controller({[] { return QList<int>{0}; }, [] { return true; }});
    controller.setStages({&first});

    QVERIFY(controller.start());
    QCOMPARE(controller.progressDone(), 2);
    QCOMPARE(controller.progressTotal(), 5);
    QCOMPARE(controller.stageIndex(), 0);
    QCOMPARE(controller.stageCount(), 1);
}
```

**Шаг 2: собрать тест и убедиться, что он падает**

```sh
cd /Users/gladskikh/Git/LLocr
cmake --build build -j 8 --target test_auto_pipeline
```

Ожидаемо: сборка падает — `app/AutoPipelineStage.h` не существует.

**Шаг 3: контракт этапа**

`src/app/AutoPipelineStage.h`:

```cpp
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "core/StatusMessage.h"

namespace llocr {

// One step of the auto pipeline (ADR 257). The pipeline runs the stages in
// order and moves on when a stage reports that it is done; a stage that
// cannot run says why, and the pipeline skips it — a missing model or a
// failed stage does not stop the run.
class AutoPipelineStage : public QObject
{
    Q_OBJECT

public:
    // The pages the run works on: the thumbnail selection at the moment the
    // run started, or every page when the selection is empty.
    using Scope = QList<int>;

    AutoPipelineStage(QString id, QString title, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }

    // Empty when the stage can run; otherwise why it is skipped — no model
    // for the task, nothing to recognize. The reason reaches the status line.
    virtual QString skipReason(const Scope &pages) const = 0;

    // Starts the work over the pages. The stage reports the end of the work
    // through finished() and an early failure through failed(); after failed()
    // the stage must not emit finished().
    virtual void start(const Scope &pages) = 0;
    virtual void stop() = 0;

    virtual int progressDone() const { return 0; }
    virtual int progressTotal() const { return 0; }

signals:
    void finished();
    void failed(const StatusMessage &message);

private:
    QString m_id;
    QString m_title;
};

}  // namespace llocr
```

**Шаг 4: оркестратор**

`src/app/AutoPipelineController.h`:

```cpp
#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "app/AutoPipelineStage.h"
#include "core/StatusMessage.h"

namespace llocr {

// The auto pipeline (ADR 257): a list of stages, run in order over the pages
// the user selected. The controller owns the order and the reporting; every
// stage owns its own work. A stage that cannot run says why and the pipeline
// skips it, so the run ends with the stages that did work rather than with
// the first missing model.
class AutoPipelineController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        // The pages the run works on: the thumbnail selection, empty = all.
        std::function<QList<int>()> scopePages;
        // Whether the document can be worked on now (no other job runs).
        std::function<bool()> idle;
    };

    explicit AutoPipelineController(Deps deps, QObject *parent = nullptr);

    // The stages, in the order they run. The controller does not own them.
    void setStages(QList<AutoPipelineStage *> stages);

    bool isRunning() const { return m_running; }
    bool canStart() const;
    QString blockedReason() const;

    int stageIndex() const { return m_stageIndex; }
    int stageCount() const { return m_stages.size(); }
    QString stageTitle() const;
    int progressDone() const;
    int progressTotal() const;

public slots:
    // Starts the run; returns false when it could not start.
    bool start();
    // Stops the running stage; the stages after it do not start.
    void stop();

signals:
    void stateChanged();
    void statusRequested(const llocr::StatusMessage &message);
    void finished(bool ok);

private:
    void runStage(int index);
    void onStageFinished();
    void onStageFailed(const llocr::StatusMessage &message);
    void finish(bool ok);
    void reportSummary();

    Deps m_deps;
    QList<AutoPipelineStage *> m_stages;
    AutoPipelineStage::Scope m_scope;
    AutoPipelineStage *m_current = nullptr;
    int m_stageIndex = -1;
    bool m_running = false;
    bool m_stopping = false;
    QStringList m_skips;
    QStringList m_failures;
};

}  // namespace llocr
```

`src/app/AutoPipelineController.cpp`:

```cpp
#include "app/AutoPipelineController.h"

#include <QCoreApplication>
#include <QTimer>

namespace llocr {

AutoPipelineController::AutoPipelineController(Deps deps, QObject *parent)
    : QObject(parent)
    , m_deps(std::move(deps))
{
}

void AutoPipelineController::setStages(QList<AutoPipelineStage *> stages)
{
    m_stages = std::move(stages);
    for (AutoPipelineStage *stage : m_stages) {
        connect(stage, &AutoPipelineStage::finished, this, &AutoPipelineController::onStageFinished);
        connect(stage, &AutoPipelineStage::failed, this, &AutoPipelineController::onStageFailed);
    }
}

bool AutoPipelineController::canStart() const
{
    return !m_running && m_deps.idle() && !m_deps.scopePages().isEmpty();
}

QString AutoPipelineController::blockedReason() const
{
    if (m_running)
        return QString();
    if (!m_deps.idle())
        return StatusMessage::translate("AutoPipelineController", "Another job is running").text();
    if (m_deps.scopePages().isEmpty())
        return StatusMessage::translate("AutoPipelineController", "No pages to process").text();
    return QString();
}

QString AutoPipelineController::stageTitle() const
{
    if (!m_current)
        return QString();
    return m_current->title();
}

int AutoPipelineController::progressDone() const
{
    return m_current ? m_current->progressDone() : 0;
}

int AutoPipelineController::progressTotal() const
{
    return m_current ? m_current->progressTotal() : 0;
}

bool AutoPipelineController::start()
{
    if (m_running)
        return false;
    if (!m_deps.idle() || m_deps.scopePages().isEmpty()) {
        emit statusRequested(StatusMessage::translate("AutoPipelineController", "The document is busy"));
        return false;
    }
    m_scope = m_deps.scopePages();
    m_skips.clear();
    m_failures.clear();
    m_stopping = false;
    m_running = true;
    emit stateChanged();
    runStage(0);
    return true;
}

void AutoPipelineController::stop()
{
    if (!m_running)
        return;
    m_stopping = true;
    if (!m_current) {
        finish(false);
        return;
    }
    m_current->stop();
    // A stage that does not report its stop must not leave the run hanging.
    QTimer::singleShot(0, this, [this]() {
        if (m_running && m_stopping)
            finish(false);
    });
}

void AutoPipelineController::runStage(int index)
{
    if (!m_running)
        return;
    if (index >= m_stages.size()) {
        finish(true);
        return;
    }
    AutoPipelineStage *stage = m_stages.at(index);
    m_stageIndex = index;
    m_current = stage;
    emit stateChanged();

    const QString reason = stage->skipReason(m_scope);
    if (!reason.isEmpty()) {
        m_skips.append(QStringLiteral("%1 — %2").arg(stage->title(), reason));
        QTimer::singleShot(0, this, [this, index]() { runStage(index + 1); });
        return;
    }
    stage->start(m_scope);
}

void AutoPipelineController::onStageFinished()
{
    if (!m_running || sender() != m_current)
        return;
    if (m_stopping) {
        finish(false);
        return;
    }
    runStage(m_stageIndex + 1);
}

void AutoPipelineController::onStageFailed(const StatusMessage &message)
{
    if (!m_running || sender() != m_current)
        return;
    m_failures.append(message.text());
    emit statusRequested(message);
    if (m_stopping) {
        finish(false);
        return;
    }
    runStage(m_stageIndex + 1);
}

void AutoPipelineController::finish(bool ok)
{
    m_running = false;
    m_stopping = false;
    m_current = nullptr;
    m_stageIndex = -1;
    emit stateChanged();
    reportSummary();
    emit finished(ok);
}

void AutoPipelineController::reportSummary()
{
    QList<StatusMessage> parts;
    for (const QString &skip : m_skips)
        parts.append(StatusMessage::literal(skip));
    for (const QString &failure : m_failures)
        parts.append(StatusMessage::literal(failure));
    if (parts.isEmpty())
        return;
    emit statusRequested(StatusMessage::join(parts, QStringLiteral("; ")));
}

}  // namespace llocr
```

**Шаг 5: собрать и прогнать тест**

```sh
cmake --build build -j 8 --target test_auto_pipeline
ctest --test-dir build -R test_auto_pipeline --output-on-failure
```

Ожидаемо: PASS (6 тестов).

**Шаг 6: подключить к сборке — УЖЕ СДЕЛАНО**

Записи в `src/CMakeLists.txt` (цель `llocr_app`) и в `tests/CMakeLists.txt`
добавлены контроллером до запуска задачи, чтобы два исполнителя не правили один
файл. Проверь, что они на месте, и **не добавляй их повторно**:

```cmake
# src/CMakeLists.txt, внутри qt_add_library(llocr_app STATIC ...)
    app/AutoPipelineStage.h
    app/AutoPipelineController.h
    app/AutoPipelineController.cpp

# tests/CMakeLists.txt
qt_add_executable(test_auto_pipeline test_auto_pipeline.cpp)

target_link_libraries(test_auto_pipeline PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_auto_pipeline COMMAND test_auto_pipeline)
```

Требование к тесту: `QTEST_MAIN(TestAutoPipeline)` + `#include "test_auto_pipeline.moc"`, headless-платформа — как в соседних тестах (`tests/CMakeLists.txt` уже задаёт `TIMEOUT` и `QT_QPA_PLATFORM=offscreen` для всего каталога).

**Шаг 7: коммит**

```sh
git add src/app/AutoPipelineStage.h src/app/AutoPipelineController.h src/app/AutoPipelineController.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt tests/test_auto_pipeline.cpp
git commit -m "feat: the auto pipeline — a stage contract and an orchestrator"
```

---

### Задача 2: Шов очереди — сбор блоков отдельно от их выполнения

**Файлы:**
- Изменить: `src/app/VerificationQueueController.h`
- Изменить: `src/app/VerificationQueueController.cpp`
- Создать: `tests/test_verification_queue.cpp`
- Изменить: `tests/CMakeLists.txt` — **запись уже добавлена контроллером** (проверь, не дублируй):

```cmake
qt_add_executable(test_verification_queue test_verification_queue.cpp)

target_link_libraries(test_verification_queue PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_verification_queue COMMAND test_verification_queue)
```

**Интерфейсы:**
- Производит (для задачи 3):
  - `struct VerificationQueueController::Task { int page; int box; };` — **переносится из private в public** (бывший `VerifyTask`).
  - `using TypeFilter = std::function<bool(const QString &blockType)>;`
  - `QList<Task> unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;`
  - `QList<Task> enabledBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;`
  - `QList<Task> fixableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;`
  - `public slots: void recognizeBlocks(const QList<Task> &tasks); void checkBlocks(const QList<Task> &tasks); void fixBlocks(const QList<Task> &tasks);`
  - `signals: void queueFinished();` — очередь опустела.
- Потребляет: ничего из задачи 1.

**Шаг 1: написать падающий тест**

`tests/test_verification_queue.cpp`. Сборщики обращаются к документу и к
хранилилю типов проверки — рантайм им не нужен, но очередь его требует в
конструкторе, поэтому тест поднимает настоящий `VerificationQueueController`
с минимальными зависимостями.

Подготовка документа — по образцу `tests/test_document_model.cpp`: временный
PNG, `appendImage`, затем страница правится через неконстантную ссылку
`model.page(i)`:

```cpp
#include <QtTest>

#include "app/DocumentModel.h"
#include "app/VerificationPromptStore.h"
#include "app/VerificationQueueController.h"
#include "config/LaunchProfileStore.h"
#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A block of the given type and text.
BoundingBox textBox(const QString &label, const QString &text)
{
    BoundingBox box;
    box.label = label;
    box.text = text;
    return box;
}

}  // namespace

class TestVerificationQueue : public QObject
{
    Q_OBJECT

private slots:
    void theBlocksWithoutTextAreTheRecognitionWork();
    void onlyTheEnabledTypesAreChecked();
    void theFixWorkIsTheBlocksThatWaitForIt();
    void theCorrectedLayerWithoutAVerdictIsTheSecondCheck();
    void theTypeFilterNarrowsTheCollection();
};
```

> `DocumentModel` не копируется (`Q_DISABLE_COPY_MOVE`): страницу с блоками
> готовь на месте — `document.page(0).recognized = true;` и
> `document.page(0).result.pages = {page};`.

Тело теста — на настоящей очереди:

```cpp
void TestVerificationQueue::theBlocksWithoutTextAreTheRecognitionWork()
{
    TestSettingsIsolation isolation;   // первым членом теста — см. tests/testsettings.h
    SettingsStore settings;
    LaunchProfileStore launchProfiles(settings);
    RequestProfileStore checkProfiles(settings);
    RequestProfileStore fixProfiles(settings);
    RuntimeController runtime(settings, launchProfiles);
    VerificationPromptStore verification;

    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::white);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("page.png"));
    QVERIFY(source.save(path, "PNG"));

    DocumentModel document;
    QVERIFY(document.appendImage(path));
    document.page(0).recognized = true;
    OcrPage page;
    page.boxes = {textBox(QStringLiteral("text"), QString()),
                  textBox(QStringLiteral("title"), QStringLiteral("already read"))};
    document.page(0).result.pages = {page};

    VerificationQueueController::Deps deps;
    deps.document = document;
    deps.verification = verification;
    deps.checkRequestProfiles = checkProfiles;
    deps.fixRequestProfiles = fixProfiles;
    deps.settings = settings;
    deps.runtime = runtime;
    deps.cropProvider = [](int, int) { return QImage(); };
    deps.layoutBusy = [] { return false; };
    deps.displayTextProvider = [](int, int) { return QString(); };
    VerificationQueueController queue(deps);

    const auto tasks = queue.unrecognizedBlocks({0});
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks.first().box, 0);   // only the block without text
}
```

Остальные тесты — на том же каркасе, с разными блоками:

- `onlyTheEnabledTypesAreChecked`: тип «text» выключен в
  `VerificationPromptStore` (через `setEnabled(row, false)`), «title» включён;
  блок «text» без текста в проверку не попадает, «title» — попадает.
- `theFixWorkIsTheBlocksThatWaitForIt`: Mismatch и Review ждут правки, Ok —
  нет.
- `theCorrectedLayerWithoutAVerdictIsTheSecondCheck`: блок с `correctedText` и
  `correctedCheckStatus == NotChecked` — действующий слой коррекции, вердикта
  нет → он в работе второй проверки; блок, которому правка отказала
  (`correctedCheckStatus == Review`), отвечен и в работе не числится.
- `theTypeFilterNarrowsTheCollection`: фильтр оставляет только блоки своего
  типа.

**Шаг 2: убедиться, что тест падает**

```sh
cmake --build build -j 8 --target test_verification_queue
```

Ожидаемо: нет `unrecognizedBlocks` / `enabledBlocks` / `fixableBlocks`.

**Шаг 3: открыть сборщики**

`src/app/VerificationQueueController.h`:

1. `struct VerifyTask` переносится в public и переименовывается в `Task`:

```cpp
public:
    // One block of the work list: the page and the box on it.
    struct Task {
        int page;
        int box;
    };

    // Which block types a stage handles. Empty — every type. A stage that
    // reads only some types (a task assigned to them) narrows the
    // collection with it (ADR 257).
    using TypeFilter = std::function<bool(const QString &blockType)>;
```

2. В public-секцию рядом с `pageBlocksRecognitionSupported` добавить:

```cpp
    // The work lists, collected over an explicit page set: the blocks the
    // recognition pass reads (no text yet), the blocks the check judges
    // (the enabled types and the duplicate suspects) and the blocks that
    // wait for the fixer.
    QList<Task> unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> enabledBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
    QList<Task> fixableBlocks(const QList<int> &pages, const TypeFilter &filter = {}) const;
```

3. В `public slots` добавить:

```cpp
    // The queues over an explicit work list — the stages of the auto
    // pipeline collect the blocks themselves (ADR 257).
    void recognizeBlocks(const QList<Task> &tasks);
    void checkBlocks(const QList<Task> &tasks);
    void fixBlocks(const QList<Task> &tasks);
```

4. В `signals` добавить:

```cpp
    // The queue ran out of work. The auto pipeline waits for it to move on
    // to the next stage (ADR 257).
    void queueFinished();
```

**Шаг 4: реализовать сборщики и переименовать `VerifyTask` → `Task`**

`src/app/VerificationQueueController.cpp`:

- Все `VerifyTask` → `Task` (включая приватные сигнатуры `collectEnabledBoxes`, `collectProblemBoxes`, `collectFixBoxes`, `collectUnrecognizedBoxes`).
- Существующие коллекторы получают фильтр типов:

```cpp
void VerificationQueueController::collectEnabledBoxes(int pageIndex, QList<int> &out, int &answered, int &rejected, const TypeFilter &filter) const
```

Фильтр применяется к `box.label`: `if (filter && !filter(box.label)) continue;`.

- Новые публичные сборщики:

```cpp
QList<VerificationQueueController::Task> VerificationQueueController::unrecognizedBlocks(const QList<int> &pages, const TypeFilter &filter) const
{
    QList<Task> tasks;
    for (const int page : pages) {
        if (!m_deps.document.isValidIndex(page))
            continue;
        collectUnrecognizedBoxes(page, tasks, filter);
    }
    return tasks;
}
```

`enabledBlocks` собирает через `collectEnabledBoxes` (тот, что считает `answered`/`rejected`), `fixableBlocks` — через `collectFixBoxes`.

- `collectUnrecognizedBoxes` и `collectFixBoxes` принимают фильтр.

**Шаг 5: публичные слоты запуска очередей**

```cpp
void VerificationQueueController::recognizeBlocks(const QList<Task> &tasks)
{
    startRecheckQueue(tasks);
}

void VerificationQueueController::checkBlocks(const QList<Task> &tasks)
{
    startVerifyQueue(tasks);
}

void VerificationQueueController::fixBlocks(const QList<Task> &tasks)
{
    startFixQueue(tasks);
}
```

**Шаг 6: сигнал `queueFinished`**

В `finishVerifyQueue()` (конец очереди) добавить `emit queueFinished();` — рядом с `emit stateChanged();`.

**Шаг 7: существующие методы не меняют поведения**

`checkPagesEnabledBlocks`, `recheckPagesBlocks`, `fixPagesProblemBlocks` и их одностраничные/общие варианты остаются как были — они собирают блоки и запускают очередь. Убедись, что ни один путь не изменился: тесты `test_app_import`, `test_decision_model`, `test_fix_protocol` должны остаться зелёными.

**Шаг 8: собрать и прогнать**

```sh
cmake --build build -j 8 --target test_verification_queue
ctest --test-dir build -R "test_verification_queue|test_app_import|test_decision_model|test_fix_protocol" --output-on-failure
```

**Шаг 9: коммит**

```sh
git add src/app/VerificationQueueController.h src/app/VerificationQueueController.cpp \
        tests/CMakeLists.txt tests/test_verification_queue.cpp
git commit -m "feat: the verification queue — the work list is collected apart from its run"
```

---

### Задача 3: Пять этапов

**Файлы:**
- Создать: `src/app/AutoPipelineStages.h`
- Создать: `src/app/AutoPipelineStages.cpp`
- Изменить: `src/CMakeLists.txt`

**Интерфейсы:**
- Потребляет: `AutoPipelineStage` (задача 1), `VerificationQueueController::Task` и публичные сборщики/слоты (задача 2), `LayoutController`.
- Производит (для задачи 4): `AutoMarkupStage`, `AutoRecognitionStage`, `AutoVerificationStage`, `AutoFixStage`.

**Шаг 1: объявить классы**

`src/app/AutoPipelineStages.h`:

```cpp
#pragma once

#include <functional>

#include <QString>

#include "app/AutoPipelineStage.h"
#include "app/VerificationQueueController.h"

namespace llocr {

class LayoutController;

// The markup stage: the layout pass over the scope. The prompt, the parser
// and the orientation check live in the facade, so the stage asks for the
// pass and is told when the whole pass is over — including the second pass
// of a page the markup turned (ADR 246).
class AutoMarkupStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Runs the markup pass over the pages and calls done(ok) when the pass
    // is over.
    using RunPass = std::function<void(const AutoPipelineStage::Scope &, const std::function<void(bool)> &)>;

    explicit AutoMarkupStage(LayoutController &layout, RunPass runPass, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    LayoutController &m_layout;
    RunPass m_runPass;
    bool m_running = false;
};

// The recognition stage: every block of the scope that has no text yet.
class AutoRecognitionStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    explicit AutoRecognitionStage(VerificationQueueController &queue, ModelAvailable modelAvailable, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    bool m_running = false;
};

// The verification stage: the blocks of the enabled types that have no
// verdict on their effective layer (ADR 164). The same stage serves the
// second check — after the fix, the correction layer is the effective one
// and its blocks have no verdict (ADR 257).
class AutoVerificationStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    // Whether the model for this task is configured — the stage skips
    // itself when it is not.
    using ModelAvailable = std::function<bool()>;

    AutoVerificationStage(VerificationQueueController &queue, ModelAvailable modelAvailable,
                           QString titleKey, QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    ModelAvailable m_modelAvailable;
    bool m_running = false;
};

// The fix stage: the blocks that wait for the fixer (ADR 188).
class AutoFixStage : public AutoPipelineStage
{
    Q_OBJECT

public:
    AutoFixStage(VerificationQueueController &queue, AutoVerificationStage::ModelAvailable modelAvailable,
                 QObject *parent = nullptr);

    QString skipReason(const AutoPipelineStage::Scope &pages) const override;
    void start(const AutoPipelineStage::Scope &pages) override;
    void stop() override;
    int progressDone() const override;
    int progressTotal() const override;

private:
    VerificationQueueController &m_queue;
    AutoVerificationStage::ModelAvailable m_modelAvailable;
    bool m_running = false;
};

}  // namespace llocr
```

**Шаг 2: реализовать этапы**

`src/app/AutoPipelineStages.cpp` — общие правила:

- Заголовки этапов — ключи перевода, а не готовые строки: `title()` возвращает
  `StatusMessage::translate("AutoPipelineStage", m_titleKey).text()`.
  Ключи: `"Markup"`, `"Block recognition"`, `"Verification"`,
  `"Verification of the corrected blocks"`, `"Fix"`.
- `skipReason()`:
  - нет модели → `StatusMessage::translate("AutoPipelineStage", "No model for this task").text()`;
  - работа пуста → `StatusMessage::translate("AutoPipelineStage", "Nothing to <verb>").text()`
    («Nothing to recognize» / «Nothing to check» / «Nothing to fix»);
  - иначе — пустая строка.
- `start()`:
  - собрать список блоков через публичные сборщики очереди;
  - если список пуст — `emit finished()` сразу (работа не началась);
  - иначе запустить очередь (`m_queue.recognizeBlocks(tasks)` /
    `checkBlocks` / `fixBlocks`) и ждать `queueFinished`.
  - После запуска `if (!m_queue.queueActive())` — очередь не взялась
    (занята или список пуст) → `emit finished()`.
- `stop()`: `m_queue.stop()` и `emit finished()`.
- Ожидание конца очереди: в конструкторе `connect(&m_queue, &VerificationQueueController::queueFinished, this, ...)`;
  в обработчике — если этап не запущен (`!m_running`), игнорировать; иначе
  `m_running = false; emit finished();`.

Пример (этап проверки):

```cpp
QString AutoVerificationStage::skipReason(const AutoPipelineStage::Scope &pages) const
{
    if (m_modelAvailable && !m_modelAvailable())
        return StatusMessage::translate("AutoPipelineStage", "No model for this task").text();
    if (m_queue.enabledBlocks(pages).isEmpty())
        return StatusMessage::translate("AutoPipelineStage", "Nothing to check").text();
    return QString();
}

void AutoVerificationStage::start(const AutoPipelineStage::Scope &pages)
{
    m_running = true;
    m_queue.checkBlocks(m_queue.enabledBlocks(pages));
    if (!m_queue.queueActive()) {
        m_running = false;
        emit finished();
    }
}
```

**Шаг 3: подключить к сборке**

`src/CMakeLists.txt` — в `llocr_app`:

```
    app/AutoPipelineStages.h
    app/AutoPipelineStages.cpp
```

**Шаг 4: собрать**

```sh
cmake --build build -j 8
```

Ожидаемо: сборка чистая (этапы пока никто не создаёт — они только компилируются).

**Шаг 5: коммит**

```sh
git add src/app/AutoPipelineStages.h src/app/AutoPipelineStages.cpp src/CMakeLists.txt
git commit -m "feat: the auto pipeline stages — markup, recognition, verification, fix"
```

---

### Задача 4: Фасад — запуск прогона из приложения

**Файлы:**
- Изменить: `src/app/AppController.h`
- Изменить: `src/app/AppController.cpp`
- Изменить: `src/app/AvailabilityPresenter.h`
- Изменить: `src/app/AvailabilityPresenter.cpp`

**Интерфейсы:**
- Потребляет: `AutoPipelineController` и четыре этапа (задачи 1 и 3), `LayoutController`, `VerificationQueueController`.
- Производит (для задачи 5, QML): свойства
  `autoPipelineRunning`, `autoPipelineAvailable`, `autoPipelineStageIndex`,
  `autoPipelineStageCount`, `autoPipelineStageTitle`,
  `autoPipelineProgressDone`, `autoPipelineProgressTotal`;
  слоты `startAutoPipeline()`, `stopAutoPipeline()`.

**Шаг 1: объявить свойства и слоты**

`src/app/AppController.h` — рядом с прочими `Q_PROPERTY`:

```cpp
    // The auto pipeline (ADR 257).
    Q_PROPERTY(bool autoPipelineRunning READ autoPipelineRunning NOTIFY autoStateChanged)
    Q_PROPERTY(bool autoPipelineAvailable READ autoPipelineAvailable NOTIFY autoStateChanged)
    Q_PROPERTY(int autoPipelineStageIndex READ autoPipelineStageIndex NOTIFY autoStateChanged)
    Q_PROPERTY(int autoPipelineStageCount READ autoPipelineStageCount NOTIFY autoStateChanged)
    Q_PROPERTY(QString autoPipelineStageTitle READ autoPipelineStageTitle NOTIFY autoStateChanged)
    Q_PROPERTY(int autoPipelineProgressDone READ autoPipelineProgressDone NOTIFY autoStateChanged)
    Q_PROPERTY(int autoPipelineProgressTotal READ autoPipelineProgressTotal NOTIFY autoStateChanged)
```

Слоты:

```cpp
    Q_INVOKABLE void startAutoPipeline();
    Q_INVOKABLE void stopAutoPipeline();
```

Сигнал: `void autoStateChanged();`

Геттеры — тонкие обёртки над контроллером:

```cpp
bool AppController::autoPipelineRunning() const { return m_autoPipeline.isRunning(); }
bool AppController::autoPipelineAvailable() const { return m_autoPipeline.canStart(); }
int AppController::autoPipelineStageIndex() const { return m_autoPipeline.stageIndex(); }
int AppController::autoPipelineStageCount() const { return m_autoPipeline.stageCount(); }
QString AppController::autoPipelineStageTitle() const { return m_autoPipeline.stageTitle(); }
int AppController::autoPipelineProgressDone() const { return m_autoPipeline.progressDone(); }
int AppController::autoPipelineProgressTotal() const { return m_autoPipeline.progressTotal(); }
```

**Шаг 2: собрать конвейер**

В конструкторе `AppController` после создания `m_verify` и `m_layout`:

```cpp
    m_autoMarkupStage = new AutoMarkupStage(m_layout,
        [this](const AutoPipelineStage::Scope &pages, const std::function<void(bool)> &done) {
            runMarkupPass(pages, done);
        }, this);
    m_autoRecognitionStage = new AutoRecognitionStage(m_verify,
        [this]() { return m_availability.blockRecognitionModelAvailable(); }, this);
    m_autoVerificationStage = new AutoVerificationStage(m_verify,
        [this]() { return m_availability.decisionModelAvailable(); },
        QStringLiteral("Verification"), this);
    m_autoRecheckStage = new AutoVerificationStage(m_verify,
        [this]() { return m_availability.decisionModelAvailable(); },
        QStringLiteral("Verification of the corrected blocks"), this);
    m_autoFixStage = new AutoFixStage(m_verify,
        [this]() { return m_availability.fixModelAvailable(); }, this);

    AutoPipelineController::Deps deps;
    deps.scopePages = [this]() { return scopePages(); };
    deps.idle = [this]() { return m_availability.actionsIdle(); };
    m_autoPipeline = new AutoPipelineController(deps, this);
    m_autoPipeline->setStages({m_autoMarkupStage, m_autoRecognitionStage, m_autoVerificationStage,
                               m_autoFixStage, m_autoRecheckStage});
    connect(m_autoPipeline, &AutoPipelineController::stateChanged, this, &AppController::autoStateChanged);
    connect(m_autoPipeline, &AutoPipelineController::statusRequested, this, &AppController::statusRequested);
```

Имена полей `decisionModelAvailable()` / `fixModelAvailable()` — **сверься** с
`AvailabilityPresenter.h`: используй те реальные имена, что там есть (или
добавь недостающие). Модель блоков (`blockRecognition`) проверяется через
`m_layout.available()` и наличие модели распознавания в профиле.

**Шаг 3: разметка по просьбе конвейера**

`src/app/AppController.h` (private):

```cpp
    // The markup pass over the pages; done(ok) when the whole pass is over.
    void runMarkupPass(const QList<int> &pages, const std::function<void(bool)> &done);
    void finishMarkupPass(bool ok);
```

`src/app/AppController.cpp`:

```cpp
void AppController::runMarkupPass(const QList<int> &pages, const std::function<void(bool)> &done)
{
    m_markupPassDone = done;
    startLayout(pages);
}

void AppController::finishMarkupPass(bool ok)
{
    if (!m_markupPassDone)
        return;
    const auto done = m_markupPassDone;
    m_markupPassDone = nullptr;
    done(ok);
}
```

`startLayout(pages)` сейчас возвращает `void` и уходит молча, когда:
занят (`m_layout.busy()`), идёт импорт, идёт проверка, нет роли «layout» или
нет промпта. **Каждый из этих путей обязан звать `finishMarkupPass(false)`** —
иначе конвейер будет ждать разметку вечно.

Разбор «страница повернулась» (ADR 246): разметка может пройти в два прохода.
Конец пайплайна разметки — это момент, когда разметка не занята **и** список
`m_remarkupPages` пуст. В обработчике `LayoutController::busyChanged` (он уже
есть в `AppController.cpp`):

```cpp
    connect(&m_layout, &LayoutController::busyChanged, this, [this]() {
        if (m_layout.busy())
            return;
        if (!m_remarkupPages.isEmpty()) {
            QList<int> pages = m_remarkupPages;
            m_remarkupPages.clear();
            startLayout(pages);
            return;
        }
        finishMarkupPass(true);
    });
```

**Шаг 4: слоты запуска**

```cpp
void AppController::startAutoPipeline()
{
    m_autoPipeline.start();
}

void AppController::stopAutoPipeline()
{
    m_autoPipeline.stop();
}
```

**Шаг 5: прогон занимает документ**

`AvailabilityPresenter` получает ещё один вход — «идёт авто-прогон» — и
учитывает его в `actionsIdle()`: пока идёт прогон, кнопки разметки, проверки и
правки неактивны (иначе две работы писали бы в один документ).

В `AvailabilityPresenter::Deps` добавить поле:

```cpp
    // The auto pipeline runs (ADR 257): the document is busy until it ends.
    std::function<bool()> autoPipelineBusy;
```

и в `actionsIdle()`:

```cpp
    if (m_deps.autoPipelineBusy && m_deps.autoPipelineBusy())
        return false;
```

В `AppController` в `Deps` преседентера:

```cpp
        autoPipelineBusy = [this]() { return m_autoPipeline.isRunning(); },
```

**Шаг 6: перевод при смене языка**

`AppController::retranslate()` должен заново отдать заголовок этапа:
`emit autoStateChanged();` — заголовки этапов переводятся ключами.

**Шаг 7: собрать и прогнать**

```sh
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

**Шаг 8: коммит**

```sh
git add src/app/AppController.h src/app/AppController.cpp src/app/AvailabilityPresenter.h src/app/AvailabilityPresenter.cpp
git commit -m "feat: the facade runs the auto pipeline"
```

---

### Задача 5: Кнопка «Авто» и строка состояния

**Файлы:**
- Изменить: `src/qml/MainWindow/Header.qml`
- Изменить: `src/qml/TaskStatus.qml`
- Изменить: `resources/i18n/llocr_ru.ts`

**Интерфейсы:**
- Потребляет: свойства и слоты задачи 4.

**Шаг 1: кнопка**

`src/qml/MainWindow/Header.qml` — в группе «Auto» вместо заглушки:

```qml
        IconToolButton {
            text: qsTr("Recognize %1 pg.").arg(headerRoot.scopePageCount)
            iconName: "zap"
            Layout.fillWidth: true
            stopMode: Controller.autoPipelineRunning
            enabled: stopMode || (headerRoot.pipelineIdle && Controller.hasImage && Controller.autoPipelineAvailable)
            ToolTip.visible: hovered
            ToolTip.text: stopMode
                          ? qsTr("Stop the automatic pipeline")
                          : (Controller.autoPipelineAvailable
                             ? qsTr("Mark up, recognize, verify and fix the selected pages")
                             : qsTr("The automatic pipeline needs a markup and a recognition model"))
            onClicked: {
                if (stopMode) {
                    Controller.stopAutoPipeline()
                    return
                }
                autoConfirmDialog.open()
            }
        }
```

`headerRoot.pipelineIdle` — существующее свойство заголовка («нет другой
работы»); если его нет, используй `Controller.actionsIdle`.

**Шаг 2: подтверждение**

Рядом с `markupConfirmDialog` (тот же `InterruptConfirmDialog`):

```qml
    InterruptConfirmDialog {
        id: autoConfirmDialog
        title: qsTr("Automatic pipeline?")
        prompt: qsTr("The pipeline replaces the blocks and the text of the selected pages. Continue?")
        onConfirmed: Controller.startAutoPipeline()
    }
```

**Шаг 3: строка состояния**

`src/qml/TaskStatus.qml` — в начале `text()`:

```qml
    if (Controller.autoPipelineRunning) {
        const stage = Controller.autoPipelineStageTitle
        const total = Controller.autoPipelineProgressTotal
        const done = Controller.autoPipelineProgressDone
        const head = qsTr("Auto %1/%2: %3").arg(Controller.autoPipelineStageIndex + 1)
                         .arg(Controller.autoPipelineStageCount).arg(stage)
        return total > 0 ? qsTr("%1 %2 / %3").arg(head).arg(done).arg(total) : head
    }
```

**Шаг 4: переводы**

В `resources/i18n/llocr_ru.ts` дописать:

- контекст `Header`: «Stop the automatic pipeline», «The automatic pipeline needs a markup and a recognition model», «Automatic pipeline?», «The pipeline replaces the blocks and the text of the selected pages. Continue?»
- контекст `TaskStatus`: «Auto %1/%2: %3», «%1 %2 / %3»
- контекст `AutoPipelineController`: «Another job is running», «No pages to process», «The document is busy»
- контекст `AutoPipelineStage`: «Markup», «Block recognition», «Verification», «Verification of the corrected blocks», «Fix», «No model for this task», «Nothing to recognize», «Nothing to check», «Nothing to fix»

Русские переводы — по смыслу, без машинного тона.

**Шаг 5: проверить QML**

```sh
cmake --build build -j 8
scripts/check.sh
```

`qmllint` проверяет типы свойств: имена свойств из шага 1 задачи 4 должны
существовать в `AppController`.

**Шаг 6: коммит**

```sh
git add src/qml/MainWindow/Header.qml src/qml/TaskStatus.qml resources/i18n/llocr_ru.ts
git commit -m "feat: the auto button runs the pipeline and the footer shows the stage"
```

---

## Проверка целиком

```sh
scripts/check.sh
```

Затем вручную: открыть документ → «Авто» → «Распознать N стр.» → пройти
конвейер до конца → в подвале видно этапы → блоки получили текст, вердикты и
правки.

## Что осталось за рамками плана

- Распознавание разных типов блоков разными моделями — шов готов
  (`TypeFilter` + этап), сама таблица «тип → задача» и новые задачи — следующий
  ADR.
- Уровни сложности авто-режима — не делаем (решение пользователя: одна кнопка).
- Прогон в фоне с возобновлением после перезапуска — не делаем.
