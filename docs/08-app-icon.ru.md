# 08. Значок приложения (кроссплатформенный)

> Английский оригинал: [docs/08-app-icon.md](08-app-icon.md)

Этот документ описывает, как предоставляется значок приложения LLocr и как
подготовить/перегенерировать требуемые ресурсы для каждой операционной
системы.

## Статус

Значок **подключён** для всех трёх целевых ОС:

| ОС       | Что используется                                 | Куда попадает                  |
| -------- | ------------------------------------------------ | ------------------------------ |
| Windows  | `resources/icons/llocr.ico`                      | встроен как ресурс исполняемого файла (`llocr.rc`) |
| macOS    | `resources/icons/llocr.icns`                     | `.app/Contents/Resources/llocr.icns` (+ `Info.plist`) |
| Linux    | `resources/icons/hicolor/**/llocr.png`           | установка в тему значков freedesktop `hicolor` (+ `.desktop`) |

Каждая платформа также получает **значок окна/панели задач в рантайме** из
встроенных PNG нескольких размеров через `QGuiApplication::setWindowIcon()`
(см. `src/main.cpp`).

## Исходный ресурс

Авторитетный векторный источник — `resources/icons/app-icon.svg`. Все
растровые форматы генерируются из него. После добавления значка `xmlns` был
исправлен на стандартное пространство имён SVG (`http://www.w3.org/2000/svg`).

Сгенерированные растровые ресурсы (зафиксированы в репозитории, чтобы
сборке / CI не требовались инструменты-конвертеры):

- `resources/icons/llocr-*.png` — RGBA PNG нескольких размеров для значка
  окна (16, 24, 32, 48, 64, 128, 256, 512, 1024).
- `resources/icons/llocr.ico` — значок Windows (сжатые PNG-записи, Vista+:
  16/24/32/48/64/128/256).
- `resources/icons/llocr.icns` — контейнер значков macOS (PNG-записи для
  16/32/64/128/256/512/1024).
- `resources/icons/hicolor/<size>/apps/llocr.png` — набор для темы значков
  freedesktop.

## Как применяется значок в рантайме

В `src/main.cpp` собирается `QIcon` — по одному файлу на размер — и
передаётся в `app.setWindowIcon(...)`. Qt затем выбирает самый чёткий
подходящий размер для оконной системы на каждой платформе (панель задач,
Alt-tab, оконный менеджер). PNG встроены через ресурс `app_icons`
(`:/icons/llocr-*.png`).

## macOS

**Разработка (сборка по умолчанию).** Значок окна задаётся в рантайме.
Окна macOS обычно не рисуют значок в строке заголовка, поэтому при
разработке значок окна виден только там, куда Qt его выводит.

**Настоящий бандл .app (Dock + Finder).** Чтобы значок появился в Dock и
Finder, соберите приложение как бандл macOS с опцией
`LLOCR_MACOS_APP_BUNDLE`:

```sh
cmake -S . -B build-bundle -G "Unix Makefiles" \
  -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/macos \
  -DLLOCR_MACOS_APP_BUNDLE=ON
cmake --build build-bundle -j 8
```

Результат — `build-bundle/bin/llocr.app` с:

- `Contents/MacOS/llocr`
- `Contents/Resources/llocr.icns`
- `Contents/Info.plist`, содержащим `CFBundleIconFile` = `llocr.icns`

Запускайте через `open build-bundle/bin/llocr.app` (или `.../MacOS/llocr`).

> Опция по умолчанию **выключена**, чтобы существующий рабочий процесс
> разработки `build/bin/llocr` не менялся. Включайте её, только когда нужен
> распространяемый `.app`.

Для установщика (подписанный/нотаризованный `.dmg` — более поздний шаг
выпуска) запустите `macdeployqt`/`macdeployqt6` от Qt на `.app`; `.icns`
уже лежит в папке `Resources` бандла.

## Windows

Ico встраивается в исполняемый файл через Windows-ресурс `.rc`:

```
IDI_ICON1 ICON "llocr.ico"
```

Когда `src/CMakeLists.txt` конфигурируется на `WIN32`, `llocr.rc`
добавляется к исходникам цели, и компилятор ресурсов (`rc.exe` из MSVC
в наборе MSVC2022 64-bit) встраивает его. Explorer затем показывает значок
у `.exe`. (MinGW-альтернатива `windres` не используется: LLocr собирает
Windows компилятором MSVC 2022 64-bit — ADR 54.) Если компилятор ресурсов
Windows не может разрешить `llocr.ico` (некоторые сборки MSVC `rc`
разрешают относительный путь от рабочего каталога), замените `"llocr.ico"`
в `llocr.rc` на полный путь к `resources/icons/llocr.ico`.

## Linux

Соединяются две части:

1. **Значок рантайма/лаунчера.** PNG из hicolor устанавливаются в тему
   значков freedesktop `hicolor`, чтобы GNOME/KDE и другие среды, запускающие
   приложения через `.desktop`, находили подходящий размер:

   ```cmake
   install(FILES resources/icons/hicolor/<size>/apps/llocr.png
           DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/<size>/apps)
   ```

   а шаблон записи `.desktop` `resources/icons/llocr.desktop.in`
   прогоняется через `configure_file` (с подстановкой `@LLOCR_EXECUTABLE@`)
   в `${CMAKE_INSTALL_DATADIR}/applications/llocr.desktop`.

2. **Ручная альтернатива.** Если вы не используете `cmake --install`,
   скопируйте те же файлы сами:

   ```sh
   install -d /usr/share/applications
   # for each size dir under resources/icons/hicolor:
   install resources/icons/hicolor/<size>/apps/llocr.png \
       /usr/share/icons/hicolor/<size>/apps/
   ```

   Если значок freedesktop не подхватывается (например, потому что рабочий
   стол кэширует темы значков), положите `llocr.png` рядом с бинарником
   или укажите в `Icon=` файла `llocr.desktop` абсолютный путь к PNG.

## Перегенерация растровых ресурсов

Зафиксированные в репозитории ресурсы генерируются из
`resources/icons/app-icon.svg` скриптами в `tools/`:

- `tools/rasterize_icon/main.cpp` — крошечная Qt-программа, которая рендерит
  SVG в PNG через `QSvgRenderer` (QtSvg). Соберите/запустите её против
  установленных фреймворков Qt, чтобы получить файлы `icon-<N>.png`.
- `tools/make_ico.py` — собирает `llocr.ico` из PNG-записей (Python stdlib).
- `tools/make_icns.py` — собирает `llocr.icns` из PNG-записей (Python
  stdlib).

Они нужны, только когда меняется дизайн значка; в обычный `cmake --build`
они не входят (уже отрендеренные файлы PNG/ICO/ICNS зафиксированы в
репозитории).
