# План рефакторинга LLocr — god-классы, store'ы, QML-списки

> Статус: план, не реализован.
> Составлен по итогам код-ревью (сентябрь 2026). Порядок исполнения — от простого
> к сложному. Каждый этап самодостаточен: заканчивается сборкой, полным
> `ctest` и коммитом.
>
> Правила на весь план:
> - публичный QML-API контроллеров и синглтонов **не меняется** (QML-файлы
>   продолжают вызывать те же `Q_INVOKABLE`/свойства);
> - UI-строки — через `tr()` в правильном контексте, ru-переводы
>   (`resources/i18n/llocr_ru.ts`) обновляются в том же этапе;
> - после каждого этапа обновлять `docs/03-architecture.md`,
>   `docs/04-components.md`, `docs/07-glossary.md` (правило проекта) и,
>   при изменении статуса, `AGENTS.md`.

## Сводка этапов

| Этап | Сложность | Объём | Что делает |
|------|-----------|-------|------------|
| 0. Подготовка | — | S | Базлайн: сборка, тесты, ветка |
| 1. `implicitHeight` у списковых компонентов | низкая | S | QML-only, 3 компонента + 4 потребителя |
| 2. `VerificationBlocksTab`: фильтрация по группам | средняя | M | Прокси-модель вместо 3× Repeater |
| 3 | Дедупликация store'ов | средняя | M | Общий слой персистентности для 3 store'ов |
| 4. Разбор `ModelInstaller` | высокая | L | Извлечение `ModelInstallTransaction` |
| 5. Разбор `AppController` | высокая | L | Извлечение `VerificationQueueController` и `ExportController` |

> Статус: этапы 0–3 выполнены (0–2 — в предыдущих итерациях, 3 — в этой).

Зависимости: этапы 1–2 независимы друг от друга и от C++-этапов; этапы 3–5
независимы между собой, но каждый опирается на базлайн этапа 0. Внутри этапа 5
сначала 5a (верификация), потом 5b (экспорт).

---

## Этап 0. Подготовка (S)

1. Собрать и прогнать полный тест-набор как базлайн:
   ```sh
   cmake --build build -j 8
   ctest --test-dir build -j 4
   ```
   Зафиксировать результат (29/29 pass).
2. Ветка `refactor/app-model-split` (или по этапу на ветку — по вкусу;
   минимум один коммит на этап).
3. Прогнать `qmllint` из build-окружения (с путями импорта модуля `LLocr`)
   и сохранить базовый список предупреждений — чтобы отличать новые от старых.

**Критерий готовности:** baseline-тесты зелёные, ветка создана.

---

## Этап 1. `implicitHeight` у списковых компонентов (S, QML-only)

### Проблема
`Common/ModelInstalledList.qml`, `Common/ModelPresetList.qml`,
`Common/RuntimeBuildsList.qml` публикуют только `rowHeight` (дефолты 40 / — / 36)
и не имеют `implicitHeight`. Каждый потребитель дублирует арифметику высоты и
держит собственную копию константы:

| Потребитель | Файл | Формула |
|---|---|---|
| `LocationTab` (установленные модели) | `Settings/LocationTab.qml:242` | `Math.min(count,3) * 40` |
| `LocationTab` (пресеты) | `Settings/LocationTab.qml:272` | `Math.min(count,3) * 36` |
| `StepModel` | `Setup/StepModel.qml` | `rowHeight: 34` + `count * 34` |
| `RuntimeTabInternal` (билды) | `Settings/RuntimeTabInternal.qml:157` | `installedBuildCount * 36` |

Смена дефолта `rowHeight` в компоненте молча ломает потребителя (обрезка или
дыры). У `ModelInstalledList` дополнительно `visible: count > 0` — пустой
список должен давать нулевую высоту.

