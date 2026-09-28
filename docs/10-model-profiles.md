# 10. Model profiles: one file per model

> План перестройки организации профилей. Написан 28.09.2026.
> Побочные баги, найденные при разборе, вынесены в
> [`11-profile-findings.md`](11-profile-findings.md) — их чинят независимо.

## 1. Зачем

Параметры одной модели разложены по восьми JSON-файлам и по трём
независимым id-пространствам:

| Что | Ключ | Файл |
| --- | --- | --- |
| Пресеты установки | `unlimited-ocr-q8_0` (на **квант**) | `resources/profiles/defaultLlmPresetsOcr.json`, `…Validate.json` |
| Параметры запроса | `unlimited-ocr` (на **рецепт**) | `resources/profiles/requestOcr.json`, `…Validate.json` |
| Параметры запуска | `macos-metal` (на **os+backend**) | `resources/profiles/serverLaunchOcr.json`, `…Validate.json` |
| Словарь меток | `lfm25-vl-3b` (на **рецепт**) | `resources/profiles/labels.json` → `overrides` |
| Промпты, парсер | `unlimited-ocr` | C++, `OcrModelFactory` |

Добавление модели требует правки пяти мест в четырёх форматах, и они молча
расходятся при неполном изменении — см. `11-profile-findings.md`, пункты 1–4.

## 2. Ключевое решение: слои не пересекаются

Отдельный набор «базовых» launch-параметров, общих для всех моделей, **не
работает**: у LFM2.5-VL нет ни `dry-*`, ни `image-min/max-tokens`, а контекст и
предел вывода свои. Набор параметров — свойство модели, а не класса моделей.

Поэтому слои **не пересекаются**, и override-семантика между ними не нужна.
Сборка — объединение непересекающихся множеств:

```
launch args = platform ∪ policy ∪ model ∪ user
```

`image-min-tokens` отсутствует у LFM не потому, что модель перебила значение
пустым, а потому что его нет ни в одном слое для этой модели. Сегодня это
выразить нельзя: значение наследуется, и «у меня LFM, а тут 456» получается
только ручным удалением строки.

Пользовательский слой — единственный, у кого есть override, и он по
определению сверху.

### 2.1. Три слоя и их содержимое

**`platform`** — свойство железа, зависит от `os` + `backend`:
`n-gpu-layers`, `flash-attn`.

**`policy`** — способ работы сервера, не зависит ни от модели, ни от платформы:
`parallel`, `cache-reuse`, `no-context-shift`, `no-warmup`.

**`model`** — свойство весов, живёт в `models/<id>.json` → `roles.<role>.launch`:
`ctx-size`, `n-predict`, `cache-type-k`, `cache-type-v`,
`image-min-tokens`, `image-max-tokens`, `dry-sequence-breaker`, `special`.

Из 14 параметров нынешнего `serverLaunchOcr.json` в слое «платформа» сегодня
лежат 12; после переноса там останется 2.

### 2.2. Роль из этого слоя уходит

`serverLaunchOcr.json` и `serverLaunchValidate.json` содержат одни и те же
шесть id (`macos-metal`, `win-cuda`, `win-vulkan`, `linux-cuda`,
`linux-vulkan`, `cpu`) и отличаются только параметрами — а отличаются теми
самыми, которые принадлежат модели. Разница 14 против 13 строк — это ровно
`dry-sequence-breaker`.

После переноса остаётся **один** файл с 6 hardware-профилями по 2 параметра.
`serverLaunchValidate.json` удаляется.

## 3. Решения, принятые по ходу обсуждения

1. **`ctx-size` — свойство модели, значение по умолчанию 16384.** Зависит и от
   модели, и от железа: у пользователя может не поместиться модель с таким
   контекстом, и он захочет уменьшить. Уменьшение — точечная правка
   пользовательского слоя, остальные параметры модели не дублируются.
   Окончательное значение — по результатам тестов.
2. **`dry-sequence-breaker` остаётся в launch-параметрах модели. Из
   request-параметров убирается** хак `dry_sequence_breakers: ["\uE000"]` —
   private-use символ, который никогда не совпадёт. С ними могут быть
   проблемы. Уровень сервера покрывает задачу.
