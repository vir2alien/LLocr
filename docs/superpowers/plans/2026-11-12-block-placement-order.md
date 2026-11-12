# Порядок блоков: новый блок занимает своё место в тексте страницы — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Блок, нарисованный пользователем, занимает в списке блоков страницы своё геометрическое место (а при удалении блока и рисовании нового поверх него — освободившееся место), и его распознанный текст попадает в текст страницы на этом месте, а не в конец.

**Architecture:** Чистые правила размещения живут в слое `core` (`src/core/BlockPlacement.{h,cpp}`) и не знают ничего выше себя: они возвращают индекс, на который блок вставляется в список блоков страницы. Слой `app` (`AppController`) решает, когда размещение происходит: `addBlockAt` вставляет блок на вычисленный индекс вместо `append`, `removeBlock` запоминает освободившееся место (слот), `moveBlock` пересчитывает индексы слотов, новая разметка страницы сбрасывает слоты. Порядок текста не меняется — он остаётся join'ом блоков в порядке списка (ADR 296); меняется только состав и порядок списка.

**Tech Stack:** Qt6 / C++20, `llocr_core` → `llocr_app`, QTest.

**Spec:** `docs/superpowers/specs/2026-11-12-block-placement-order-design.md` — план спорит из него; исполнителю читать оба.

## Global Constraints

- **Коммиты не делает агент** — пользователь коммитит сам; шагов `git commit` в плане нет.
- После каждой задачи — зелёная проверка: `scripts/check.sh` (то же, что CI). После задач, добавляющих файлы (T1, T4), первый прогон — `scripts/check.sh --configure`.
- Быстрый цикл одного теста: `cmake --build build -j 8 && ctest --test-dir build -R test_block_placement --output-on-failure`.
- Слои (ADR 106): `llocr_core` не знает про `llocr_app`; тесты линкуют те же таргеты, что и приложение, никаких ручных копий исходников.
- `slots` — макро Qt (`#define slots`), имя параметра или переменной с таким именем не компилируется: в правилах размещения используется `freed`.
- Правило 7 AGENTS.md: комментарии — только когда код нельзя понять из имён; без TODO, без баннеров, без закомментированного кода.
- UI-строки не трогаем: правило меняет порядок блоков, а не слова на экране; новых пользовательских сообщений нет.
- Координаты блоков нормализованы (0..1), ось Y — вниз (`BoundingBox::rect` в `src/core/OcrResult.h`).
- Ссылки на кропы `image://ocr/crop/N` встраивают индекс блока в списке (`src/parsers/BlockRender.cpp:175`) — при вставке в середину они пересобираются вместе с текстом страницы.
- Индексы блоков нельзя сдвигать под работающей очередью: структурные изменения запрещены на время прогона (`checkBusy()` в `addBlockAt`/`removeBlock`/`moveBlock`) — этот инвариант сохраняется.

## Review Focus

- **Индексы слотов после изменений списка**: удаление/вставка/перемещение блока сдвигают индексы слотов; забытый сдвиг = блок, вставленный не туда. Пин: T3, шаги 4–7.
- **Слот не должен перехватывать чужой блок**: правило «половина меньшего прямоугольника» — блок, нарисованный рядом, слот не берёт. Пин: T1, шаг 1 (тест `aBlockAwayFromTheFreedPlaceTakesItsGeometricPlace`).
- **Существующие блоки не двигаются**: правило меняет только позицию вставки; проверять это по индексам, а не по тексту. Пин: T2, шаг 6.
- **Ссылки на кропы после вставки в середину**: индекс картинки растёт на единицу, ссылка в тексте страницы должна это отражать. Пин: T4, шаг 5.
- **Тесты, которые меняют ожидания**: `drawnBlockReachesThePageTextOnAMarkupPage` ждёт индекс «в конце списка» (2) — теперь это 1; ожидание меняется осознанно, а не «чтобы тест прошёл». Пин: T2, шаг 7.

---

### Task 1: Чистые правила размещения блока (`llocr_core`)

**Files:**
- Create: `src/core/PageIndex.h` (перенос `remapIndexAfterMove` из `src/app/PageIndex.h` — слой `core` не может зависеть от `app`)
- Create: `src/core/BlockPlacement.h`
- Create: `src/core/BlockPlacement.cpp`
- Modify: `src/app/PageIndex.h` (включить `core/PageIndex.h`, убрать перенесённую функцию)
- Modify: `src/CMakeLists.txt` (добавить новые файлы в `llocr_core`)
- Create: `tests/test_block_placement.cpp`
- Modify: `tests/CMakeLists.txt` (зарегистрировать таргет `test_block_placement`)

**Interfaces:**
- Consumes: `llocr::BoundingBox` (`src/core/OcrResult.h`, поле `rect` — `QRectF` в координатах 0..1).
- Produces:
  ```cpp
  namespace llocr {
  struct FreeSlot { int index = -1; QRectF rect; };
  struct Placement { int index = 0; int slot = -1; };
  Placement placementFor(const QList<BoundingBox> &boxes, const QRectF &rect, const QList<FreeSlot> &freed);
  QList<FreeSlot> slotsAfterRemoval(const QList<FreeSlot> &freed, int removedIndex, const QRectF &removedRect);
  QList<FreeSlot> slotsAfterPlacement(const QList<FreeSlot> &freed, const Placement &placement);
  QList<FreeSlot> slotsAfterMove(const QList<FreeSlot> &freed, int from, int to);
  int remapIndexAfterMove(int key, int from, int to);   // переехал из src/app/PageIndex.h
  }
  ```

