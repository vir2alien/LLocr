# 11. Findings: побочные баги при разборе профилей

Найдены 28.09.2026 при подготовке [`10-model-profiles.md`](10-model-profiles.md).
Каждый пункт правится независимо от рефакторинга, если не указано иное.

## 🔴 1. `ctx-size` конфликтует в двух файлах и молча уменьшается при установке модели

```
defaultLlmPresetsOcr.json : "ctxSize": 8192
serverLaunchOcr.json      : "ctx-size": 16384
```

`ModelInstallTransaction::completeInstall()` (`ModelInstallTransaction.cpp:581-582`)
и `ModelInstaller::setActiveModel()` (`ModelInstaller.cpp:203-204`) вызывают
`setActiveProfileNumber("ctx-size", 8192)`. Любая установка модели переписывает
16384 → 8192 в пользовательской копии launch-профиля, и это переживает
перезапуск.

Итог: до установки модели контекст 16384, после — 8192, в зависимости от того,
что произошло раньше. Какое из двух чисел правильное, в проекте нигде не
записано.

Решается на шаге 2 плана. Значение 16384 принято как стартовое, окончательное —
по результатам тестов.

## 🔴 2. `setParserId` вызывается из двух мест, ADR 96 убрал только одно

ADR 96 убрал запись `ModelEntry::parser` в `Settings.parserId` из
`ModelInstaller::setActiveModel()`. Но
`ModelInstallTransaction::completeInstall()` (`ModelInstallTransaction.cpp:577-582`)
её всё ещё делает.

Активация через пресет и через список установленных дают разный парсер. Убрать
вызов.

## 🟡 3. `ModelEntry::prompt` — мёртвое поле

Пишется при установке (`ModelInstallTransaction.cpp:514`), сериализуется в
`index.json` (`ModelRegistry.cpp:82, 124`), но при распознавании не читается:
`RecognitionController::promptText()` (`RecognitionController.cpp:55-61`) берёт
текст из `OcrModel::promptVariants()`.

Дубликат источника истины, причём рабочий — в коде. Удаляется на шаге 3. Перед
удалением убедиться полным grep по `ModelEntry` в `src/` и `tests/`, что чтений
нет.

## 🟡 4. `image-min/max-tokens` Unlimited-OCR попадают в проверочную роль

В `serverLaunchValidate.json` те же строки присутствуют с нейтральными
описаниями «Minimum number of vision tokens per image». Текстовая Qwen3.5
получает vision-бюджет, рассчитанный под Unlimited-OCR.

Уходит само при переносе в профиль модели (шаг 2). До того — удалить из
`serverLaunchValidate.json` вручную.

## 🟡 5. `minBuild` не проверяется нигде

Парсится (`ModelPreset.cpp:16`), показывается в `presetInfo()`
(`ModelInstaller.cpp:334`), но enforcement только глобальный:
`ServerCapabilities::kMinimumSupportedBuild = "b4000"` (`ServerCapabilities.h`).
Значение `b8000` у LFM2.5-VL — только для показа.

Пресет с `minBuild` выше глобального минимума установить можно, и он будет
работать неправильно. Проверка — на старте, при активации модели.

## 🟡 6. Список ресурсов профилей продублирован в шести местах CMake

Один и тот же набор JSON перечислен в `src/CMakeLists.txt:353-366` и в пяти
блоках `tests/CMakeLists.txt` (`test_det_parser`, `test_verification_prompts`,
`test_app_import`, `test_model_installer`, `test_model_preset_catalog`).

Добавление или переименование файла = правка шести мест. Если идём на «файл на
модель» (шаг 2) — перевести на glob по `resources/profiles/*.json` + тест, что
каждый файл парсится и id уникальны.

## 🟡 7. `alias` один на обе роли

`ServerLaunchConfig::fromSettings()` (`ServerLaunchConfig.cpp:22`) берёт
`launchModelAlias()` вне ветки `if (role == ConnectionRole:: Check)` —
проверочный сервер поднимается с тем же alias, что и OCR. С разными alias в
профилях нужно либо per-role настройку, либо брать alias из активного профиля
модели и не хранить его вовсе.

## 🟢 8. Идентификаторы пресетов несогласованы

В одном массиве `defaultLlmPresetsOcr.json` лежат `unlimited-ocr-q8_0` и
`unlimited-ocr-q4_k_m.gguf` — один с расширением, другой без. Плюс
`qwen3.5-4b-q4_k_xl` без `.gguf`. Уходит при переходе на `files.quants[].id`.

## 🟢 9. `n-predict` и `max_tokens` дублируют один предел вывода

`n-predict: 8192` (`serverLaunchOcr.json`) и `max_tokens: 8192`
(`requestOcr.json`) — одно и то же число в двух файлах, редактируется
независимо. `max_tokens: 8192` при `n-predict: 4096` даёт тихую обрезку ответа
без ошибки. Схлопывается в `maxOutput` (шаг 2).

## 🟢 10. `estimateModelMemory` считает только для OCR-роли

`RuntimeController::estimateModelMemory()` (`RuntimeController.cpp:822-848`)
читает `ctx-size` и `cache-type-*` из `m_launchProfiles.activeProfile()` —
launch-профиля OCR-роли, независимо от того, какая модель оценивается.
Вызывается из `StepLaunch.qml:33` только с `Settings.launchModelPath`. Для
check-модели оценки нет.

## 🟢 11. `dry_sequence_breakers: ["\uE000"]` — хак в request-параметрах

`requestOcr.json`, order 5. Private-use символ U+E000, который никогда не
совпадёт, — способ заставить сервер принять непустой массив. Прямое следствие:
`dry-sequence-breaker` на уровне сервера (`serverLaunchOcr.json`, order 14) и
`dry_sequence_breakers` в API — два места одного и того же, без проверки
согласованности. По решению №2 плана хак убирается, остаётся серверный флаг.