3. **`image-min/max-tokens` — опциональные поля модели.** Актуальны для
   Unlimited-OCR; у LFM2.5-VL в финальной реализации их не будет; у других
   моделей могут появиться. Отсутствие = параметр не передаётся.

## 4. Целевая раскладка файлов

```
resources/profiles/
  serverLaunch.json        ← platform (os+backend) + policy, не пересекаются
  models/
    unlimited-ocr.json     ← один файл на семейство моделей
    lfm25-vl-3b.json
    qwen3.5-4b.json
  labels.json              ← базовый словарь label → style (без overrides)
  verifyPrompts.json       ← без изменений
```

`labels.json` теряет секцию `overrides` — она переезжает в
`roles.<role>.blocks.styles` профиля модели.

## 5. Формат профиля модели

`resources/profiles/models/unlimited-ocr.json`:

```jsonc
{
  "schemaVersion": 1,
  "id": "unlimited-ocr",
  "title": "Unlimited-OCR",
  "license": "https://huggingface.co/sahilchachra/Unlimited-OCR-GGUF",
  "minBuild": "b4000",

  "files": {
    "repo": "sahilchachra/Unlimited-OCR-GGUF",
    "revision": "",
    "quants": [
      { "id": "q8_0",   "file": "Unlimited-OCR-Q8_0.gguf",   "sha256": "" },
      { "id": "q4_k_m", "file": "Unlimited-OCR-Q4_K_M.gguf", "sha256": "" }
    ],
    "mmproj": { "id": "f16", "file": "mmproj-Unlimited-OCR-F16.gguf", "sha256": "" }
  },

  "roles": {
    "ocr": {
      "alias": "llocr-unlimited-ocr",
      "parser": "det_tokens",
      "maxOutput": 8192,
      "prompts": [
        { "id": "document-parsing", "title": "Document parsing", "text": "document parsing." }
      ],
      "blocks": { "styles": {} },
      "launch": [
        { "order": 1, "name": "ctx-size",             "value": 16384 },
        { "order": 2, "name": "cache-type-k",         "value": "f32" },
        { "order": 3, "name": "cache-type-v",         "value": "f32" },
        { "order": 4, "name": "image-min-tokens",     "value": 456 },
        { "order": 5, "name": "image-max-tokens",     "value": 1156 },
        { "order": 6, "name": "dry-sequence-breaker", "value": "none" },
        { "order": 7, "name": "special" }
      ],
      "request": [
        { "order": 1, "name": "temperature",        "value": 0.0 },
        { "order": 2, "name": "dry_multiplier",     "value": 0.8 },
        { "order": 3, "name": "dry_base",           "value": 1.75 },
        { "order": 4, "name": "dry_allowed_length", "value": 35 },
        { "order": 5, "name": "dry_penalty_last_n", "value": 2048 },
        { "order": 6, "name": "max_tokens",         "value": 8192 },
        { "order": 7, "name": "stream",             "value": false }
      ]
    }
  }
}
```

`roles` — keyed-объект, а не отдельный файл на роль и не список строк:

- список загрузки (`files`) общий для обеих ролей; два файла разъедутся по
  `sha256`, а он используется для проверки при скачивании;
- `ModelEntry` описывает **установленный файл**, а не роль: `roles` уже
  хранится списком в одной записи реестра (ADR 80), и повторная установка из
  другого окна мержит роли, а не создаёт вторую запись;
- keyed-объект расширяется новым типом роли без изменения схемы.

### 5.1. `maxOutput` вместо пары `n-predict` + `max_tokens`

Один и тот же предел вывода сейчас выражен дважды и редактируется независимо:
`n-predict: 8192` в launch-профиле и `max_tokens: 8192` в request-профиле.
Расхождение (`max_tokens: 8192` при `n-predict: 4096`) даёт **тихую обрезку**
ответа без ошибки. В профиле это одно число, расходящееся в оба места.

### 5.2. Fallback для моделей вне каталога

