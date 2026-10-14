# decision-eval — стенд оценки decision-моделей (/v1/systemone) на корпусе OCRtest

Измеряет, насколько уверенно модель разделяет «текст соответствует изображению»
и «не соответствует», и сравнивает варианты формулировки/схемы запроса к
`/v1/systemone`. Результат — `report.md` с рейтингом вариантов, лучшим порогом
и разбивкой по типам блоков.

## Быстрый старт

```sh
cd scripts/decision-eval
python3 -m venv .venv                       # один раз; python ≥ 3.12
./.venv/bin/pip install pymupdf pillow matplotlib

./.venv/bin/python run.py --stage all     # probe -> stage 1 -> stage 2 -> отчёт
./.venv/bin/python run.py --stage dry     # показать состав кейсов без сервера
./.venv/bin/python run.py --stage report  # перерисовать отчёт из собранных строк
./.venv/bin/python run.py --config configs/clef-flash.json --stage 1 --full \
  --block-types equation,table --variants v0_app_current,v5_structured_single \
  --timeout 300                           # прогон другой модели (clef-flash)

# быстрее (4 слота сервера + 4 параллельных запроса; per-slot ctx остаётся 8192):
./.venv/bin/python run.py --stage 1 --server-parallel 4 --concurrency 4 --timeout 300
```

Требуется: корпус `OCRtest/TEST.llocr`, установленный llama.cpp + модель
(пути в `configs/clef-flash.json`), свободный GPU — приложение LLocr и его сервер
должны быть закрыты. Если сервер уже запущен кем-то: `--base-url http://127.0.0.1:PORT`.

## Стадии

- **probe** — запуск llama-server с аргументами приложения, фиксация точного
  формата ответов `noul` / `choice` / `score` (`work/results/probe.json`).
- **stage 1** — стратифицированная подвыборка блоков (все редкие типы + 15
  text + 10 equation) × 5 классов кейсов × 6 вариантов запроса.
- **stage 2** — топ-2 варианта по AUC на всех блоках + ось разрешения
  изображений (`up2x_small`, `down1024`) против `as_is`.
- Прогон резюмируемый: каждый ответ кешируется в `work/cache/` по хешу
  запроса; прерванный запуск продолжает с места остановки.

## Классы кейсов

| класс | ожидание | источник |
|---|---|---|
| `match` | да | исходный текст блока (презюмируется верным) |
| `mismatch/subtle|moderate|gross` | нет | синтетическая порча текста (детерминированная, по uid) |
| `notation` | да | переобозначение, не меняющее содержание (регистр, переносы, пробелы) |
| `sanity_blank` / `sanity_cross` | нет | белый лист вместо кропа / кроп чужого блока |
| `real_ismatch` | нет | блок, помеченный `wrong` в `OCRtest/labels.json` |

`OCRtest/labels.json` создаётся шаблоном при первом запуске: `uid →
"wrong" | "unsure" | ""`.

## Варианты запроса

`v0_app_current` — запрос приложения в его актуальной форме (схема v5:
state-JSON `{"block_type", "recognized_text", "notation"}` + один вопрос;
тексты читаются из профиля `resources/profiles/models/clef-flash.json`,
правка профиля автоматически ребейзлит). Остальные — в `variants.py`:
минимальный вопрос (± criteria), мульти-вопрос, одиночный choice и
v6–v9 — специализированные схемы для формул/таблиц (тип-промпт, таблица
как JSON-ряды, посимвольный вопрос, кроп + рендер LaTeX второй картинкой).
Исторические схемы замеров d1-3B (state-строка и др.) — в git-истории
файла и в `docs/17-d1-verification-study.md`.

Итоги прогонов 13–14.10.2026 (полный корпус 221 блок): d1-3B — AUC v5/v3
0.75–0.76, v0 (state-строка) 0.668 (при пороге 0.6 пропускается ~90 %
ошибок); формулы и таблицы не проверяются никакой формулировкой
(docs/17, §7). clef-flash — та же схема v5 даёт AUC 0.907 на формулах и
0.787 на таблицах (docs/17, §8). Полная статья с методикой и выводами:
`docs/17-d1-verification-study.md`.

## Выход

- `OCRtest/work/results/report.md` — рейтинг, метрики по классам/типам,
  худшие кейсы, разрешение, токены/латентность.
- `OCRtest/work/results/cases.csv` — построчно каждый запрос.
- `OCRtest/work/results/probe.json` — форматы ответов `/v1/systemone`.
- `rows.jsonl` (сырые строки для `--stage report`) пишется при прогоне, в
  репозиторий не входит — отчёт перерисовывается сразу после запуска либо
  восстанавливается из кеша `work/cache`.

## Другая модель

Скопировать конфиг в `configs/<имя>.json`, поменять `server_bin` / `model` /
`mmproj` / `extra_args` (кеш автоматически инвалидируется — в ключ входит
имя и размер файла модели) и при необходимости добавить вариант в
`variants.py`. Транспорт `/v1/systemone` общий для моделей класса System One.
