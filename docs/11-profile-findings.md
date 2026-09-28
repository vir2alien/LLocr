# 11. Findings: побочные баги при разборе профилей

Найдены 28.09.2026 при подготовке [`10-model-profiles.md`](10-model-profiles.md).
Статус: закрыт — правкой, дефолтом или решением «не в этом шаге».

## 🔴 1. `ctx-size` конфликтует в двух файлах и молча уменьшается при установке модели ✅ закрыт

```
defaultLlmPresetsOcr.json : "ctxSize": 8192
serverLaunchOcr.json      : "ctx-size": 16384
```

`ModelInstallTransaction::completeInstall()` и `ModelInstaller::setActiveModel()`
вызывали `setActiveProfileNumber("ctx-size", 8192)`: любая установка модели
переписывала 16384 → 8192 в пользовательской копии launch-профиля, и это
переживало перезапуск. До установки модели контекст был 16384, после — 8192.

Закрыто на шаге 2 плана: `ctx-size` живёт только в профиле модели, а
`setActiveProfileNumber` удалён вместе с обоими вызовами. Конфликт больше не
может воспроизвестись. Значение 16384 — стартовое, окончательное по
результатам тестов.

## 🔴 2. `setParserId` вызывается из двух мест, ADR 96 убрал только одно ✅ закрыт

ADR 96 убрал запись `ModelEntry::parser` в `Settings.parserId` из
`ModelInstaller::setActiveModel()`, но `ModelInstallTransaction::completeInstall()`
её всё ещё делал — активация через пресет и через список установленных давали
разный парсер.

Закрыто на шаге 2: вызов удалён.

## 🟡 3. `ModelEntry::prompt` — мёртвое поле ✅ закрыт

Пишется при установке, сериализуется в `index.json`, но при распознавании не
читался: `RecognitionController::promptText()` брал текст из
`OcrModel::promptVariants()`.

Закрыто на шаге 3: поле удалено из `ModelEntry` и `ModelPreset`, промпт живёт
в профиле модели, и `promptText()` читает его оттуда.

## 🟡 4. `image-min/max-tokens` Unlimited-OCR попадают в проверочную роль ✅ закрыт

В `serverLaunchValidate.json` те же строки были с нейтральными описаниями
«Minimum number of vision tokens per image»: текстовая Qwen3.5 получала
vision-бюджет, рассчитанный под Unlimited-OCR.

Закрыто на шаге 2: параметр живёт в `models/unlimited-ocr.json`, поэтому в
проверочной роли его нет по построению, а не по забывчивости.

## 🟡 5. `minBuild` не проверяется нигде ⬜

Парсится, показывается в `presetInfo()`, но enforcement только глобальный
(`ServerCapabilities::kMinimumSupportedBuild = "b4000"`). Значение `b8000` у
LFM2.5-VL — только для показа: пресет с `minBuild` выше глобального минимума
установить можно, и он будет работать неправильно.

`ModelProfiles::Profile::minBuild` уже читается из профиля модели; проверка на
старте — в шаге 4 вместе с миграцией настроек.

## 🟡 6. Список ресурсов профилей продублирован в шести местах CMake ✅ закрыт

Один и тот же набор JSON перечислен в `src/CMakeLists.txt` и в пяти блоках
`tests/CMakeLists.txt`.

Закрыто на шаге 3: список профилей моделей объявлен один раз
(`LLOCR_MODEL_PROFILE_FILES` в `tests/CMakeLists.txt`, glob в
`src/CMakeLists.txt`) — новая модель это новый файл, а не правка шести мест.

## 🟡 7. `alias` один на обе роли ⬜

`ServerLaunchConfig::fromSettings` берёт `launchModelAlias()` вне ветки
`ConnectionRole::Check` — проверочный сервер поднимается с тем же alias, что и
OCR. `alias` уже есть в `ModelProfiles::Role`, но пока не используется.

Правило для шага 4: alias из профиля модели как значение по умолчанию, без
per-role настройки — тогда переключение роли не сможет оставить старый alias.

## 🟢 8. Идентификаторы пресетов несогласованы ⬜

В одном массиве `defaultLlmPresetsOcr.json` лежат `unlimited-ocr-q8_0` и
`unlimited-ocr-q4_k_m.gguf` — один с расширением, другой без. Плюс
`qwen3.5-4b-q4_k_xl` без `.gguf`. Уходит при переходе на `files.quants[].id`
в шаге 4.

## 🟢 9. `n-predict` и `max_tokens` дублируют один предел вывода ✅ закрыт

`n-predict: 8192` и `max_tokens: 8192` — одно и то же число в двух файлах.
`max_tokens: 8192` при `n-predict: 4096` даёт тихую обрезку ответа без ошибки.

Закрыто на шаге 3: `maxOutput` в профиле модели — единственный источник;
`n-predict` берётся из launch-параметров, `max_tokens` подставляется из
`maxOutput` в `ModelProfiles::requestWithMaxOutput()`. В JSON `max_tokens` не
пишется вовсе, разойтись он больше не может.

## 🟢 10. `estimateModelMemory` считает только для OCR-роли ✅ закрыт

Читал `ctx-size` и `cache-type-*` из launch-профиля OCR-роли независимо от
оцениваемой модели; вызывался только с `Settings.launchModelPath`.

Закрыто на шаге 2: сигнатура `estimateModelMemory(modelPath, forCheck)`, роль
выбирает профиль модели.

## 🟢 11. `dry_sequence_breakers: ["\uE000"]` — хак в request-параметрах ✅ закрыт

`requestOcr.json`, order 5. Private-use символ U+E000, который никогда не
совпадёт, — способ заставить сервер принять непустой массив. Прямое следствие:
`dry-sequence-breaker` на уровне сервера и `dry_sequence_breakers` в API — два
места одного и того же без проверки согласованности.

Закрыто на шаге 3: хак удалён вместе с `requestOcr.json`; уровень сервера
(`dry-sequence-breaker` в `roles.ocr.launch` профиля) покрывает задачу.