- [ ] **Step 1: Написать падающий тест**

Создать `tests/test_block_placement.cpp`:

```cpp
#include <QtTest>

#include "core/BlockPlacement.h"

using namespace llocr;

namespace {

BoundingBox blockAt(qreal x, qreal y, qreal w, qreal h)
{
    BoundingBox box;
    box.rect = QRectF(x, y, w, h);
    return box;
}

// Three blocks of one column: a title, a text and a text.
QList<BoundingBox> column()
{
    return {blockAt(0.08, 0.06, 0.82, 0.10), blockAt(0.08, 0.20, 0.82, 0.20), blockAt(0.08, 0.50, 0.82, 0.30)};
}

}  // namespace

class TestBlockPlacement : public QObject
{
    Q_OBJECT

private slots:
    void aBlockAboveTheFirstOneTakesTheFirstPlace();
    void aBlockBetweenTwoTakesThePlaceBetweenThem();
    void aBlockBelowAllOfThemGoesLast();
    void aBlockToTheLeftInTheSameBandGoesFirst();
    void aBlockInAnotherColumnStaysInItsColumn();
    void aBlockOverAFreedPlaceTakesThatPlace();
    void aBlockAwayFromTheFreedPlaceTakesItsGeometricPlace();
    void theBestOfSeveralFreedPlacesWins();
    void removingABlockFreesItsPlace();
    void placingABlockShiftsTheFreedPlaces();
    void movingABlockShiftsTheFreedPlaces();
};

void TestBlockPlacement::aBlockAboveTheFirstOneTakesTheFirstPlace()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.01, 0.20, 0.03), {});
    QCOMPARE(placement.index, 0);
    QCOMPARE(placement.slot, -1);
}

void TestBlockPlacement::aBlockBetweenTwoTakesThePlaceBetweenThem()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.42, 0.20, 0.05), {});
    QCOMPARE(placement.index, 2);
}

void TestBlockPlacement::aBlockBelowAllOfThemGoesLast()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.85, 0.20, 0.05), {});
    QCOMPARE(placement.index, 3);
}

void TestBlockPlacement::aBlockToTheLeftInTheSameBandGoesFirst()
{
    const QList<BoundingBox> band = {blockAt(0.30, 0.10, 0.30, 0.10), blockAt(0.70, 0.10, 0.20, 0.10)};
    const Placement placement = placementFor(band, QRectF(0.05, 0.12, 0.20, 0.05), {});
    QCOMPARE(placement.index, 0);
}

void TestBlockPlacement::aBlockInAnotherColumnStaysInItsColumn()
{
    // Two columns: the reading order is column by column.
    const QList<BoundingBox> two = {blockAt(0.05, 0.10, 0.40, 0.10), blockAt(0.05, 0.30, 0.40, 0.10), blockAt(0.55, 0.10, 0.40, 0.10), blockAt(0.55, 0.30, 0.40, 0.10)};
    // The drawn block sits under the left column — it belongs after the left
    // column's blocks, not at the end of the page.
    const Placement placement = placementFor(two, QRectF(0.05, 0.50, 0.40, 0.10), {});
    QCOMPARE(placement.index, 2);
}

void TestBlockPlacement::aBlockOverAFreedPlaceTakesThatPlace()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}};
    const Placement placement = placementFor(column(), QRectF(0.10, 0.25, 0.30, 0.10), freed);
    QCOMPARE(placement.index, 1);
    QCOMPARE(placement.slot, 0);
}

void TestBlockPlacement::aBlockAwayFromTheFreedPlaceTakesItsGeometricPlace()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}};
    const Placement placement = placementFor(column(), QRectF(0.10, 0.85, 0.20, 0.05), freed);
    QCOMPARE(placement.index, 3);
    QCOMPARE(placement.slot, -1);
}

void TestBlockPlacement::theBestOfSeveralFreedPlacesWins()
{
    const QList<FreeSlot> freed = {FreeSlot{0, QRectF(0.05, 0.05, 0.30, 0.10)}, FreeSlot{2, QRectF(0.05, 0.45, 0.30, 0.10)}};
    const Placement placement = placementFor(column(), QRectF(0.05, 0.46, 0.30, 0.08), freed);
    QCOMPARE(placement.index, 2);
    QCOMPARE(placement.slot, 1);
}

void TestBlockPlacement::removingABlockFreesItsPlace()
{
    const QList<FreeSlot> freed = slotsAfterRemoval({}, 1, QRectF(0.08, 0.20, 0.82, 0.20));
    QCOMPARE(freed.size(), 1);
    QCOMPARE(freed.first().index, 1);

    // A place further down the list shifts with the blocks before it.
    const QList<FreeSlot> shifted = slotsAfterRemoval({FreeSlot{3, QRectF(0.05, 0.60, 0.30, 0.10)}}, 1, QRectF(0.08, 0.20, 0.82, 0.20));
    QCOMPARE(shifted.size(), 2);
    QCOMPARE(shifted.at(0).index, 2);
    QCOMPARE(shifted.at(1).index, 1);
}

void TestBlockPlacement::placingABlockShiftsTheFreedPlaces()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}, FreeSlot{3, QRectF(0.05, 0.60, 0.30, 0.10)}};
    const QList<FreeSlot> shifted = slotsAfterPlacement(freed, Placement{1, -1});
    QCOMPARE(shifted.size(), 2);
    QCOMPARE(shifted.at(0).index, 2);
    QCOMPARE(shifted.at(1).index, 4);
}

void TestBlockPlacement::movingABlockShiftsTheFreedPlaces()
{
    const QList<FreeSlot> freed = {FreeSlot{2, QRectF(0.05, 0.60, 0.30, 0.10)}};
    const QList<FreeSlot> shifted = slotsAfterMove(freed, 0, 3);
    QCOMPARE(shifted.size(), 1);
    QCOMPARE(shifted.first().index, 1);
}

QTEST_MAIN(TestBlockPlacement)
#include "test_block_placement.moc"
```