### Шаги
1. В каждый из трёх компонентов добавить:
   ```qml
   property int maxVisibleRows: 3
   implicitHeight: count <= 0 ? 0
                              : Math.min(count, maxVisibleRows) * rowHeight
   ```
   (у `ModelPresetList`/`RuntimeBuildsList` сверить фактические дефолты
   `rowHeight` перед правкой — если отличаются, дефолт `rowHeight` оставить
   как есть, потребители перейдут на `implicitHeight`).
2. Потребители: заменить ручные формулы на
   `Layout.preferredHeight: <listId>.implicitHeight` (или `height: ...` в
   не-Layout контексте) и удалить локальные `rowHeight`, где они существовали
   только ради формулы. В `StepModel.qml` — удалить `rowHeight: 34`, если
   дефолт компонента после унификации совпадает, иначе оставить переопределение.
3. **Не трогать** особый случай `RuntimeTabInternal.qml`: встроенный
   не-интерактивный `RuntimeBuildsList` передаёт колесо внешнему `ScrollView`
   (там есть комментарий) — меняется только источник высоты.
4. Прогнать `resources/qml` через qmllint (этап 0) — новых предупреждений нет.

### Риски
- Низкие. Всё в QML; поведение то же при тех же константах.
- Проверить глазами: Settings → Models/Runtime, Setup → Model step.

### Проверка
- Сборка (qmlcachegen поймает синтаксис), ручной прогон вкладок настроек,
  полный `ctest` (быстрый регресс).

---

## Этап 2. `VerificationBlocksTab`: фильтрация по группам (M)

### Проблема
`Settings/VerificationBlocksTab.qml:239-294` — три `Repeater` на одной модели
`Verification.blockModel`, каждый инстанцирует **все** типы блоков, 2/3
делегатов постоянно `visible: false` (`group === "content" | "captions" |
"service"`). При ~19 типах — ~57 созданных строк вместо ~19, каждый с
HoverHandler/TapHandler/ToolTip. Разметка `BlockTypeRow` при этом в колонках
полагается на то, что невидимые строки не занимают место — с фильтрованной
моделью это уходит само.

### Рекомендуемое решение: C++ `QSortFilterProxyModel`
QML-only вариант через `DelegateModel` + `filterOnGroup` возможен, но
не тестируется в C++-тестах и требует мутировать группы из делегата.
Проект C++-центричный — берём прокси.

### Шаги
1. Новый класс `BlockGroupFilterModel : QSortFilterProxyModel`
   (`src/app/BlockGroupFilterModel.{h,cpp}`):
   - `Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY groupChanged)`;
   - `filterAcceptsRow`: `sourceModel()->data(idx, GroupRole) == m_group`;
   - роль `GroupRole` уже есть у `VerificationBlocksModel` (роль 3).
2. Экспозиция в QML — в `VerificationPromptStore`
   (`src/app/VerificationPromptStore.{h,cpp}`):
   ```cpp
   Q_PROPERTY(QAbstractListModel* blockModelContent   READ ... CONSTANT)
   Q_PROPERTY(QAbstractListModel* blockModelCaptions  READ ... CONSTANT)
   Q_PROPERTY(QAbstractListModel* blockModelService   READ ... CONSTANT)
   ```
   Три прокси, `setSourceModel(m_model)` в конструкторе store'а. `CONSTANT`
   корректен: источник живёт столько же, сколько store.
3. QML: три `Repeater` получают `model: Verification.blockModelContent` (и
   т.д.), из `BlockTypeRow` уходит `visible: group === ...` и required-свойство
   `group` остаётся (роль по-прежнему доставляется). `loadValues()` →
   `resetFrom()` → `beginResetModel` на источнике корректно сбрасывает прокси.
4. Зарегистрировать файлы в `src/CMakeLists.txt`.
5. Тест: в `tests/test_verification_prompts.cpp` добавить кейсы на прокси:
   разбивка по группам (сумма размеров трёх прокси = `totalCount`), реакция на
   `resetFrom` (прокси переживает reset того же источника), `enabledCount`
   не изменился.
