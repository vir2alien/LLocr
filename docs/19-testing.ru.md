# 19. Тестирование: набор юнит-тестов и оценочные стенды

> Английский оригинал: [docs/19-testing.md](19-testing.md)
>
> Стенды живут в `OCRtest/` (см. также `OCRtest/README.md`).

## 1. Набор юнит-тестов

Все тесты — цели Qt Test в `tests/`, подключённые в `tests/CMakeLists.txt`
и запускаемые через `ctest`. Каждая цель получает действующие на весь
каталог `TIMEOUT 300` и `QT_QPA_PLATFORM=offscreen` (ADR 99); переопределения
на уровне отдельного теста сильнее. Два вспомогательных бинарника
собираются, но не регистрируются: `mock_llama_server` (локальная подмена
llama-server) и `candidate_render_dump` (рендерит кандидатов visual-diff
для стенда decision).

Одна команда запускает всё — configure-if-needed → build → ctest →
clang-format → qmllint (ADR 100):

```sh
scripts/check.sh                 # reuse the configured build/ tree
scripts/check.sh --configure     # re-configure first
```

Windows: дерево пресета `dev-win` (`build/win-msvc2022`, Ninja), собирается
в терминале MSVC x64; Qt DLL должны быть на `PATH` перед `ctest`.
`test_export_renderer` запускает headless Chromium с
`--disable-gpu --no-sandbox --single-process`; на некоторых машинах его
GPU-инициализация прерывается (особенность среды, не регрессия).

### 61 цель, по доменам

| Домен | Цели |
| --- | --- |
| Парсеры | `test_det_parser` |
| Слой config | `test_profile_storage`, `test_secret_store` |
| Слой app (конвейер, очередь, доступность) | `test_runtime_tab_external_qml`, `test_verification_prompts`, `test_auto_pipeline`, `test_verification_queue`, `test_availability_presenter` |
| Модели (адаптеры, decision, fix/read) | `test_ocr_models`, `test_crop_scaling`, `test_pagemodel`, `test_page_edit_store`, `test_box_model`, `test_decision_model`, `test_fix_protocol`, `test_fix_diff_gate`, `test_external_runtime` |
| Документ, страницы, разметка, ориентация | `test_page_status`, `test_layout`, `test_project_store`, `test_document_model`, `test_page_rotation`, `test_page_orientation`, `test_djvu_document`* , `test_app_import` |
| Импорт, настройки, профили | `test_settings_store`, `test_request_profile`, `test_request_budget`, `test_launch_profile`, `test_launch_config` |
| QML (вкладка запуска, таблица задач, мастер) | `test_launch_tab_qml`, `test_task_models_table_qml`, `test_setup_wizard_qml` |
| Установщик рантайма, жизненный цикл | `test_runtime_installer`, `test_runtime_lifetime` |
| Экспорт и рендеринг | `test_exporter`, `test_export_renderer`, `test_candidate_renderer` |
| Транспорт и управляемый сервер | `test_http_client`, `test_capabilities`, `test_runtime_locator`, `test_server_process`, `test_ensure_connection` |
| Загрузки, каталоги, установки, блокировки | `test_model_memory_estimator`, `test_download_manager`, `test_release_catalog`, `test_archive_extractor`, `test_staged_install`, `test_install_transaction`, `test_model_catalog`, `test_model_registry`, `test_hardware_info`, `test_model_installer`, `test_model_install_transaction`, `test_model_preset_catalog`, `test_install_lock` |
| Core (клиент, переходы, diff, latex) | `test_llama_client`, `test_box_transitions`, `test_text_diff`, `test_latex_text`, `test_log_document_sync` |

\* `test_djvu_document` (и DjVu-часть `test_app_import`) собирается только
тогда, когда DjVuLibre найден на этапе конфигурации.

Зафиксированные в репозитории фикстуры: `tests/fixtures/djvu/`
(синтетические DjVu-документы, вместе с генератором),
`tests/fixtures/exporter-preview/` (фальшивый бандл KaTeX),
`tests/fixtures/orientation-corpus/` (рендеры страниц OCR_TEST, которые
потребляет `test_page_orientation` через `LLOCR_CORPUS_RENDERS`; при их
отсутствии тест пропускается). Тестовый класс, который конструирует
`SettingsStore`, должен объявить `TestSettingsIsolation` (см. `AGENTS.md`)
— защитник перенаправляет QSettings во временный профиль.

## 2. Оценочные стенды (`OCRtest/`)

