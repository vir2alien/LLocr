# 08. Application Icon (cross-platform)

This document explains how the LLocr application icon is provided and how to
produce/regenerate the required assets for each operating system.

## Status

The icon is **wired in** for all three target OSes:

| OS       | What is used                                    | Where it lands                  |
| -------- | ----------------------------------------------- | ------------------------------- |
| Windows  | `resources/icons/llocr.ico`                     | embedded as an executable resource (`llocr.rc`) |
| macOS    | `resources/icons/llocr.icns`                    | `.app/Contents/Resources/llocr.icns` (+ `Info.plist`) |
| Linux    | `resources/icons/hicolor/**/llocr.png`           | installation into the freedesktop `hicolor` theme (+ `.desktop`) |

Every platform also gets a **runtime window/taskbar icon** from embedded
multi-size PNGs via `QGuiApplication::setWindowIcon()` (see `src/main.cpp`).

## Source asset

The authoritative vector source is `resources/icons/app-icon.svg`. All raster
formats are generated from it. After adding the icon the `xmlns` was corrected
to the standard SVG namespace (`http://www.w3.org/2000/svg`).

Generated raster assets (committed so builds / CI need no converter tooling):

- `resources/icons/llocr-*.png` — multi-size RGBA PNGs for the window icon
  (16, 24, 32, 48, 64, 128, 256, 512, 1024).
- `resources/icons/llocr.ico` — Windows icon (Vista+ PNG-compressed entries:
  16/24/32/48/64/128/256).
- `resources/icons/llocr.icns` — macOS icon container (PNG entries for
  16/32/64/128/256/512/1024).
- `resources/icons/hicolor/<size>/apps/llocr.png` — freedesktop icon theme set.

## How the runtime icon is applied

In `src/main.cpp` a `QIcon` is built with one file per size and passed to
`app.setWindowIcon(...)`. Qt then picks the crispest matching size for the
window system on each platform (taskbar, Alt-tab, WM). The PNGs are embedded via
the `app_icons` resource (`:/icons/llocr-*.png`).

## macOS

**Development (default build).** The window icon is set at runtime. macOS
windows do not usually draw a title-bar icon, so during development the window
icon is only visible where Qt exposes it.

**Real .app bundle (Dock + Finder).** To get the icon in the Dock and Finder,
build the application as a macOS bundle with the `LLOCR_MACOS_APP_BUNDLE` option:

```sh
cmake -S . -B build-bundle -G "Unix Makefiles" \
  -DCMAKE_PREFIX_PATH=/Users/gladskih/Qt/6.10.3/macos \
  -DLLOCR_MACOS_APP_BUNDLE=ON
cmake --build build-bundle -j 8
```

The result is `build-bundle/bin/llocr.app` with:

- `Contents/MacOS/llocr`
- `Contents/Resources/llocr.icns`
- `Contents/Info.plist` containing `CFBundleIconFile` = `llocr.icns`

Run with `open build-bundle/bin/llocr.app` (or `.../MacOS/llocr`).

> The option is **OFF by default** so the existing `build/bin/llocr` development
> workflow is unchanged. Enable it only when you want a distributable `.app`.

For an installer (a signed/notarization `.dmg` is a later release-step), run
Qt's `macdeployqt`/`macdeployqt6` on the `.app`; the `.icns` is already in the
bundle's `Resources` folder.

## Windows

The ico is embedded into the executable via a Windows `.rc` resource:

```
IDI_ICON1 ICON "llocr.ico"
```

When `src/CMakeLists.txt` is configured on `WIN32`, `llocr.rc` is added to the
target sources and the Resource compiler (`rc` / *windres*) embeds it. Explorer
then shows the icon for the `.exe`. If the Windows resource compiler cannot
resolve `llocr.ico` (some MSVC `rc` builds resolve relative to the working
directory), replace `"llocr.ico"` in `llocr.rc` with the full path to
`resources/icons/llocr.ico`.

## Linux

Two pieces combine:

1. **Runtime/launcher icon.** The hicolor PNGs are installed into the
   freedesktop `hicolor` icon theme so GNOME/KDE and other `.desktop`-launching
   environments find the matching size:

   ```cmake
   install(FILES resources/icons/hicolor/<size>/apps/llocr.png
           DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/<size>/apps)
   ```

   and the `.desktop` entry template `resources/icons/llocr.desktop.in` is
   `configure_file`d (substituting `@LLOCR_EXECUTABLE@`) into
   `${CMAKE_INSTALL_DATADIR}/applications/llocr.desktop`.

2. **Manual alternative.** If you do not use `cmake --install`, copy the same
   files yourself:

   ```sh
   install -d /usr/share/applications
   # for each size dir under resources/icons/hicolor:
   install resources/icons/hicolor/<size>/apps/llocr.png \
       /usr/share/icons/hicolor/<size>/apps/
   ```

   If the freedesktop icon is not picked up (for example because your desktop
   caches icon themes), place `llocr.png` next to the binary or point
   `Icon=` in `llocr.desktop` at an absolute PNG path.

## Regenerating the raster assets

The committed assets are generated from `resources/icons/app-icon.svg` by the
scripts in `tools/`:

- `tools/rasterize_icon/main.cpp` — a tiny Qt program that renders an SVG to
  PNGs with `QSvgRenderer` (QtSvg). Build/run against the installed Qt
  frameworks to produce `icon-<N>.png` files.
- `tools/make_ico.py` — assembles `llocr.ico` from PNG entries (Python stdlib).
- `tools/make_icns.py` — assembles `llocr.icns` from PNG entries (Python stdlib).

These are only needed when the icon design changes; they are not part of the
normal `cmake --build` (the PNG/ICO/ICNS already-rendered files are committed).