6. **Опционально, той же темой:** устранить двойное состояние
   `VerificationPromptStore::m_blocks` ↔ `VerificationBlocksModel::m_blocks`
   (находка ревью, 62/100): оставить единственным источником копию store'а, а
   модели читать данные через указатель на store, либо наоборот. Минимальный
   вариант — просто задокументировать инвариант «model — редактируемая копия,
   синхронизируется только через rebuildModel()».

### Риски
- Средние. Касается живого UI настроек.
- `DelegateChooser`-подобных переиспользований нет; делегат один и тот же —
  риска деградации нет, только выигрыш.
- Внимание на `Layout.preferredWidth: columnsRow.width / 3` — не меняется.

### Проверка
- `ctest` (включая новый кейс), ручной прогон вкладки: тумблеры, «Select all /
  Deselect all», счётчик «Selected X of Y», сохранение/сброс.

---

## Этап 3. Дедупликация store'ов (M)

### Проблема
`LaunchProfileStore`, `RequestProfileStore` и (третья, слегка иная копия)
`VerificationPromptStore::save` повторяют один и тот же слой персистентности:

- `userPath()` — `RuntimePaths(...).profilesDir()` + роль-суффикс имени файла;
- `reloadUserProfiles()` — одинаковая форма: открыть → распарсить → warn и
  откат к built-in при ошибке;
- `persistUserProfiles()` — почти байт-в-байт совпадает
  (`LaunchProfileStore.cpp:246-329` ↔ `RequestProfileStore.cpp:198-249`):
  удалить файл, если нечего хранить → `mkpath` → `schemaVersion` → `QSaveFile`
  → три одинаковых текста предупреждений;
- `saveDraft()` — одинаковая логика «совпадает с built-in → удалить
  пользовательскую копию, иначе вставить и сохранить»
  (`LaunchProfileStore.cpp:278-293` ↔ `RequestProfileStore.cpp:218-230`).

Любая правка (атомарность, диагностика ошибок — например, свежий фикс
`writeOwnerJson`) должна вноситься трижды; `VerificationPromptStore::save`
уже разошёлся в деталях.

### Шаги — инкрементально, без смены публичного API
1. **3.1 Общая запись.** Новая пара `src/app/ProfileStorage.{h,cpp}`
   (namespace `ProfileStorage`):
   ```cpp
   // Атомарная запись JSON-объекта: mkpath + QSaveFile + commit.
   // Возвращает false + errorString — предупреждение остаётся на store
   // (свой tr()-контекст).
   bool writeJsonAtomic(const QString &path, const QJsonObject &root,
                        QString *error = nullptr);
   // Чтение + парсинг; ok=false при ошибке чтения/парса (текст — в *error).
   QJsonDocument readJson(const QString &path, bool *ok, QString *error = nullptr);
   // Удалить файл, если существует; false при неудаче удаления.
   bool removeIfExists(const QString &path, QString *error = nullptr);
   ```
2. **3.2 Перевод store'ов.** `persistUserProfiles()` в обоих store'ах и
   `save()` в `VerificationPromptStore` строят свой `QJsonObject` (это
   специфично и остаётся на месте), но запись/удаление/чтение идут через
   `ProfileStorage`. Тексты предупреждений — по-прежнему `tr()` каждого
   класса (контекст в `.ts` не меняется, **переводы не переносятся**).
3. **3.3 (опционально) `userPath()`.** Общий хелпер
   `ProfileStorage::userProfilePath(RuntimePaths, role, fileName)`.
4. **Не делать в этом этапе:** объединение самих классов в шаблонную базу
   `ProfileStoreBase<TProfile, TModel>`. Выигрыш от 3.1–3.3 снимает 80%
   дублирования без смены иерархии; шаблонная база с CRTP-моделью усложнит
   отладку ради копейки.
