# LLocr

![Status](https://img.shields.io/badge/status-MVP%20in%20development-orange)
![License](https://img.shields.io/badge/license-GPLv3-blue)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

**Кроссплатформенное приложение для OCR на локальных LLM.**

LLocr сам скачивает, устанавливает и запускает локальный сервер llama.cpp
(или подключается к любому OpenAI-совместимому эндпоинту), распознаёт текст
из изображений, PDF и DjVu, проверяет результат второй моделью и исправляет
третьей — затем экспортирует в Markdown, TXT, HTML, DOCX и PDF. Всё работает
локально: данные не покидают машину.

> English version of this file: [README.md](README.md). Вся документация
> двуязычна: у каждого документа есть зеркало `*.ru.md`.

---

## Возможности

- **Четыре задачи моделей** — разметка страницы (`layout`), OCR блока
  (`blockRecognition`), визуальная проверка (`decision`) и AI-исправление
  (`fix`), у каждой — своя модель, квантизация и профиль запроса
- **Управляемый рантайм** — приложение само скачивает и ставит llama.cpp
  (GitHub releases, проверка sha256, транзакционная установка); модели
  качаются с Hugging Face с закреплёнными ревизиями
- **Конвейер проверки** — каждый блок судит визуально-диффовая модель (кроп
  против отрендеренного кандидата), расхождения уходят модели исправления
  (OK / FIX / REVIEW), а «Перераспознать» перечитывает блок только по
  изображению
- **Инспектор блока** — два текстовых слоя у каждого блока (результат OCR и
  AI-исправление) как две карточки версий с пословной подсветкой различий,
  утверждением и ручным редактированием
- **Ручная разметка** — блоки рисуются мышью или размечает модель; страница
  распознаётся поблочно
- **Пайплайн ориентации** — двухступенчатая попиксельная проверка
  (раскос + ось), автровыпрямление страницы и подсказки поворота от модели
- **Авто-проход** — одна кнопка прогоняет разметка → распознавание →
  проверка → исправление → проверка исправленного по диапазону страниц
- **Ввод PDF, DjVu и изображений**; многостраничные документы с
  переупорядочиванием, удалением и живой лентой миниатюр
- **Редактируемые блоки изображений/графиков** — обнаруженные области можно
  двигать, менять в размере и удалять
- **Живой Markdown-предпросмотр** с рендером формул LaTeX и таблиц
  (Qt WebEngine + marked + KaTeX)
- **Экспорт** в Markdown / TXT / HTML / DOCX (Pandoc) / PDF (Pandoc или
  встроенный писатель) с областью «Всё / Текущая / диапазон»
- **Самодостаточные проекты** — всё рабочее состояние (источники, блоки,
  вердикты, ручные правки) сохраняется в один файл `.llocr` и открывается
  обратно
- **Секреты в хранилище ОС** — API-ключи живут в macOS Keychain, Windows
  Credential Manager или Linux Secret Service, но не в файле настроек
- Локализация (Системная / English / Русский) и темы (Системная / Светлая /
  Тёмная)

## Технологии

| Область | Технологии |
| --- | --- |
| Ядро / UI | Qt6 · C++ · QML |
| Markdown-предпросмотр / экспорт HTML+PDF | Qt WebEngine · marked · KaTeX · DOMPurify |
| Ввод PDF | Qt PDF (`QPdfDocument`) |
| Ввод DjVu (опционально) | DjVuLibre C API |
| OCR-рантайм | llama.cpp (`llama-server`) |
| Экспорт | Pandoc (DOCX/PDF), встроенный HTML-писатель |
| Сборка | CMake (vcpkg участвует только на Windows — ZLIB) |

## Начало работы

### Что нужно

- Qt 6.10 с **Qt PDF**, **Qt WebEngine** (тянет Qt WebChannel и Qt
  Positioning) и **Qt LinguistTools**
- CMake ≥ 3.21 и компилятор C++20 — **MSVC 2022 64-bit на Windows**
  (в MinGW-сборке Qt нет Qt WebEngine)
- vcpkg (только Windows: ZLIB) · Pandoc (опционально: экспорт DOCX/PDF)
- DjVuLibre (опционально: ввод `.djvu`)
- Дистрибутив llama.cpp приложение скачает само при первом запуске

Конкретные шаги для всех трёх платформ: [docs/06-dev-setup.ru.md](docs/06-dev-setup.ru.md).

### Сборка

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/путь/к/Qt/6.10.3/<платформа>
cmake --build build -j
ctest --test-dir build
```

> **Windows:** используйте пакет Qt `msvc2022_64` и компилятор MSVC 2022
> 64-bit; готовое дерево пресета — `build/win-msvc2022`
> (`cmake --preset dev-win`). Перед запуском тестов добавьте Qt DLL в
> `PATH`. См. `docs/06-dev-setup.ru.md`.

## Документация

Вся документация двуязычна: английский основной текст, зеркало `*.ru.md`.

### Приложение

| # | Документ | RU | EN |
| --- | --- | --- | --- |
| 01 | Идея, цели, требования | [01-overview.ru.md](docs/01-overview.ru.md) | [en](docs/01-overview.md) |
| 02 | Технологии и их обоснование | [02-tech-stack.ru.md](docs/02-tech-stack.ru.md) | [en](docs/02-tech-stack.md) |
| 03 | Архитектура, слои, ключевые абстракции | [03-architecture.ru.md](docs/03-architecture.ru.md) | [en](docs/03-architecture.md) |
| 04 | Каталог компонентов | [04-components.ru.md](docs/04-components.ru.md) | [en](docs/04-components.md) |
| 05 | План работ и статус | [05-roadmap.ru.md](docs/05-roadmap.ru.md) | [en](docs/05-roadmap.md) |
| 06 | Инструменты, сборка, CI/CD, дистрибуция | [06-dev-setup.ru.md](docs/06-dev-setup.ru.md) | [en](docs/06-dev-setup.md) |
| 07 | Глоссарий + журнал ADR (все архитектурные решения) | [07-glossary.ru.md](docs/07-glossary.ru.md) | [en](docs/07-glossary.md) |
| 08 | Кроссплатформенная иконка приложения | [08-app-icon.ru.md](docs/08-app-icon.ru.md) | [en](docs/08-app-icon.md) |
| 09 | Четыре задачи и конвейер проверки/исправления | [09-tasks-and-verification.ru.md](docs/09-tasks-and-verification.ru.md) | [en](docs/09-tasks-and-verification.md) |
| 10 | Внешний рантайм: эндпоинты по задачам и секреты | [10-external-runtime.ru.md](docs/10-external-runtime.ru.md) | [en](docs/10-external-runtime.md) |
| 11 | Авто-проход (пять этапов) | [11-auto-pipeline.ru.md](docs/11-auto-pipeline.ru.md) | [en](docs/11-auto-pipeline.md) |
| 12 | Пайплайн исправления ориентации страниц | [12-orientation-pipeline.ru.md](docs/12-orientation-pipeline.ru.md) | [en](docs/12-orientation-pipeline.md) |
| 19 | Тесты и оценочные стенды | [19-testing.ru.md](docs/19-testing.ru.md) | [en](docs/19-testing.md) |

### Мануалы моделей (команда запуска + сборка запроса)

| # | Модель | Задачи | RU | EN |
| --- | --- | --- | --- | --- |
| 13 | MinerU2.5-Pro-2605-1.2B | layout, blockRecognition | [13-model-mineru.ru.md](docs/13-model-mineru.ru.md) | [en](docs/13-model-mineru.md) |
| 14 | GLM-OCR | blockRecognition | [14-model-glm-ocr.ru.md](docs/14-model-glm-ocr.ru.md) | [en](docs/14-model-glm-ocr.md) |
| 15 | Gemma 4 E4B / 12B QAT | fix | [15-model-gemma-4.ru.md](docs/15-model-gemma-4.ru.md) | [en](docs/15-model-gemma-4.md) |
| 16 | Qwen3.5-4B / 9B (исправление по умолчанию) | fix | [16-model-qwen3.5.ru.md](docs/16-model-qwen3.5.ru.md) | [en](docs/16-model-qwen3.5.md) |
| 17 | MiniCPM-OCR Jev (визуальный дифф) | decision | [17-model-minicpm-jev.ru.md](docs/17-model-minicpm-jev.ru.md) | [en](docs/17-model-minicpm-jev.md) |
| 18 | Оценка видеопамяти: формула и калибровка всех моделей | — | [18-memory-estimate.ru.md](docs/18-memory-estimate.ru.md) | [en](docs/18-memory-estimate.md) |

### Архив

Вырезанные из релиза модели, парсеры и мануалы лежат в
[`archive/`](archive/README.md) с манифестом и рецептом восстановления.

## Лицензия

LLocr распространяется под лицензией **GNU General Public License v3.0** —
см. [LICENSE](LICENSE). Сторонние компоненты собраны в
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) и каталоге [`licenses/`](licenses/);
библиотеки Qt подключаются **динамически**, как того требует LGPL v3.