- [ ] **Step 2: Зарегистрировать таргет и убедиться, что он падает**

В `tests/CMakeLists.txt` рядом с другими таргетами слоя `core` добавить:

```cmake
qt_add_executable(test_block_placement test_block_placement.cpp)

target_link_libraries(test_block_placement PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_block_placement COMMAND test_block_placement)
```

Выполнить:

```bash
cmake --build build -j 8
ctest --test-dir build -R test_block_placement --output-on-failure
```

Ожидаемая ошибка компиляции: `core/BlockPlacement.h` не найден (`fatal error: 'core/BlockPlacement.h' file not found`) — это и есть «красный» тест.

- [ ] **Step 3: Перенести `remapIndexAfterMove` в слой `core`**

Создать `src/core/PageIndex.h`:

```cpp
#pragma once

namespace llocr {

// The index of a key (a page or a block) after the item at `from` moved to
// `to`: the moved item takes the new index, the items between the two shift
// by one.
inline int remapIndexAfterMove(int key, int from, int to)
{
    if (key == from)
        return to;
    if (from < to && key > from && key <= to)
        return key - 1;
    if (from > to && key >= to && key < from)
        return key + 1;
    return key;
}

}  // namespace llocr
```

В `src/app/PageIndex.h` удалить определение `remapIndexAfterMove` и добавить `#include "core/PageIndex.h"` в начало файла (остальные хелперы — `remapHashAfterRemove`, `remapHashAfterMove` — остаются здесь; пространство имён то же, поэтому все вызывающие места продолжают компилироваться без правок).

- [ ] **Step 4: Реализовать правила размещения**

Создать `src/core/BlockPlacement.h`:

```cpp
#pragma once

#include <QList>
#include <QRectF>

#include "core/OcrResult.h"

namespace llocr {

// The place a removed block leaves behind: its position in the block list and
// the rectangle it covered. A block drawn over that rectangle takes the place
// in the text (ADR 310).
struct FreeSlot {
    int index = -1;
    QRectF rect;
};

// Where a block drawn on the page goes into the list of blocks.
struct Placement {
    int index = 0;  ///< the position the block takes in the list
    int slot = -1;  ///< the freed place the block took, -1 when it took none
};

// The place a block drawn at `rect` takes: the freed place it covers, or its
// position on the page.
Placement placementFor(const QList<BoundingBox> &boxes, const QRectF &rect, const QList<FreeSlot> &freed);

// The freed of a page after its blocks changed.
QList<FreeSlot> slotsAfterRemoval(const QList<FreeSlot> &freed, int removedIndex, const QRectF &removedRect);
QList<FreeSlot> slotsAfterPlacement(const QList<FreeSlot> &freed, const Placement &placement);
QList<FreeSlot> slotsAfterMove(const QList<FreeSlot> &freed, int from, int to);

}  // namespace llocr
```

Создать `src/core/BlockPlacement.cpp`:

