# 15 — План перехода на decision-модель в пайплайне проверки

Рабочий план рефакторинга пайплайна проверки. Целевое состояние модели
описано в [`14-d1-3b.md`](14-d1-3b.md) (и [`14-d1-3b.ru.md`](14-d1-3b.ru.md));
этот документ — последовательность шагов внедрения с критериями проверки.
После каждого шага — остановка для ручной проверки.

## Принятые решения

1. **Статусы блоков.** К существующим добавляется `Mismatch`: `NotChecked`
   (серая точка — «запланировано к проверке»), `Ok` (зелёная — decision-модель
   подтвердила совпадение), **`Mismatch`** (красная — decision-модель не
   подтвердила, блок ждёт повторного распознавания), `Fixed` (янтарная — блок
   повторно распознан, `correctedText` хранит результат), `Review` (красная —
   блок нечитаем / повторное распознавание вернуло пустоту). Новое значение
   добавляется в конец enum — старые `.llocr`-проекты читаются без миграции.
2. **Опции.** `check/autoCheck` («Автоматическая проверка») запускает очередь
   проверки (фаза decision) после распознавания. Новый ключ
   `check/autoRecheck` («Автоматическое повторное распознавание») разрешает
   фазу 2 сразу за фазой 1. Ручные «Проверить всё/страницу/блок» используют ту
   же логику: фаза 1 всегда, фаза 2 — при включённой опции.
3. **Запрос роли blockRecognition.** Состав запроса не меняется (system +
   промпт типа блока + изображение + распознанный текст как ориентир для
   таблиц/формул), меняется только контракт ответа — сразу полный текст блока.
4. **Хранилище настроек.** Ключи `check/*` в QSettings не переименовываются
   (настройки пользователей сохраняются); переименовывается роль в профилях,
   реестре моделей (миграция `index.json`) и enum'ах. Внутренние имена
   `CheckController` / `CheckRequest` / `CheckResult` сохраняются.
5. **Decision-модель.** Собственный эндпоинт `/v1/systemone` (не
   chat/completions), текст вопроса — `roles.decision.systemPrompt` профиля,
   порог совпадения `decision/matchThreshold` (по умолчанию 0.5, зажим
   [0,1]), собственный алиас `llocr-decision` и ключи `decision/*`. Ошибка
   запроса (сеть, 404 на старом рантайме) — не вердикт: блок остаётся
   непроверенным, ошибка выводится в футер.
6. Один прогон проверки — не более двух переключений сервера (decision ↔
   blockRecognition); переключения с общими весами схлопываются по общему
   правилу.

## Шаги

### Шаг 1. Переименование роли `check` → `blockRecognition` (без изменения поведения)

- Профили `lfm25-vl-3b.json`, `qwen3.5-4b.json`, `teleocr.json`: ключ
  `roles.check` → `roles.blockRecognition` (промпты не трогаем — контракт
  OK/FIX/REVIEW остаётся до шага 4).
- C++: строка роли в местах lookup'а (`ServerLaunchConfig.cpp`,
  `RuntimeController.cpp`, `LaunchProfileStore.cpp`, `ModelInstaller.cpp`,
  `ModelQuantModel.cpp`, `ModelInstallTransaction.cpp`,
  `RequestProfileStore.cpp`, `SettingsStore.cpp`,
  `VerificationQueueController.cpp`, `GeneralPurposeModel.cpp`);
  `ConnectionRole::Check` → `ConnectionRole::BlockRecognition`,
  `RequestProfileStore::Role::Check` → `Role::BlockRecognition`.
- Миграция `index.json` реестра моделей: `"check"` в списках `roles` →
  `"blockRecognition"` при загрузке.
- UI: «Модель проверки» → «Модель OCR блока» (меню, окно настроек, вкладка
  мастера, сводка) + `llocr_ru.ts`.
- Тесты: `test_model_preset_catalog`, `test_model_registry` (+ тест
  миграции), `test_settings_store`, `test_launch_profile`,
  `test_request_profile`, `test_model_installer`, `test_ensure_connection`,
  `test_ocr_models`, `test_app_import`.

Критерии: `scripts/check.sh` зелёный; в меню «Модель OCR блока»; ранее
установленные модели проверки видны и активируются (миграция реестра);
проверка документа работает как раньше (OK/FIX/REVIEW).

### Шаг 2. Роль `decision`: бэкенд + окно настроек

- `ConnectionRole::Decision`; ветвления в `RuntimeController` (ключи
  `decision/modelPath` / `mmprojPath` / `draftPath`, `serverRunsRole`,
  статус «Switching to the decision model…», внешний режим
  `decision/modelName`), `ServerLaunchConfig::fromSettings`.
- `RequestProfileStore::Role::Decision` + синглтон `RequestProfilesDecision`
  в `main.cpp`.
- `SettingsStore`: `decision/modelPath`, `decision/mmprojPath`,
  `decision/draftPath`, `decision/requestProfileId`, `decision/modelName`,
  `decision/matchThreshold` (double, 0.5, зажим [0,1]).
- Генерализация `bool forCheck` → параметр роли в `ModelInstaller` (третий
  список `decisionQuantModels`), `ModelQuantModel`, `ModelInstallTransaction`;
  поддержка `"decision"` в `ModelRegistry.roles`; `LaunchProfileStore`.