5. Тест: новый `tests/test_profile_storage.cpp` — writeJsonAtomic
   (перезапись, mkpath, отказ commit → false), readJson (битый JSON →
   ok=false), removeIfExists (нет файла → true). Существующие
   `test_launch_profile`, `test_request_profile`, `test_verification_prompts`
   обязаны пройти без правок — они и есть регресс на поведение.

### Риски
- Низкие-средние: код простой, но под ним — профили пользователя; регресс
  ловится тремя существующими тест-наборами.
- Ничего не менять в формате файлов и `schemaVersion`.

### Проверка
- Полный `ctest`; вручную: Settings → Launch/Request (изменить, сохранить,
  «сбросить», перезапуск приложения), Verification → Prompts (сохранить,
  сбросить).

---

## Этап 4. Разбор `ModelInstaller` (L)

### Проблема
`ModelInstaller` (~912 строк) совмещает: пресеты (`reloadPresets*`),
реестр установленных (`installedInfo`, `roleInstalled*`, `setActiveModel`,
`removeModel`, `activatePreset`), HF-prepare в воркере (`beginPrepare`,
`onPrepareDone`), оркестрацию загрузки (`beginDownload`, `enqueueFile`,
`expectedShaFor`, `maybeFinishDownloads`), финализацию установки с записью
реестра (`completeInstall`) и UI-состояние (state/busy/progress/status).
Runtime-сторона того же приложения разделена на `RuntimeInstaller` /
`ReleaseCatalog` / `InstallTransaction` / `ModelRegistry` — модельная сторона
держит всё в одном классе.

### Целевая структура
```
ModelInstaller            — QML-фасад: пресеты, реестр, роль-фильтры, активация,
                            удаление; форвардит состояние транзакции
ModelInstallTransaction   — prepare → download → finalize (новый класс)
ModelRegistry/ModelCatalog — без изменений
```
`ModelInstallTransaction` зеркально повторяет паттерн `InstallTransaction`
(тот же стиль, тот же тест-контур).

### Шаги
1. Новый `src/runtime/ModelInstallTransaction.{h,cpp}`. Перенести из
   `ModelInstaller`: структуру `Pending` (переименовать в `InstallPlan`),
   `m_group`, `m_prepareGeneration`, методы `beginPrepare`, `onPrepareDone`,
   `beginDownload`, `enqueueFile`, `expectedShaFor`,
   `mmprojAlreadyOnDisk`, `maybeFinishDownloads`, `completeInstall`,
   `cancelInstall`-механику.
   - Зависимости передаются ссылками в конструкторе: `SettingsStore&`,
     `RuntimeController&` (для `runSelfTest`-инвариантов, если используются),
     `ModelRegistry&`, `RuntimePaths`.
   - Сигналы транзакции: `stateChanged(int)`, `progressChanged(double)`,
     `statusMessageChanged(QString)`, `installFinished(const ModelEntry&)`,
     `installFailed(const QString&)`.
   - Активацию модели после установки оставить наверху (см. шаг 3).
2. **Отмена и конкурентность — самое опасное место.** Сохранить семантику
   generation-гейта `m_prepareGeneration` (prepare из отменённой/повторной
   операции не должен достраивать install) и правило H.6: `.install.lock`
   держится на уровне транзакции, а не UI-фасада.
3. `ModelInstaller` после переноса:
   - держит `ModelInstallTransaction` как члена;
   - `preparePreset()` / `installPrepared()` / `cancelInstall()` — тонкие
     вызовы транзакции + установка UI-состояния;
   - сигналы транзакции форвардятся в существующие
     `stateChanged/busyChanged/progressChanged/statusMessageChanged` —
     QML-биндинги (`RuntimeTabInternal`, `StepModel`, `LocationTab`,
     `Common/*`) не меняются вообще;
   - `completeInstall`-логика записи в `ModelRegistry` уезжает в транзакцию,
     а «активировать после установки» (`setActiveModel` / `activatePreset`) —
     остаётся в `ModelInstaller` (реестр — его зона).