```cpp
#include "core/BlockPlacement.h"

#include "core/PageIndex.h"

namespace llocr {

namespace {

// Half a percent of the page: the tolerance the coordinates of two blocks are
// compared with.
constexpr qreal kEpsilon = 0.005;

// Two rectangles share a column when their horizontal spans cover at least
// half of the narrower one's width — the vertical position says nothing about
// the column.
bool sharesColumn(const QRectF &a, const QRectF &b)
{
    const qreal narrower = qMin(a.width(), b.width());
    if (narrower <= 0.0)
        return false;
    const qreal left = qMax(a.left(), b.left());
    const qreal right = qMin(a.right(), b.right());
    return right - left >= narrower / 2.0;
}

// The share of the smaller rectangle the two cover together.
qreal overlapShare(const QRectF &a, const QRectF &b)
{
    const QRectF shared = a.intersected(b);
    const qreal area = shared.width() * shared.height();
    const qreal smaller = qMin(a.width() * a.height(), b.width() * b.height());
    return smaller > 0.0 ? area / smaller : 0.0;
}

// The block lies after the drawn one: below it, or to the right in the same
// band.
bool liesAfter(const QRectF &block, const QRectF &drawn)
{
    if (block.top() >= drawn.bottom() - kEpsilon)
        return true;
    const qreal band = qMin(block.bottom(), drawn.bottom()) - qMax(block.top(), drawn.top());
    return band > 0.0 && block.left() >= drawn.right() - kEpsilon;
}

}  // namespace

Placement placementFor(const QList<BoundingBox> &boxes, const QRectF &rect, const QList<FreeSlot> &freed)
{
    Placement placement;
    qreal bestShare = 0.0;
    for (int i = 0; i < freed.size(); ++i) {
        const qreal share = overlapShare(freed.at(i).rect, rect);
        if (share < 0.5)
            continue;
        if (placement.slot < 0 || share > bestShare + kEpsilon
            || (qFuzzyCompare(share, bestShare) && freed.at(i).index < freed.at(placement.slot).index)) {
            placement.slot = i;
            bestShare = share;
        }
    }
    if (placement.slot >= 0) {
        placement.index = qBound(0, freed.at(placement.slot).index, boxes.size());
        return placement;
    }

    QList<int> candidates;
    for (int i = 0; i < boxes.size(); ++i) {
        if (sharesColumn(boxes.at(i).rect, rect))
            candidates.append(i);
    }
    if (candidates.isEmpty()) {
        for (int i = 0; i < boxes.size(); ++i)
            candidates.append(i);
    }
    for (const int index : candidates) {
        if (liesAfter(boxes.at(index).rect, rect)) {
            placement.index = index;
            return placement;
        }
    }
    placement.index = candidates.isEmpty() ? boxes.size() : candidates.last() + 1;
    return placement;
}

QList<FreeSlot> slotsAfterRemoval(const QList<FreeSlot> &freed, int removedIndex, const QRectF &removedRect)
{
    QList<FreeSlot> out;
    for (const FreeSlot &slot : freed) {
        if (slot.index == removedIndex)
            continue;
        FreeSlot moved = slot;
        if (moved.index > removedIndex)
            --moved.index;
        out.append(moved);
    }
    FreeSlot place;
    place.index = removedIndex;
    place.rect = removedRect;
    out.append(place);
    return out;
}

QList<FreeSlot> slotsAfterPlacement(const QList<FreeSlot> &freed, const Placement &placement)
{
    QList<FreeSlot> out;
    for (int i = 0; i < freed.size(); ++i) {
        if (i == placement.slot)
            continue;
        FreeSlot moved = freed.at(i);
        if (moved.index >= placement.index)
            ++moved.index;
        out.append(moved);
    }
    return out;
}

QList<FreeSlot> slotsAfterMove(const QList<FreeSlot> &freed, int from, int to)
{
    QList<FreeSlot> out;
    out.reserve(freed.size());
    for (const FreeSlot &slot : freed) {
        FreeSlot moved = slot;
        moved.index = remapIndexAfterMove(moved.index, from, to);
        out.append(moved);
    }
    return out;
}

}  // namespace llocr
```

- [ ] **Step 5: Подключить новые файлы к сборке**

В `src/CMakeLists.txt` в списке исходников `llocr_core` добавить:

```cmake
    core/PageIndex.h
    core/BlockPlacement.h
    core/BlockPlacement.cpp
```

- [ ] **Step 6: Собрать и прогнать тест**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_block_placement --output-on-failure
```

Ожидается: `test_block_placement` PASS (11 проверок). Если падает — правило в `placementFor` не совпадает с ожиданиями из шага 1; сверить расчёт по координатам из комментариев теста.

- [ ] **Step 7: Полная проверка дерева**

```bash
scripts/check.sh
```

Ожидается: всё зелёное (сборка, `ctest`, clang-format, qmllint).

---

### Task 2: Вставка блока на вычисленное место (`llocr_app`)

**Files:**
- Modify: `src/app/BoxListModel.h` (объявить `insertBox`)
- Modify: `src/app/BoxListModel.cpp` (реализация `insertBox`)
- Modify: `src/app/AppController.h` (поле `m_freeSlots`, включение `core/BlockPlacement.h`)
- Modify: `src/app/AppController.cpp` (`addBlockAt`: вставка на вычисленный индекс вместо `append`)
- Modify: `tests/test_app_import.cpp` (новый тест + обновлённое ожидание в `drawnBlockReachesThePageTextOnAMarkupPage`)

**Interfaces:**
- Consumes: `llocr::placementFor`, `llocr::Placement` из `core/BlockPlacement.h` (T1).
- Produces:
  ```cpp
  // BoxListModel
  void insertBox(int row, const BoundingBox &box);
  ```

- [ ] **Step 1: Написать падающий тест**

В `tests/test_app_import.cpp` в класс теста добавить новый слот и тест:

```cpp
    // A block the operator drew in the middle of the page: it takes the place
    // between the blocks around it, and its recognized text lands there in the
    // page text — not at the end of it.
    void aDrawnBlockTakesItsPlaceInThePageText()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        // Three blocks: a title over two texts.
        server.layoutContent = QStringLiteral("<|box_start|>80 60 900 160<|box_end|><|ref_start|>title<|ref_end|>\n\n"
                                              "<|box_start|>80 200 900 400<|box_end|><|ref_start|>text<|ref_end|>\n\n"
                                              "<|box_start|>80 500 900 800<|box_end|><|ref_start|>text<|ref_end|>");
        server.transcriptionSequence = {QStringLiteral("First block"), QStringLiteral("Second block"), QStringLiteral("Third block")};

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 3);

        // The operator draws a block between the second and the third one.
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.1, 0.42, 0.2, 0.05));
        QCOMPARE(boxes->rowCount(), 4);
        QCOMPARE(controller.selectedBoxIndex(), 2);

        // The block is recognized…
        server.transcriptionSequence = {QStringLiteral("Drawn block")};
        const int requests = server.requests();
        controller.recognizeSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), requests + 1, kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(controller.selectedBlockText(), QStringLiteral("Drawn block"), kImportTimeoutMs);

        // …and its text stands between the texts of the blocks around it.
        const QString text = controller.resultText();
        const int first = text.indexOf(QStringLiteral("First block"));
        const int drawn = text.indexOf(QStringLiteral("Drawn block"));
        const int third = text.indexOf(QStringLiteral("Third block"));
        QVERIFY(first >= 0);
        QVERIFY(drawn >= 0);
        QVERIFY(third >= 0);
        QVERIFY2(first < drawn && drawn < third, qPrintable(text));
    }