- QML: окна настроек переходят с `isVerifyModelRole: bool` на
  `role: string`; третье окно в `Main.qml`; пункт меню «Модель decision»
  после «Модель OCR»; в `RuntimeTabExternal` поле «Model name (decision)»;
  в Request-вкладке окна decision — «Match threshold (%)» (0–100, шаг 5).
- Тесты: свитч на decision в `test_ensure_connection`; дефолты/зажим
  `decision/*` в `test_settings_store`; три списка в `test_model_installer`;
  launch-слой decision в `test_launch_profile`.

Критерии: `scripts/check.sh` зелёный; в настройках «Модель decision»: d1-3b
(q4_k_m/q8_0) скачивается и активируется, Launch/Request-вкладки с порогом;
остальные окна не изменились.

### Шаг 3. `DecisionModel`: клиент `/v1/systemone`

- `src/core/DecisionRequest.h` / `DecisionResult.h`,
  `src/models/DecisionModel.{h,cpp}`: тело запроса (`state` = текст блока,
  `images` = data-URL кропа, `questions.match` типа `noul` с текстом из
  профиля), парс ответа в трёх написаниях (`"match": 0.97`, `{"noul":…}`,
  `{"probability":…}`), остальное — ошибка; `abort()`.
- `src/app/DecisionController.{h,cpp}`: резолв `ConnectionRole::Decision`,
  один запрос, stop/abort — аналог `CheckController`.
- `mock_llama_server`: маршрут `/v1/systemone` (флаги управляемой вероятности
  и «сломанного» режима — 404/мусор).
- Новый тест `test_decision_model`.

Критерии: `scripts/check.sh` зелёный, новый тест проходит; в UI пока ничего
не меняется (осознанная остановка).

### Шаг 4. Новый пайплайн очереди: decision → повторное распознавание

- `BoxCheckStatus` / `CheckStatus`: + `Mismatch` (в конец enum).
- `VerificationQueueController` — две фазы: фаза 1 через `DecisionController`
  (порог `decision/matchThreshold`); `Ok` → зелёная, ниже порога →
  `Mismatch` + очередь фазы 2; фаза 2 (`CheckController`, роль
  blockRecognition) сразу, если включён `check/autoRecheck`. Прогресс в
  футере — по фазам.
- `GeneralPurposeModel::parseResponse`: контракт blockRecognition — ответ =
  текст блока (после `stripControlTokens`); пусто → `Review`; ошибки →
  `Failed`.
- `AppController::applyCheckResultToBox`: случай `Mismatch`.
- Ключ `check/autoRecheck` + чекбокс «Автоматическое повторное
  распознавание» под «Автоматическая проверка» + обновлённые пояснения.
- Промпты роли `blockRecognition` во всех трёх профилях: без OK/FIX/REVIEW,
  «выдавай сразу полный текст блока».
- Тесты: `test_app_import` (две фазы, autoRecheck вкл/выкл, ошибка decision,
  stop), `test_ocr_models` (новый парсинг), `test_project_store`
  (раунд-трип `Mismatch`).

Критерии: `scripts/check.sh` зелёный; вручную на реальных моделях: зелёные/
красные отметки после «Проверить всё», с включённой опцией красные становятся
янтарными и текст заменяется; на рантайме без `/v1/systemone` — ошибка в
футере, блоки остаются непроверенными; Stop прерывает обе фазы.

### Шаг 5. Серые точки «запланировано к проверке»

- `BoxListModel`: роль `boxVerificationPlanned` = текст непустой ∧
  `NotChecked` ∧ (тип включён в фильтре ∨ `duplicateSuspect`); обновление при
  `setBoxes` / `updateBoxCheck` и при изменении фильтра типов.
- `ImagePreview.qml`, `BlockEditPanel.qml`: серая точка по
  `boxVerificationPlanned`.
- Тест роли в `test_box_model`.

Критерии: переключение типов блоков в «Настройки проверки» мгновенно
показывает/убирает серые точки; после проверки точка становится зелёной или
красной.

### Шаг 6. Мастер настройки: вкладка «Модель decision»

- `StepModel.qml`: три вкладки — «Модель OCR», «Модель decision», «Модель
  OCR блока»; прохождение по-прежнему гейтится только OCR-моделью.
- `StepDone.qml`: строка «Модель decision:» в сводке.

Критерии: чистый профиль → три вкладки; мастер проходится без decision; в
сводке три модели.

### Шаг 7. Кнопка «Распознать» в панели блока

- `BlockEditPanel.qml`: кнопка «Распознать» после «Проверить».
- `AppController::recognizeSelectedBlock()`: одиночный запуск фазы 2 для
  выбранного блока (работает по любому статусу); успех → `Fixed` +
  `correctedText` + пересборка страницы; пусто → `Review`; ошибка → футер.
- Тест одиночного распознавания в `test_app_import`.

Критерии: красный блок → «Распознать» → янтарный, текст заменён, «Отменить
исправление» возвращает исходный; кнопка заблокирована во время распознавания.

### Шаг 8. Документация и ADR

- `docs/07-glossary.md`: ADR 145 (двухэтапная проверка), ADR 146
  (переименование роли и миграции), ADR 147 (`/v1/systemone`); правка
  правила именования ролей.
- Сверить `docs/14-d1-3b.md` / `.ru.md` с реализацией; обновить `AGENTS.md`;
  почистить `docs/todo.md`; аудит `llocr_ru.ts`.

Критерии: ADR прочитаны, таблица файлов в `14-d1-3b.md` совпадает с кодом.