4. i18n: статусные строки загрузки/установки меняют контекст `tr()` —
   перенести в `llocr_ru.ts` под новый контекст `ModelInstallTransaction`.
5. Тесты:
   - новый `tests/test_model_install_transaction.cpp` по образцу
     `test_install_transaction` + локальный HTTP-mock из `test_model_catalog`:
     happy-path (prepare → download → finalize → `installFinished` с валидным
     `ModelEntry`), sha-mismatch, отмена в каждой фазе (в т.ч. во время
     prepare-воркера — проверка generation-гейта), повторный install поверх
     установленного;
   - `test_model_installer.cpp` — **без правок**: публичный API фасада
     сохранён, это главный регресс;
   - `test_install_lock` — без правок (замки не переехали).

### Риски
- Высокие: конкурентные колбэки, отмены, блокировки. Смягчение — сохранение
  generation-гейта и lock-семантики (шаги 2–3) + два существующих тест-набора
  как защита API.
- Не переносить `DownloadManager`/`DownloadGroup` — они переиспользуются
  как есть.

### Проверка
- Полный `ctest`; вручную: Settings → Models — поиск, prepare, install
  (с отменой на середине), активация, удаление; мастер Setup → Model step;
  проверка `.ts`: новые строки переведены.

---

## Этап 5. Разбор `AppController` (L)

### Проблема
`AppController` (~1200 строк) держит ~8 обязанностей. Два самых крупных
самодостаточных блока (находка ревью, 88/100):

- **Очередь верификации** (`AppController.cpp:640-938`): `VerifyTask`,
  `m_verifyQueue`, `m_verifyPage/BoxIndex/QueueActive/Finished/Total/Done`,
  `startVerifyQueue/startNextVerify/finishVerifyQueue`,
  `applyCheckResultToBox`, `collectEnabledBoxes` + wiring на `CheckController`.
  Это отдельный конечный автомат — готовый `VerificationQueueController`
  (паттерн уже есть: `RecognitionController`/`CheckController`).
- **Экспортный конвейер** (`AppController.cpp:940-1116`): `collectPages`,
  `pdfPageLayout`, рендер через `ExportRenderer`, выбор pandoc/fallback,
  `finalizeRenderedExport`, `finishExport`, флаг `m_exporting`.

### 5a. `VerificationQueueController` (сначала — он проще изолируется)

1. Новый `src/app/VerificationQueueController.{h,cpp}`:
   - переносит: `VerifyTask`, очередь и все `m_verify*`-поля,
     `startVerifyQueue`, `startNextVerify`, `finishVerifyQueue`,
     `applyCheckResultToBox`, `collectEnabledBoxes`;
   - владеет `CheckController` (или получает ссылку — решить по месту;
     владение чище: AppController отдаёт свои ссылки `DocumentModel&`,
     `BoxListModel&`, `VerificationPromptStore&` в конструкторе);
   - сигналы: `progressChanged()`, `finished()`, `statusRequested(QString)`;
   - доступ к данным страниц/боксов — через переданные ссылки + существующие
     модели; `m_documentLock` остаётся в `AppController`, контроллер получает
     `std::function`-обёртки для чтения страницы/бокса (как уже сделано для
     `RecognitionController` через `m_skipPage`).
2. `AppController` сохраняет **весь** публичный API:
   - `checkSelectedBlock / checkEnabledBlocksOnPage / checkAllEnabledBlocks /
     stopCheck` — тонкие форвардеры;
   - свойства `checkBusy / checkRunning / checkFinished / checkProgressDone /
     checkProgressTotal / checkErrorMessage / pageVerificationSupported /
     allPageVerificationSupported` — читают из контроллера, сигнал
     `checkStateChanged` эмитится по `progressChanged/finished` контроллера;
   - `applyCheckResultToBox` остаётся доступен для одиночной проверки
     выбранного блока (форвард).