```

- [ ] **Step 2: Убедиться, что тест падает**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_app_import --output-on-failure
```

Ожидается: падение на `QCOMPARE(controller.selectedBoxIndex(), 2)` — сейчас нарисованный блок попадает в конец списка (индекс 3), а его текст — в конец текста страницы.

- [ ] **Step 3: Добавить `insertBox` в модель блоков**

В `src/app/BoxListModel.h` рядом с `removeBox`/`moveBox` объявить:

```cpp
    void insertBox(int row, const BoundingBox &box);
```

В `src/app/BoxListModel.cpp` рядом с `removeBox` реализовать:

```cpp
void BoxListModel::insertBox(int row, const BoundingBox &box)
{
    if (row < 0 || row > m_boxes.size())
        return;
    beginInsertRows({}, row, row);
    m_boxes.insert(row, box);
    endInsertRows();
}
```

- [ ] **Step 4: Вставлять блок на вычисленный индекс**

В `src/app/AppController.h`:
- в список включений добавить `#include "core/BlockPlacement.h"`;
- рядом с другими полями добавить:

```cpp
    // The places the removed blocks leave behind (ADR 310): a block drawn
    // over one of them takes its place in the text.
    QHash<int, QList<FreeSlot>> m_freeSlots;
```

В `src/app/AppController.cpp`, функция `addBlockAt` — заменить добавление в конец на вставку на вычисленное место:

```cpp
    const QList<FreeSlot> freed = m_freeSlots.value(m_currentPage);
    Placement placement;
    int boxCount = 0;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(m_currentPage);
        if (page.result.pages.isEmpty())
            page.result.pages.append(OcrPage());
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        boxCount = boxes.size();
        placement = placementFor(boxes, box.rect, freed);
        boxes.insert(placement.index, box);
        page.recognized = true;
        ++m_cropRevision;
    }
    m_freeSlots[m_currentPage] = slotsAfterPlacement(freed, placement);
```

и ниже по функции заменить

```cpp
    updateBoxesForCurrent();
```

на

```cpp
    if (m_boxModel.rowCount() == boxCount)
        m_boxModel.insertBox(placement.index, box);
    else
        updateBoxesForCurrent();
```

и заменить

```cpp
    setSelectedBoxIndex(m_document.page(m_currentPage).result.pages.first().boxes.size() - 1);
```

на

```cpp
    setSelectedBoxIndex(placement.index);
```

- [ ] **Step 5: Пересобрать**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_app_import --output-on-failure
```

Ожидается: новый тест `aDrawnBlockTakesItsPlaceInThePageText` PASS; остальные тесты файла — без изменений в статусе.

- [ ] **Step 6: Обновить ожидание индекса в существующем тесте**

В `tests/test_app_import.cpp`, тест `drawnBlockReachesThePageTextOnAMarkupPage`: блок удаляется (индекс 1), а новый нарисован справа от оставшейся таблицы. Он встаёт на место удалённого блока — индекс 1, а не 2 (конец списка). Заменить:

```cpp
        QCOMPARE(controller.selectedBoxIndex(), 2);
```

на

```cpp
        QCOMPARE(controller.selectedBoxIndex(), 1);
