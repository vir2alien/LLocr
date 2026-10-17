# Модель: LightOnOCR-3-4B

> Ручной запуск, контракт запросов и подключение в приложении. English
> version: `docs/23-lightonocr.md`.

## 1. Что это

**LightOnOCR-3-4B** (LightOn) — VLM, обученная ровно на двух промптах:
`grounding` для страничного разбора с блоками и **пустой промпт** для
транскрипции; всё остальное карточка называет out of distribution. В LLocr
закрывает три задачи, по одной на обученный промпт: **pageOcr**
(grounding), **rawPageOcr** и **blockRecognition** (оба — пустой промпт).
На разметку, верификацию и исправление не претендует. Лицензия Apache-2.0.

## 2. Файлы

| файл | размер | назначение |
|---|---|---|
| `LightOnOCR-3-4B-Q8_0.gguf` (…Q6_K/Q5_K_M/Q4_K_M/Q3_K_M) | 4.2 ГБ (Q8_0) | модель |
| `LightOnOCR-3-4B.mmproj-bf16.gguf` | ~1.3 ГБ | vision-проектор, **обязателен** |

- Репозиторий: `prithivMLmods/LightOnOCR-3-4B-GGUF` (профиль
  `resources/profiles/models/lightonocr.json`); оригинальные веса —
  `lightonai/LightOnOCR-3-4B`.

## 3. Запуск

Слои приложения (слой модели профиля добавляет четыре флага поверх
платформенного и политического слоёв):

```
llama-server \
  --model <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B-Q8_0.gguf \
  --mmproj <modelsDir>/prithivMLmods__LightOnOCR-3-4B-GGUF/LightOnOCR-3-4B.mmproj-bf16.gguf \
  --alias llocr-pageocr \
  --host 127.0.0.1 --port <авто> \
  --flash-attn on --jinja --reasoning-budget 0 --image-min-tokens 1024 \
  --n-gpu-layers 99 --parallel 1 \
  --cache-reuse 0 --no-context-shift --no-warmup
```

- **`--jinja`** — chat-шаблон берётся из метаданных GGUF; транскрипция
  ходит с пустым промптом, grounding — голое слово, и форму ответа держит
  именно шаблон.
- **`--reasoning-budget 0`** — модель обучена с отключённым рассуждением
  (это дефолт её шаблона); без флага ответ может начаться с think-блока.
- **`--flash-attn on`** — требуется vision-стеку (платформенный дефолт).
- **`--image-min-tokens 1024`** — llama.cpp предупреждает, что Qwen-VL
  требует минимум 1024 image-токена для grounding-задач.

У всех трёх ролей набор флагов идентичен — переключение между ними не
перезагружает те же веса.

## 4. Контракт запроса

- **pageOcr** — один chat-запрос на страницу: `[image_url data-URI, text
  "grounding"]`; парсер `lightonocr` (`llocr_parsers`) превращает ответ с
  метками в позиционированные блоки. Профиль стилизует словарь карточки:
  заголовки остаются обычным текстом, chart-блоки становятся таблицами
  данных, подписи/сноски/номера страниц — курсивом.
- **rawPageOcr / blockRecognition** — тот же формат запроса, но текстовая
  часть **не отправляется вовсе** (`OcrModel` опускает пустой промпт):
  страница или кроп блока транскрибируются как есть, `parser: raw`.

Сэмплирование: `temperature 0`, `top_p 0.9`, штрафы нейтральные,
`stream false`, `cache_prompt true` (эмбеддинги изображения
переиспользуются между страницами).

## 5. Замечания по верификации

Отдельной калибровки нет; распознанные страницы проходят обычный конвейер
decision/fix (docs/18, docs/20).

## 6. Настройки в приложении

- Задачи — **pageOcr**, **rawPageOcr**, **blockRecognition** (окно
  «Модели»); дефолтом ни одна не является (unlimited-ocr / glm-ocr
  остаются дефолтами).
- Установка — Settings → Models (репозиторий HF
  `prithivMLmods/LightOnOCR-3-4B-GGUF`) или шаг оборудования мастера.

## 7. Известные ограничения

- **Два промпта, и ничего сверх.** Просить у неё исправить, оценить или
  разметить страницу — out of distribution по формулировке самой карточки;
  профиль сознательно не объявляет эти роли.
- GGUF-репозиторий — сторонняя упаковка (`prithivMLmods`), не сама
  LightOn.