Модель может быть не из каталога: ручной путь, External-сервер, свой GGUF.
Тогда `ctx-size` неоткуда взять, и llama-server подставит свой дефолт —
**4096**, то есть катастрофа для страницы с таблицей.

Нужен явный резервный набор и предупреждение в UI. Это не «единый набор для
всех», а последний рубеж, применяемый только когда профиль модели не найден:

```jsonc
"fallback": { "ctx-size": 16384, "n-predict": 8192 }
```

UI: «Модель не из каталога — параметры запуска взяты из резервного набора».

## 6. Профиль без роли: `roles.check` без `prompts`

Промпты проверки одинаковы для всех check-моделей, поэтому
`verifyPrompts.json` не трогаем. В профиле модели секция `prompts` при
`roles.check` отсутствует, источником остаётся `VerificationPromptStore`. Точка
переопределения на будущее — `roles.check.promptsRef`.

## 7. Что меняется в коде

| Место | Изменение |
| --- | --- |
| `LaunchProfileStore` | разбирать `platforms` + `policy`; роль из store уходит |
| `LaunchProfileStore::setActiveProfileNumber` | удалить — `ctx-size` больше не переписывается при установке модели |
| `ModelInstaller::setActiveModel` | убрать вызов `setActiveProfileNumber` (находка 1) |
| `ModelInstallTransaction::completeInstall` | убрать `setActiveProfileNumber` и `setParserId` (находки 1, 2) |
| `ServerLaunchConfig::fromSettings` | принимать `role`, брать alias из профиля модели; сейчас `launch/modelAlias` один на обе роли (`ServerLaunchConfig.cpp:22`) |
| `RuntimeController::estimateModelMemory` | читать `ctx-size`/`cache-type` из **собранного** набора, а не из `activeProfile()`; заодно перестать быть только для OCR-роли |
| `LaunchTab.qml`, `StepLaunch.qml` | resolved-список с пометкой источника строки; правка пишет в пользовательский слой |
| `OcrModelFactory` | `promptVariants()` и `defaultParserId()` уходят в профиль; класс остаётся для модели с другой формой ответа (ADR 89) |
| `ModelPreset` / `ModelEntry` | `ctxSize` и `prompt` поглощаются профилем; `parser` — дефолт, не принуждение (ADR 96) |
| `BlockStyleMap` | `overrides` из `labels.json` → `blocks.styles` профиля |
| `main.cpp` | `serverLaunchValidate.json` и `LaunchProfilesValidate` удаляются |
| `src/CMakeLists.txt`, `tests/CMakeLists.txt` | glob по `resources/profiles/*.json` вместо ручного списка (находка 6) |

## 8. Порядок работ

Поправка после начала реализации: **шаги 1 и 2 не независимы.** Набор
launch-параметров становится правильным только когда существуют все три слоя.
Пока `ctx-size` / `image-*` не уедут в профиль модели, их нельзя убрать из
платформенного слоя — OCR-модель их потеряет, а у проверочной роли они
останутся. Поэтому шаг 1 — сокращение двух файлов до одного без переноса
параметров модели; находка #4 закрывается на шаге 2.

### Шаг 1. Один файл, две непересекающиеся секции ✅

- `serverLaunch.json`: секция `policy` (не зависит ни от модели, ни от
  платформы) и секция `profiles` (платформенный слой). Ключ `profiles`
  сохраняется, чтобы формат пользовательской копии не менялся.
- Удалены `serverLaunchOcr.json`, `serverLaunchValidate.json`.
- `LaunchProfileStore` без роли; `LaunchProfilesValidate` удалён.
- `RuntimeController` теряет второй store; обе роли используют один набор.
- `Settings.check/launchProfileId` удалён по образцу ADR 117 (`launch/presetId`).
- Пользовательская копия: один файл `serverLaunch.json`. Если его нет, но есть
  `serverLaunchValidate.json`, он принимается как начальное значение — иначе
  правки пользователя потерялись бы молча.