```

- [ ] **Step 7: Полная проверка дерева**

```bash
scripts/check.sh
```

---

### Task 3: Освободившееся место (слоты) — удаление, перемещение, новая разметка

**Files:**
- Modify: `src/app/AppController.cpp` (`removeBlock` — запись слота; `moveBlock` — пересчёт индексов слотов; `applyLayout` — сброс слотов страницы)
- Modify: `tests/test_app_import.cpp` (новый тест: удалённый блок и новый на его месте)

**Interfaces:**
- Consumes: `llocr::slotsAfterRemoval`, `llocr::slotsAfterMove`, `llocr::slotsAfterPlacement` (T1).
- Produces: без новых публичных интерфейсов; поле `AppController::m_freeSlots` (T2) наполняется и поддерживается.

- [ ] **Step 1: Написать падающий тест**

В `tests/test_app_import.cpp` добавить тест. Разметка — **две колонки**: на одной колонке слот и чистая геометрия дают одинаковый результат, и тест не отличил бы правило от геометрии. На двух колонках блок, нарисованный на месте удалённого правого блока, по геометрии встал бы в левую колонку (индекс 1), а по слоту — на своё место (индекс 2).

```cpp
    // The operator deletes a block and draws a new one over the place it
    // left: the new block's text stands where the deleted block's text stood.
    void aDrawnBlockTakesThePlaceOfARemovedOne()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));
        // Two columns: the reading order is column by column.
        server.layoutContent = QStringLiteral("<|box_start|>80 200 480 500<|box_end|><|ref_start|>text<|ref_end|>\n\n"
                                              "<|box_start|>80 550 480 850<|box_end|><|ref_start|>text<|ref_end|>\n\n"
                                              "<|box_start|>550 200 950 500<|box_end|><|ref_start|>text<|ref_end|>");
        server.transcriptionSequence = {QStringLiteral("Left top"), QStringLiteral("Left bottom"), QStringLiteral("Right top")};

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 3);

        // The operator deletes the right column's block…
        QVERIFY(controller.removeBlock(2));
        QCOMPARE(boxes->rowCount(), 2);
        QVERIFY(!controller.resultText().contains(QStringLiteral("Right top")));

        // …and draws a new one over the place it left.
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.6, 0.25, 0.3, 0.1));
        QCOMPARE(boxes->rowCount(), 3);
        QCOMPARE(controller.selectedBoxIndex(), 2);

        server.transcriptionSequence = {QStringLiteral("Drawn block")};
        const int requests = server.requests();
        controller.recognizeSelectedBlock();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(), requests + 1, kImportTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(controller.selectedBlockText(), QStringLiteral("Drawn block"), kImportTimeoutMs);

        // The new block's text stands where the removed block's text stood: the
        // whole left column comes before it.
        const QString text = controller.resultText();
        const int leftTop = text.indexOf(QStringLiteral("Left top"));
        const int leftBottom = text.indexOf(QStringLiteral("Left bottom"));
        const int drawn = text.indexOf(QStringLiteral("Drawn block"));
        QVERIFY(leftTop >= 0);
        QVERIFY(leftBottom >= 0);
        QVERIFY(drawn >= 0);
        QVERIFY2(leftTop < leftBottom && leftBottom < drawn, qPrintable(text));
    }
```

- [ ] **Step 2: Убедиться, что тест падает**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_app_import --output-on-failure
```

Ожидается: падение `QCOMPARE(controller.selectedBoxIndex(), 2)` — фактическое значение 1 (геометрия вставила блок в левую колонку).

- [ ] **Step 3: Запоминать освободившееся место при удалении**

В `src/app/AppController.cpp`, функция `removeBlock`: сохранить прямоугольник удалённого блока и обновить слоты:

```cpp
    BoundingBox removed;
    {
        QWriteLocker locker(&m_documentLock);
        ...
        removed = boxes.at(boxIndex);
        boxes.removeAt(boxIndex);
        ++m_cropRevision;
    }
    m_freeSlots[m_currentPage] = slotsAfterRemoval(m_freeSlots.value(m_currentPage), boxIndex, removed.rect);
```

(переменная `removed` объявляется перед блоком с блокировкой; `boxes.at(boxIndex)` читается до `removeAt`).

- [ ] **Step 4: Пересчитывать индексы слотов при перемещении**

В `src/app/AppController.cpp`, функция `moveBlock`, после `boxes.move(from, to);`:

```cpp
    m_freeSlots[m_currentPage] = slotsAfterMove(m_freeSlots.value(m_currentPage), from, to);
```

- [ ] **Step 5: Сбрасывать слоты при новой разметке**

В `src/app/AppController.cpp`, функция `applyLayout` (страница получает новые блоки от модели разметки), рядом с присваиванием `page.result = parsed;`:

```cpp
        m_freeSlots.remove(pageIndex);
```

