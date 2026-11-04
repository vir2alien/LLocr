# Block Queue Roles Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Разделить `VerificationQueueController` на роль-очереди (Decision / Recheck / Remedy), убрать пробросные слои в цепочке «Исправить блок», попутно закрыть долг автопайплайна.

**Architecture:** `BlockWorkList` (чистые запросы к документу) + `BlockRunGate` (взаимное исключение) + базовый `BlockQueue` (механика очереди) + три роли: `DecisionQueue`, `RecheckQueue`, `RemedyQueue` (fix+read, один класс с двумя протоколами — ADR 260). `VerificationQueueController` удаляется; `AppController` (фасад, QML API без изменений) владеет очередями напрямую.

**Tech Stack:** Qt6 / C++ / CMake, слой `llocr_app`, тесты QtTest (`test_app_import` — end-to-end через mock-сервер, главный страховочный).

**Spec:** план одобрен пользователем 2026-11-04 (см. историю сессии); ключевые ADR-ограничения: 86, 146, 213, 257, 260, 261, 267.

## Global Constraints

- QML API `AppController` — без изменений (ADR 213); сигналы `blockVerified/blockRecognized/blockFixed/blockRead` сохраняются.
- ADR 86: очереди не мутируют документ — только сигналы.
- ADR 146: словарь `Recheck` сохраняется.
- Тексты `StatusMessage` — дословно; 3 строки `llocr_ru.ts` в контексте `VerificationQueueController` переносятся в новые контексты.
- Каждый таск — зелёное дерево (`cmake --build --preset dev-win` + ctest) и коммит.
- Windows: Qt DLLs на PATH для ctest; abort `test_export_renderer` — известная environnement-проблема, не регрессия.

## Tasks

### Task 1 — BlockWorkList: вынос коллекторов
- Create `src/app/BlockWorkList.{h,cpp}`: `Task{page,box}`, `TypeFilter`, коллекторы `unrecognizedBlocks/enabledBlocks(+counts)/fixableBlocks/problemBlocks/readableBlocks`, предикаты `pageVerificationSupported` и др. (чистые, без layoutBusy).
- VQC держит член `BlockWorkList` и делегирует.
- `tests/test_verification_queue.cpp` — фикстура строит `BlockWorkList` напрямую; слоты 1:1.

### Task 2 — Gate + BlockQueue + DecisionQueue
- Create `BlockRunGate`, `BlockQueue`, `DecisionQueue`; decision-ветка VQC переезжает; `Phase` → `{Recheck, Fix, Read}`.
- `AppController` вызывает `m_decisionQueue.run(...)` напрямую; `startOrReportAnswered` → `AppController`.
- `AutoVerificationStage` → `DecisionQueue&` + `BlockWorkList&`; refusal-тесты `test_auto_pipeline` → реальная `DecisionQueue` + gate.

### Task 3 — RecheckQueue
- Fallthrough-ветка + `m_check`-лямбда → `RecheckQueue`; `Phase` → `{Fix, Read}`; `AutoRecognitionStage` → `RecheckQueue&`.

### Task 4 — RemedyQueue
- Fix/Read-ветки + счётчики ADR 261 + read-сводка → `RemedyQueue` (`runFix`/`runRead`); `Phase` удаляется; `AutoFixStage` → `RemedyQueue&`.

### Task 5 — Демонтаж VQC
- `AppController`: `m_workList`, `m_gate`, три очереди, `wireQueue()`; `checkFinished` — член-флаг (active→false / finished→true); progress/error через `gate.lastHolder()`.
- `AvailabilityPresenter::Deps` → `BlockWorkList&`; предикаты = `!layoutBusy() && worklist.X(...)`.
- Удалить VQC (+ CMake строки), обновить `llocr_ru.ts`.

### Task 6 — Долг автопайплайна (docs/11 §10)
- Кэш собранного списка между `skipReason` и `start` в трёх queue-стадиях.

### Task 7 — Документы
- Новый ADR в `docs/07-glossary.md` (следующий свободный номер); обновить `docs/03`, `docs/04`, `docs/09`, `docs/11` + ru-зеркала.

## Verification

```bat
cmake --build --preset dev-win
set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
ctest --test-dir build\win-msvc2022 -j 4
```

`test_app_import` должен пройти без правок (кроме удаления include VQC).

## Review Focus

1. Поздний результат после `stop()` не должен засчитаться новому запуску (guard `!active()`).
2. `checkBusy` не должен «падать» в паузах между запросами — `gate.held()`.
3. `stop()` обязан emission `finished()` активной очереди (ADR 257).
4. Read-сводка — только у read-прогона с `total>0` (ADR 261).
5. Нет рендерера → Review, не тихий скип; отказ рендера → Review с ошибкой.