- Строки `policy` в UI не редактируются (роль `editable` в
  `LaunchParametersModel`): иначе пользователь удалил бы строку, а `saveDraft`
  её молча потерял бы. Причина: при конкатенации слоёв `order` нумеруется
  независимо, сортировка перемешивала слои, и заблокированный префикс
  переставал быть префиксом — `compose()` перенумеровывает порядок.

### Шаг 2. Launch-параметры в профиль модели ✅ (частично)

- `resources/profiles/models/<id>.json` — по файлу на семейство, с
  `roles.<role>.launch` и `fallback`. Загрузчик `ModelProfiles`
  (`config/`) обходит каталог, CMake подхватывает файлы glob'ом.
- Сборка `policy ∪ platform ∪ model`; при совпадении имени побеждает модель.
  Каждый слой непересекающийся по составу, так что подстановка — скорее
  страховка на случай, если кто-то впишет модельный параметр в
  `serverLaunch.json`.
- `activeProfile(modelId, role)` — новая сигнатура; `ServerLaunchConfig::
  fromSettings` передаёт роль и активную модель.
- `setActiveProfileNumber` удалён вместе с обоими вызовами — **находка #1
  закрыта**: конфликт `ctx-size` 8192 против 16384 больше не может
  воспроизвестись, значение приходит из профиля модели.
- `setParserId` из `completeInstall()` убран — **находка #2 закрыта**.
- `estimateModelMemory(modelPath, forCheck)` — **находка #10 закрыта**.
- `ModelInstaller` и `ModelInstallTransaction` больше не зависят от
  `LaunchProfileStore` (единственная цель была `setActiveProfileNumber`).
- **Находка #4 закрыта**: `image-min/max-tokens` есть только у Unlimited-OCR;
  в проверочной роли их нет по построению, а не по забывчивости.
- **Находка #9 закрыта наполовину**: `maxOutput` в профиле — единственный
  источник `n-predict`; `max_tokens` в request-профиле пока независим
  (расходится в шаге 3, вместе с переносом request-параметров).

Проверено на данных: Unlimited-OCR даёт 14 параметров, LFM2.5-VL — 11,
разница ровно в `image-min-tokens`, `image-max-tokens` и
`dry-sequence-breaker`.

### Шаг 3. Промпты и словарь блоков

- Промпты в `roles.<role>.prompts`, словарь — в `roles.<role>.blocks`.
- Удаление `OcrModel::promptVariants()` и переопределений в
  `UnlimitedOcrModel` / `Lfm25VlModel`; фабрика отдаёт generic-адаптер.
- Удаление мёртвого `ModelEntry::prompt` (находка 3).
- LFM-layout промпт переезжает из `Lfm25VlModel.cpp:10-30` в JSON — чтобы его
  можно было вычитать в PR.

### Шаг 4. Роли, alias, миграция настроек

- `defaultLlmPresetsValidate.json` → `roles.check`; один каталог, фильтр по роли.
- `alias` в профиль; per-role настройка либо отказ от хранения alias.
- `model/recipeId` + `model/requestProfileId` + `check/requestProfileId` +
  `launch/profileId` + `check/launchProfileId` → `ocr/profileId` +
  `check/profileId`. Миграция в `applyStartupMigration()` — осторожно, ADR 83
  уже показал, как неаккуратная запись стирает настройки.
- Проверка `minBuild` на старте (находка 5).

## 9. Чего рефакторинг не даёт

- **Форму ответа.** Модель не с det-token-диалектом требует C++-адаптера;
  профиль её не заменит (ADR 89).
- **Внешние серверы.** `alias` в профиле — значение по умолчанию для `--alias`
  в Managed. В External имя приходит от сервера, настройка остаётся
  переопределяемой.
- **Проверочные промпты.** `verifyPrompts.json` остаётся общим.

## 10. ADR, которые нужно будет записать

- Разделение launch-параметров на `platform` / `policy` / `model` без
  override-семантики.
- Профиль модели как единица каталога; `roles` как keyed-объект.
- `maxOutput` как единственный предел вывода.
- `fallback` для моделей вне каталога.
- Значение `ctx-size` по умолчанию и его зависимость от железа.