- [ ] **Step 6: Пересобрать и прогнать**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_app_import --output-on-failure
```

Ожидается: `aDrawnBlockTakesThePlaceOfARemovedOne` PASS.

- [ ] **Step 7: Полная проверка дерева**

```bash
scripts/check.sh
```

---

### Task 4: Ссылки на кропы и редактор после вставки

**Files:**
- Modify: `tests/test_app_import.cpp` (новый тест: ссылка на кроп остаётся верной после вставки в середину)

**Interfaces:**
- Consumes: `AppController::croppedImage(int pageIndex, int boxIndex)` (публичный метод, `src/app/AppController.h:351`), `BoxListModel::LabelRole`/`TextRole`.
- Produces: без изменений в коде приложения — тест фиксирует инвариант.

- [ ] **Step 1: Написать тест**

В `tests/test_app_import.cpp` добавить тест:

```cpp
    // A block inserted before an image block shifts the image's index — and
    // the crop reference in the page text follows it: the reference carries
    // the index of the block in the list.
    void theCropReferenceFollowsTheBlockAfterAnInsertion()
    {
        DetTokenChatServer server;
        QVERIFY(server.start());
        m_settings->setBaseUrl(server.baseUrl());
        m_settings->setModelName(QStringLiteral("det-token-test"));

        auto &controller = *m_controller;
        controller.openFiles({QUrl::fromLocalFile(m_raster)});
        QTRY_COMPARE_WITH_TIMEOUT(controller.pageCount(), 1, kImportTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.importing(), kImportTimeoutMs);

        recognizePage(controller);
        QVERIFY(controller.hasResult());

        auto *boxes = qobject_cast<BoxListModel *>(controller.boxModel());
        QVERIFY(boxes);
        QCOMPARE(boxes->rowCount(), 2);

        // An image block at the foot of the page.
        QVERIFY(controller.addBlockAt(QStringLiteral("image"), 0.1, 0.85, 0.3, 0.1));
        const int imageIndex = controller.selectedBoxIndex();
        QCOMPARE(imageIndex, 2);
        QVERIFY2(controller.resultText().contains(QStringLiteral("image://ocr/crop/2")), qPrintable(controller.resultText()));

        // A text block goes between the text block and the image.
        QVERIFY(controller.addBlockAt(QStringLiteral("text"), 0.1, 0.42, 0.3, 0.05));
        QCOMPARE(controller.selectedBoxIndex(), 2);

        // The image block moved one place down — and its reference with it.
        QCOMPARE(boxes->data(boxes->index(3), BoxListModel::LabelRole).toString(), QStringLiteral("image"));
        QVERIFY2(controller.resultText().contains(QStringLiteral("image://ocr/crop/3")), qPrintable(controller.resultText()));
        QVERIFY(!controller.resultText().contains(QStringLiteral("image://ocr/crop/2)")));

        // The reference resolves to a crop: the image block's own rectangle.
        const QImage crop = controller.croppedImage(0, 3);
        QVERIFY(!crop.isNull());
    }
```

- [ ] **Step 2: Прогнать**

```bash
cmake --build build -j 8
ctest --test-dir build -R test_app_import --output-on-failure
```

Ожидается: PASS. Если `image://ocr/crop/3` не найдено — ссылки на кропы пересобираются не по списку; проверить `renderPageText` (`src/parsers/BlockRender.cpp:198-210`): `style.imageIndex = i` — индекс списка.

- [ ] **Step 3: Полная проверка дерева**

```bash
scripts/check.sh
```

---

### Task 5: ADR 310 и документация

**Files:**
- Modify: `docs/07-glossary.md` (ADR 310 — в секцию «UI», после ADR 309)
- Modify: `docs/07-glossary.ru.md` (тот же ADR по-русски)
- Modify: `docs/03-architecture.md` (абзац о порядке блоков в описании `AppController`/`BoxListModel`)
- Modify: `docs/03-architecture.ru.md` (тот же абзац по-русски)
- Modify: `docs/19-testing.md` и `docs/19-testing.ru.md` (новый таргет `test_block_placement` в карте тестов)

**Interfaces:**
- Consumes: итоговое поведение T1–T4.
- Produces: ADR 310 — правило вставки блока.

- [ ] **Step 1: Добавить ADR 310 в `docs/07-glossary.md`**

В секции `### UI`, сразу после записи ADR 309, добавить:

```markdown
- **310.** A block the operator drew takes its place in the text by its place on
  the page. The page text is the join of the blocks in the order of the list
  (ADR 296), and the list carries the reading order the layout model returned
  — so a drawn block used to land at the end of it, and its recognized text
  with it. The list now takes the new block where it belongs: the place a
  removed block left behind (`core/BlockPlacement.h` — the slot; the drawn
  block covers half of the smaller of the two rectangles), or the block's
  geometric position — the column it shares its width with, and inside it the
  first block that lies below the drawn one or to its right in the same band.
  The blocks already on the page never move; only the new one's place is
  decided. The crop references (`image://ocr/crop/N`) carry the index of the
  block in the list and are rebuilt with the page text, so the export stays
  right. The columns are read from the geometry, not detected: a block drawn
  exactly in the gutter between two columns takes the general rule, and the
  operator's up/down buttons remain the last word.
```

- [ ] **Step 2: Добавить ADR 310 в `docs/07-glossary.ru.md`**

Тем же местом (после ADR 309) добавить русскую запись:

```markdown
- **310.** Блок, который нарисовал оператор, занимает в тексте своё место по
  положению на странице. Текст страницы — склейка блоков в порядке списка
  (ADR 296), а список несёт порядок чтения, который вернула модель разметки, —
  поэтому нарисованный блок оказывался в конце списка, и его распознанный
  текст вместе с ним. Теперь список принимает новый блок туда, куда он
  принадлежит: на место, которое оставил удалённый блок (`core/BlockPlacement.h`
  — слот; нарисованный блок закрывает половину меньшего из двух
  прямоугольников), или на своё геометрическое место — в колонку, с которой
  он делит ширину, и внутри неё перед первым блоком, который лежит ниже
  нарисованного или правее него в той же полосе. Уже лежащие на странице
  блоки не двигаются: решается только место нового. Ссылки на кропы
  (`image://ocr/crop/N`) несут индекс блока в списке и пересобираются вместе
  с текстом страницы — экспорт остаётся верным. Колонки читаются из
  геометрии, а не определяются: блок, нарисованный ровно в промежутке между
  колонками, попадает под общее правило, а перестановка вверх/вниз остаётся
  последним словом за оператором.
