set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.21)

project(LLocr
    VERSION 0.2.0
    DESCRIPTION "Cross-platform OCR application powered by local LLMs"
    LANGUAGES CXX
)

# --- Global settings ---------------------------------------------------------
if (MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all binaries in a predictable location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Resource compilation; moc/uic are enabled by qt_standard_project_setup.
set(CMAKE_AUTORCC ON)

# Generate compile_commands.json for IDEs / clang-tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options -----------------------------------------------------------------

option(LLOCR_BUILD_TESTS "Build unit tests" ON)

# --- Dependencies ------------------------------------------------------------

# Qt6 modules used across the project.
find_package(Qt6 6.10 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Concurrent
    Qml
    Quick
    QuickControls2
    Pdf
    Test
    LinguistTools
    WebEngineQuick
)

# Guard: all resolved Qt6 modules must come from ONE Qt installation. Mixing
# module dirs from different installs (e.g. some modules from Homebrew qtbase
# and the rest from the Qt online installer) produces an app that crashes at
# startup with "Symbol not found ... QtPrivate_<version>" because Qt 6 embeds
# its version into private symbols on purpose. Detect the mix here, at
# configure time, instead of at runtime.
if(Qt6_FOUND)
    set(_qt6_required_prefixes "")
    foreach(_qt6_comp Core Gui Network Concurrent Qml Quick QuickControls2 Pdf
                    Test LinguistTools WebEngineQuick)
        if(DEFINED Qt6${_qt6_comp}_DIR)
            # <Qt6Dir> is <prefix>/lib/cmake/Qt6<Comp>; the install prefix is 3
            # levels up. REALPATH defends against symlinked prefixes
            # (e.g. /opt/homebrew/opt/qtbase -> .../Cellar/qtbase/<ver>).
            get_filename_component(_qt6_mod_prefix "${Qt6${_qt6_comp}_DIR}/../../.." ABSOLUTE)
            get_filename_component(_qt6_mod_prefix "${_qt6_mod_prefix}" REALPATH)
            if(NOT _qt6_required_prefixes)
                set(_qt6_required_prefixes "${_qt6_mod_prefix}")
            elseif(NOT "${_qt6_mod_prefix}" STREQUAL "${_qt6_required_prefixes}")
                message(FATAL_ERROR
                    "Qt6 modules resolved from different installations:\n"
                    "  reference: ${_qt6_required_prefixes}\n"
                    "  ${_qt6_comp}: ${_qt6_mod_prefix}\n"
                    "All Qt6 modules must come from the same Qt install (e.g. set "
                    "CMAKE_PREFIX_PATH to a single Qt directory). Mixing Qt "
                    "versions/modules causes an immediate crash at startup "
                    "(Symbol not found: __ZN14QObjectPrivateC2E16QtPrivate_*).")
            endif()
        endif()
    endforeach()
    unset(_qt6_required_prefixes)
    unset(_qt6_comp)
    unset(_qt6_mod_prefix)
endif()

find_package(ZLIB REQUIRED)

if(APPLE AND TARGET ZLIB::ZLIB)
    # Xcode 26+ relocates libc++ into <SDK>/usr/include/c++/v1 and requires it to
    # be searched BEFORE the C headers in <SDK>/usr/include. find_package(ZLIB)
    # resolves the SDK's system zlib and records <SDK>/usr/include as the target's
    # interface include dir, which CMake injects as -isystem far ahead of libc++ -
    # breaking every compilation inside Qt/libc++ with the SDK's own error:
    # "should contain the C++ Standard Library headers before any C Standard
    # Library". The system zlib needs no extra include dir: zlib.h is found via
    # the default sysroot search order, after c++/v1. (macOS only.)
    get_target_property(ZLIB_INCLUDE_DIRS ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
    if(ZLIB_INCLUDE_DIRS)
        list(FILTER ZLIB_INCLUDE_DIRS EXCLUDE REGEX "/usr/include$")
        set_target_properties(ZLIB::ZLIB PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}")
    endif()
endif()


# Enable Qt6 policies for modern CMake integration (qt_add_executable, etc.).
qt_standard_project_setup(REQUIRES 6.10)

# DjVu input support is optional (ADR 69): without DjVuLibre the app builds and
# works, and opening a .djvu file reports an actionable error at runtime.
# Dependency acquisition is deliberately outside this build.
option(LLOCR_WITH_DJVU "Enable DjVu input support (requires DjVuLibre)" ON)
set(DJVULIBRE_ROOT "" CACHE PATH "DjVuLibre installation prefix (or source tree with built libraries)")

set(LLOCR_DJVU_AVAILABLE OFF)

if(NOT LLOCR_WITH_DJVU)
    message(STATUS "DjVu input support disabled by LLOCR_WITH_DJVU=OFF")
else()

# Avoid picking up a Unix/MSYS pkg-config library in an MSVC build. Explicit
# paths also take precedence over pkg-config for custom Unix installations.
if(UNIX AND NOT DJVULIBRE_ROOT AND NOT DJVULIBRE_INCLUDE_DIR
        AND NOT DJVULIBRE_LIBRARY_RELEASE AND NOT DJVULIBRE_LIBRARY_DEBUG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DJVULIBRE QUIET IMPORTED_TARGET ddjvuapi)
    endif()
endif()

if(TARGET PkgConfig::DJVULIBRE)
    add_library(DjVuLibre::DjVuLibre ALIAS PkgConfig::DJVULIBRE)
    set(LLOCR_DJVU_AVAILABLE ON)
    message(STATUS "DjVu input support enabled (DjVuLibre via pkg-config)")
else()
    find_path(DJVULIBRE_INCLUDE_DIR
        NAMES libdjvu/ddjvuapi.h
        HINTS "${DJVULIBRE_ROOT}"
        PATH_SUFFIXES include
    )
    find_library(DJVULIBRE_LIBRARY_RELEASE
        NAMES djvulibre libdjvulibre
        HINTS "${DJVULIBRE_ROOT}"
        PATH_SUFFIXES lib lib64 Release lib/Release
    )
    # Upstream MSVC uses the same basename in separate configuration folders.
    # Do not accidentally discover a release library as the debug variant.
    find_library(DJVULIBRE_LIBRARY_DEBUG
        NAMES djvulibred libdjvulibred djvulibre libdjvulibre
        PATHS "${DJVULIBRE_ROOT}" ${CMAKE_PREFIX_PATH}
        PATH_SUFFIXES debug/lib Debug lib/Debug
        NO_DEFAULT_PATH
    )

    set(djvu_setup_ok ON)
    if(NOT DJVULIBRE_INCLUDE_DIR OR
            NOT EXISTS "${DJVULIBRE_INCLUDE_DIR}/libdjvu/ddjvuapi.h" OR
            (NOT DJVULIBRE_LIBRARY_RELEASE AND NOT DJVULIBRE_LIBRARY_DEBUG))
        set(djvu_setup_ok OFF)
        message(WARNING
            "DjVuLibre was not found - building WITHOUT DjVu input support; "
            "opening .djvu files will report an error at runtime. To enable it, "
            "install the development package (Unix: pkg-config module ddjvuapi), "
            "or set DJVULIBRE_ROOT / DJVULIBRE_INCLUDE_DIR and "
            "DJVULIBRE_LIBRARY_RELEASE / DJVULIBRE_LIBRARY_DEBUG to a matching "
            "native build (Windows needs MSVC x64 DLL import libraries, not "
            "MinGW or Win32 binaries). No dependency is downloaded automatically. "
            "See docs/06-dev-setup.md. Pass -DLLOCR_WITH_DJVU=OFF to silence "
            "this warning."
        )
    elseif((DJVULIBRE_LIBRARY_RELEASE AND NOT EXISTS "${DJVULIBRE_LIBRARY_RELEASE}") OR
            (DJVULIBRE_LIBRARY_DEBUG AND NOT EXISTS "${DJVULIBRE_LIBRARY_DEBUG}"))
        set(djvu_setup_ok OFF)
        message(WARNING
            "The explicitly configured DjVuLibre library does not exist "
            "(DJVULIBRE_LIBRARY_RELEASE=${DJVULIBRE_LIBRARY_RELEASE}, "
            "DJVULIBRE_LIBRARY_DEBUG=${DJVULIBRE_LIBRARY_DEBUG}) - building "
            "WITHOUT DjVu input support. Check the paths and reconfigure."
        )
    else()
        if(WIN32)
            add_library(DjVuLibre::DjVuLibre SHARED IMPORTED)
        else()
            add_library(DjVuLibre::DjVuLibre UNKNOWN IMPORTED)
        endif()
        set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${DJVULIBRE_INCLUDE_DIR}"
        )
        foreach(config RELEASE DEBUG)
            if(NOT DJVULIBRE_LIBRARY_${config})
                continue()
            endif()
            set_property(TARGET DjVuLibre::DjVuLibre APPEND PROPERTY
                IMPORTED_CONFIGURATIONS ${config}
            )
            if(WIN32)
                get_filename_component(djvulibre_lib_dir "${DJVULIBRE_LIBRARY_${config}}" DIRECTORY)
                if(config STREQUAL "DEBUG")
                    set(djvulibre_dll_suffixes bin/Debug debug/bin Debug)
                else()
                    set(djvulibre_dll_suffixes bin/Release bin Release)
                endif()
                find_file(DJVULIBRE_DLL_${config}
                    NAMES libdjvulibre.dll djvulibre.dll
                    HINTS "${djvulibre_lib_dir}" "${djvulibre_lib_dir}/../bin"
                        "${djvulibre_lib_dir}/../../bin/${config}"
                        "${DJVULIBRE_ROOT}"
                    PATH_SUFFIXES ${djvulibre_dll_suffixes}
                    NO_DEFAULT_PATH
                )
                if(NOT DJVULIBRE_DLL_${config} OR NOT EXISTS "${DJVULIBRE_DLL_${config}}")
                    set(djvu_setup_ok OFF)
                    message(WARNING
                        "DjVuLibre ${config} DLL not found - building WITHOUT "
                        "DjVu input support. Set DJVULIBRE_DLL_${config} to the "
                        "DLL matching DJVULIBRE_LIBRARY_${config}."
                    )
                    break()
                endif()
                set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                    IMPORTED_IMPLIB_${config} "${DJVULIBRE_LIBRARY_${config}}"
                    IMPORTED_LOCATION_${config} "${DJVULIBRE_DLL_${config}}"
                )
            else()
                set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                    IMPORTED_LOCATION_${config} "${DJVULIBRE_LIBRARY_${config}}"
                )
            endif()
        endforeach()
        if(djvu_setup_ok)
            if(DJVULIBRE_LIBRARY_RELEASE)
                set(djvulibre_default_config RELEASE)
            else()
                set(djvulibre_default_config DEBUG)
            endif()
            if(WIN32)
                set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                    IMPORTED_IMPLIB "${DJVULIBRE_LIBRARY_${djvulibre_default_config}}"
                    IMPORTED_LOCATION "${DJVULIBRE_DLL_${djvulibre_default_config}}"
                )
            else()
                set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                    IMPORTED_LOCATION "${DJVULIBRE_LIBRARY_${djvulibre_default_config}}"
                )
            endif()
            set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                MAP_IMPORTED_CONFIG_RELWITHDEBINFO "${djvulibre_default_config}"
                MAP_IMPORTED_CONFIG_MINSIZEREL "${djvulibre_default_config}"
            )
            if(NOT DJVULIBRE_LIBRARY_RELEASE OR NOT DJVULIBRE_LIBRARY_DEBUG)
                set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
                    MAP_IMPORTED_CONFIG_RELEASE "${djvulibre_default_config}"
                    MAP_IMPORTED_CONFIG_DEBUG "${djvulibre_default_config}"
                )
                message(STATUS "DjVuLibre: only ${djvulibre_default_config} found; using it for all configurations")
            endif()
            set(LLOCR_DJVU_AVAILABLE ON)
            message(STATUS "DjVu input support enabled (DjVuLibre found)")
        endif()
    endif()
endif()

endif()

# --- Subdirectories ----------------------------------------------------------

add_subdirectory(src)

if(LLOCR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Linux icon packing (hicolor theme + desktop entry) ------------------------
# Installs the application icon into the freedesktop hicolor icon theme so it is
# used by KDE/GNOME/other .desktop launchers, plus a .desktop entry. Only wired
# on non-Apple Unix (Linux). Uses @LLOCR_EXECUTABLE@ substituted in the .desktop
# template so it points at the installed binary.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    file(GLOB_RECURSE HICOLOR_PNGS RELATIVE "${CMAKE_SOURCE_DIR}/resources/icons/hicolor"
        "${CMAKE_SOURCE_DIR}/resources/icons/hicolor/*/apps/llocr.png")

    foreach(icon_rel ${HICOLOR_PNGS})
        install(FILES "${CMAKE_SOURCE_DIR}/resources/icons/hicolor/${icon_rel}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_rel}")
    endforeach()

    # Substitute the installed executable path into the desktop entry template.
    set(LLOCR_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/llocr")
    configure_file("${CMAKE_SOURCE_DIR}/resources/icons/llocr.desktop.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/llocr.desktop"
                   @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/llocr.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