Стенды измеряют то, чего не могут юнит-тесты: качество моделей. Это обычный
Python + конфиги, запускаемые против настоящего llama-server; ничто из
этого не входит в сборку. Основные данные (кропы, изображения страниц,
дампы эталонных текстов, проекты с вердиктами) удалены в очистке
октября 2026 — остались скрипты, конфиги и сводные документы. Общим
корпусом был **OCR_TEST** (10-страничный DjVu-скан технической литературы,
~195 блоков); будущие прогоны будут использовать более крупные корпуса той
же раскладки (см. `OCRtest/tools/README.md`).

| Стенд | Измеряет | Сводный документ |
| --- | --- | --- |
| `ocr_models/` | Качество block-OCR моделей-кандидатов (CER, ячейки таблиц, формулы) против базовых моделей | `ocr_models/REPORT.md` |
| `decision_models/` | P(match) decision-модели visual-diff; пороги по типам блоков | `decision_models/test 2/iteration8/README.md` (ADR 258) |
| `fix_models/` | Протокол исправления: поведение OK/FIX/REVIEW, эксперименты с кандидатом и без кандидата, ось порядка, размышление | `fix_models/README.md`, `FIX_REQUEST_EXPERIMENT.md`, `ORDER_AXIS.md`, `PROBE_SUMMARY.md`, `READ_PROTOCOL.md` |
| `orientation_eval/` | Двухэтапный детектор ориентации и токены поворота модели разметки | `docs/12-orientation-pipeline.md` §7 |
| `tools/` | Переиспользуемые драйверы оценки, перенесённые из находившегося вне репозитория рабочего каталога model-eval | `tools/README.md` |

### Что установили стенды (числа, стоящие за ADR)

- **Block OCR** (`ocr_models/REPORT.md`): поставляемая модель по умолчанию
  (MinerU2.5-Pro) выиграла сравнение на корпусе OCR_TEST; кропы отдельных
  блоков одного корпуса дают каждой модели идентичный вход, а оценка идёт
  против одного и того же эталонного текста.
- **Пороги decision** (ADR 258): текст 0.92, формула 0.87, таблица 0.4,
  база 0.5 — FN 10 (13.7 %) / FP 41 (39.4 %) против FN 29 / FP 15 при 0.5.
  Модель отвергает таблицы целиком (везде p < 0.02 — рендер markdown-сетки
  вне распределения), а сканы печатного текста несут хвост FP, который не
  убирается порогом; остаточные FN — печатные омоглифы и интегралы.
- **Протокол чтения (read)** (`fix_models/READ_PROTOCOL.md`, ADR 259/266):
  чтение без распознанного текста в качестве образца даёт CER 0.015 с
  попаданием в индекс 1.00 (против 0.040/0.84 с образцом) — запрос несёт
  только изображение + промпты.
- **Подача кандидата** (`fix_models/FIX_REQUEST_EXPERIMENT.md`): кандидат
  в запросе исправления — виновник несовпадений у части моделей; протоколы
  без кандидата для отдельных моделей остаются открытым направлением,
  а qwen3.5-9b кандидата сохранять обязан.
- **Ориентация** (ADR 243–246): ось читается правильно на измеренных
  порогах, направление 90°/270° из токенов разметки верно в 14/20 случаев
  (поэтому авто-поворот по умолчанию выключен), а 180° остаётся нечитаемой
  — проекции инвариантны к повороту на пол-оборота.
- **Калибровка памяти** (`docs/18-memory-estimate.md`): формула оценщика
  откалибрована по измеренным серверам для всех девяти поставляемых моделей
  (`scripts/measure-memory.sh`, `scripts/memory-from-log.py`).

### Запуск стенда

Каждый стенд — самодостаточный каталог скриптов; см. его README. Веса
моделей, под которые писались инструменты, остаются вне репозитория
(`Documents/fix-eval-models`). Чтобы проверить работу на новом корпусе,
соберите раскладку корпуса, которую ожидает `OCRtest/tools/ocr_eval.py`
(кропы + эталонный текст + опорные боксы), и направьте константы скрипта
на неё.

## 3. Связанные документы

- `docs/09-tasks-and-verification.md` — что делает тестируемый конвейер.
- `docs/07-glossary.md` — ADR 99 (умолчания для тестов), ADR 100/121
  (скрипт проверки, уровни CI).
- `docs/06-dev-setup.md` — сборка и вспомогательные скрипты разработчика.
