# Модель: Unlimited-OCR

> Ручной запуск, контракт запросов и подключение в приложении. English
> version: `docs/22-unlimited-ocr.md`. Апстрим-справка (ссылки, словарь
> токенов, ручной пример): `docs/UnlimitedOCR.md`.

## 1. Что это

**Unlimited-OCR** — document-parsing VLM от Baidu: один проход по странице
возвращает размеченные блоки с координатами (text, title, table, formula,
image, caption, header/footer, page_number, list, reference, code,
abstract, seal). В LLocr занимает задачу **pageOcr** — распознавание с
разметкой — и является её моделью по умолчанию (ADR 169). Лицензия
Apache-2.0.

## 2. Файлы

| файл | размер | назначение |
|---|---|---|
| `Unlimited-OCR-Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/IQ4_XS) | 2.9 ГБ (Q8_0) | модель |
| `mmproj-Unlimited-OCR-F16.gguf` | ~0.9 ГБ | vision-проектор, **обязателен** |

- Репозиторий: `sahilchachra/Unlimited-OCR-GGUF` (профиль
  `resources/profiles/models/unlimited-ocr.json`); оригинальные веса —
  `baidu/Unlimited-OCR`.
- Без mmproj модель не видит изображение — в ответе деградируют и метки, и
  координаты.

## 3. Запуск

Слои приложения (слой модели профиля добавляет четыре флага поверх
платформенных `n-gpu-layers`/`flash-attn` и политических
`parallel`/`cache-reuse`/`no-context-shift`/`no-warmup`):

```
llama-server \
  --model <modelsDir>/sahilchachra__Unlimited-OCR-GGUF/Unlimited-OCR-Q8_0.gguf \
  --mmproj <modelsDir>/sahilchachra__Unlimited-OCR-GGUF/mmproj-Unlimited-OCR-F16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <авто> \
  --image-min-tokens 456 --image-max-tokens 1156 \
  --dry-sequence-breaker none --special \
  --n-gpu-layers 99 --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--image-min-tokens 456 --image-max-tokens 1156`** — vision-бюджет,
  подобранный под эту модель: широкие страничные раскладки требуют места, а
  штатный минимум (16 токенов) слишком груб для мелкого текста.
- **`--special`** — ответ представляет собой det-токенную разметку, парсеру
  нужны сырые маркеры; сервер по умолчанию вырезает special-токены.
- **`--dry-sequence-breaker none`** — DRY-штраф за повторы (см. §4)
  отслеживает повторы по всему ответу; список breaking-последовательностей
  по умолчанию сбрасывал бы его на каждом переносе строки и позволял
  таблицам зацикливаться.
- ctx-size следует за машиной (ADR 126); в референсном прогоне 16384 с
  `max_tokens` 8192 (`maxOutput` в профиле).

## 4. Контракт запроса

Один chat-completions запрос на страницу: системное сообщение, затем
`[image_url data-URI, text "document parsing."]`. Ответ разбирается вне
модельного адаптера — det-токенным парсером `unlimited-ocr`
(`llocr_parsers`), превращающим строки с метками в позиционированные блоки.

Сэмплирование слоя запроса (список `request` профиля) — антициклический
набор референсного прогона: `temperature 0`, DRY `multiplier 0.8 /
base 1.75 / allowed_length 35 / penalty_last_n 128`, `stream false`.

Карта label → стиль в профиле пустая (`"blocks": {"styles": {}}`):
скомпилированные дефолты парсера уже стилизуют словарь из §1
(перечислен в `docs/UnlimitedOCR.md`).

## 5. Замечания по верификации

Отдельной калибровки для этой модели не проводилось — страницы,
распознанные ею, проходят обычный конвейер decision/fix (docs/18, docs/20).

## 6. Настройки в приложении

- Задача — **pageOcr** (окно «Модели»): ключи `tasks/pageOcr/modelPath`,
  `tasks/pageOcr/mmprojPath`; Unlimited-OCR — дефолтная модель задачи.
- Установка — Settings → Models (репозиторий HF
  `sahilchachra/Unlimited-OCR-GGUF`) или шаг оборудования мастера.
- External-режим: любой OpenAI-совместимый сервер с этой моделью;
  парсерная часть не меняется.

## 7. Известные ограничения

- GGUF-репозиторий — сторонняя упаковка (`sahilchachra`), не сам Baidu;
  происхождение квантования — на совести упаковщика.
- DRY-набор и vision-бюджет подобраны один раз на референсной машине; если
  таблицы всё же зацикливаются, первая ручка — `dry_multiplier`.
- `skip_special_tokens: false` из референсного запроса покрывается
  стартовым флагом `--special` (§3) — не настраивать дважды.