3. Тесты: `test_app_import.cpp` — регресс E2E (автопроверка после
   распознавания, стоп верификации) **без правок** — главный предохранитель.
   Опционально: focused-тест очереди с подменённым CheckController через
   интерфейс (только если не потребует разрыва типов; без фанатизма).

### 5b. `ExportController`

1. Новый `src/app/ExportController.{h,cpp}`:
   - переносит: `collectPages`, `effectiveText`, `pdfPageLayout`,
     `finalizeRenderedExport`, `finishExport`, `m_exporting`,
     поток `exportPages` (включая ветки pandoc / built-in writer / render
     fallback) и владение `ExportRenderer`;
   - `CropProvider` приходит снаружи: `std::function<QImage(int,int)>`
     от `AppController::croppedImage` (та же техника, что уже используется
     в `Exporter::CropProvider`);
   - сигнал `exportingChanged()` форвардится; статус — `statusRequested`.
2. `AppController::exportPages(...)` — форвард с сохранением сигнатуры
   `(QUrl, int scope, int fromPage, int toPage)`; `exportNameFilters`,
   `exporting()` — делегирование.
3. Учесть: экспорт вызывается при Busy-состояниях — существующие guard'ы
   (`m_recognition.busy() || m_importing`) перенести вместе с кодом, не
   дублировать.
4. Тесты: `test_exporter`, `test_export_renderer` — без правок;
   `test_app_import` (экспорт E2E) — без правок.
5. **Не делать в этом этапе** (отдельно, если понадобится): разнос
   импорта/страниц/preview-кэша, фан-аут `notifyDocumentChanged` (6 сигналов) —
   после 5a/5b AppController уже потеряет ~400 строк и перестанет быть
   критичным.

### Риски
- Высокие: оба блока на критическом пути UI. Смягчение: публичный API
  заморожен, три тест-набора E2E (`test_app_import`, `test_exporter`,
  `test_export_renderer`) обязаны пройти без правок; шаги 5a и 5b —
  **отдельные коммиты**, каждый с полным прогоном тестов.
- Порядок внутри этапа важен: 5a не зависит от 5b; делать 5a первым.

### Проверка
- Полный `ctest` после каждого из 5a/5b; вручную: распознавание → автопроверка
  → проверка выбранного блока → стоп; экспорт всеми форматами (TXT/MD/HTML/
  DOCX/PDF pandoc / PDF built-in), при активной серверной занятости.

---

## Итоговое обновление документации (по проектным правилам)

- `docs/03-architecture.md` — новые классы: `BlockGroupFilterModel`,
  `ProfileStorage`, `ModelInstallTransaction`,
  `VerificationQueueController`, `ExportController`.
- `docs/04-components.md` — разделы про Settings/Models/Verification и
  экспорт (структура изменится).
- `docs/07-glossary.md` — принятые решения (этап 2: прокси вместо
  DelegateModel; этап 3: отказ от шаблонной базы store'ов; этапы 4–5:
  фасад + транзакция/контроллеры).
- `AGENTS.md` — секция Current status.
- `resources/i18n/llocr_ru.ts` — новые контексты
  (`ModelInstallTransaction`, возможно `VerificationQueueController`).

## Оценка объёма

| Этап | Новые файлы | Правки | Оценка |
|------|-------------|--------|--------|
| 1 | 0 | 7 QML | ~1 ч |
| 2 | 2 (C++) + тест | 3 QML, 3 C++ | ~3–4 ч |
| 3 | 2 (C++) + тест | 6 C++ | ~3–4 ч |
| 4 | 2 (C++) + тест | 2 C++ (+.ts) | ~6–10 ч |
| 5a | 2 (C++) | 2–3 C++ | ~4–6 ч |
| 5b | 2 (C++) | 2–3 C++ | ~4–6 ч |