```

- [ ] **Step 3: Дописать `docs/03-architecture.md`**

В описании `BoxListModel` (абзац «**BoxListModel** — normalized bbox rectangles…») добавить предложение:

```markdown
  The order of the list is the order of the page text (ADR 296): a block the
  operator drew takes its place in the list by its place on the page (ADR 310).
```

В `docs/03-architecture.ru.md` — то же предложение по-русски:

```markdown
  Порядок списка — это порядок текста страницы (ADR 296): нарисованный
  оператором блок занимает в списке своё место по положению на странице
  (ADR 310).
```

- [ ] **Step 4: Обновить карту тестов**

В `docs/19-testing.md` в строке домена «Models (adapters, decision, fix/read)» (или в строку домена «Document, pages, layout, orientation» — по месту, где уже стоят `test_box_model` и `test_page_document») добавить `test_block_placement`; в `docs/19-testing.ru.md` — то же самое. Если в таблице указано число таргетов («The 74 targets, by domain»), увеличить его на единицу.

- [ ] **Step 5: Полная проверка дерева**

```bash
scripts/check.sh
```

---

## Self-Review

**Покрытие спецификации:**

| Раздел спецификации | Где реализовано |
| --- | --- |
| Шаг 1 — освободившееся место (слот) | T1 (`placementFor`), T3 (`removeBlock`, `applyLayout`) |
| Шаг 2 — геометрия по полосам (колонка + полоса) | T1 (`sharesColumn`, `liesAfter`) |
| Индексы слотов при удалении/вставке/перемещении | T1 (`slotsAfter*`), T3 (`removeBlock`, `moveBlock`) |
| `src/core/BlockPlacement` — чистые функции | T1 |
| `BoxListModel::insertBox` | T2, шаг 3 |
| `AppController::addBlockAt` — вставка вместо `append` | T2, шаг 4 |
| Порядок существующих блоков не меняется | T2 (реализация правила), проверка в тестах T2/T3 по индексам |
| Согласованность ссылок на кропы | T4 |
| Очереди не задеты (инвариант `checkBusy()`) | не меняется; Global Constraints |
| Тесты (чистые правила, текст страницы, кропы) | T1, T2, T3, T4 |
| ADR 310 + документация | T5 |
| Ограничение по колонкам | T1 (`sharesColumn`), ADR 310 (T5) |

**Пробелы, найденные при сверке и закрытые в плане:**

- Перенос `remapIndexAfterMove` в `core` — без него слой `core` зависел бы от `app`; включён в T1, шаг 3.
- Потребление слота новым блоком — в спецификации API был `int placementIndexFor(...)`, но вызывающему нужно знать, какой слот занят; API уточнён до `Placement` (`index` + `slot`), спецификация правится в T1 (шаг 4, комментарий) — правку внести в `docs/superpowers/specs/2026-11-12-block-placement-order-design.md` вместе с кодом.
- `updateBoxesForCurrent()` сбрасывает выделение (`setSelectedBoxIndex(-1)`) — при пути через `insertBox` сброса нет, выделение ставится явно на `placement.index` (T2, шаг 4).

**Проверка типов:** `FreeSlot`, `Placement`, `placementFor`, `slotsAfterRemoval`, `slotsAfterPlacement`, `slotsAfterMove`, `remapIndexAfterMove` — имена и сигнатуры одинаковы в T1 (объявление), T1 (реализация), T2 и T3 (вызовы). `BoxListModel::insertBox(int, const BoundingBox &)` — объявление (T2, шаг 3) и реализация (T2, шаг 3) совпадают; вызов в `addBlockAt` — `m_boxModel.insertBox(placement.index, box)`.

**Заглушек нет:** каждый шаг содержит конкретный код или конкретную команду; «написать тесты для выше» не встречается.

---

## Итог исполнения

Все пять задач выполнены; проверка `scripts/check.sh` — зелёная, кроме трёх WebEngine-тестов
редактора (`test_editor_page`, `test_editor_pull`, `test_editor_qml`), которые падают с
SIGTRAP одинаково и на чистом дереве: Chromium в песочнице не может зарегистрировать
Mach-порт (`mach_port_rendezvous.cc(410)`), к правке это отношения не имеет.

Отличия от плана, сделанные по ходу и внесённые в этот документ:

- **Имя `slots` недопустимо** — это макро Qt (`#define slots`); параметры правил
  размещения названы `freed` (в Global Constraints).
- **`sharesColumn` сравнивает только горизонтальные прогоны** прямоугольников: вертикальное
  положение о колонке не говорит ничего (в плане было пересечение прямоугольников целиком —
  из-за этого блок в левой колонке вставал в конец списка; тест
  `aBlockInAnotherColumnStaysInItsColumn` это ловит).
- **Тест слота построен на двухколоночной разметке**: на одноколоночной слот и чистая
  геометрия дают одинаковый индекс, и тест не отличал бы правило от геометрии.
- **Ответ сервера сравнивается со строкой «Drawn block»**, а не с полем
  `server.transcription`: поле отвечает за пустой список `transcriptionSequence`, а ответ
  приходит из самого списка.
- **Удалён мёртвый код** в `addBlockAt`: `blockDerivedText` вычислялся и не использовался.
- **Счётчики таргетов** в `AGENTS.md` и `docs/19-testing*.md` приведены к фактическим
  (76 таргетов; прежние 74 не совпадали с `ctest -N` и на новый тест не указывали).
