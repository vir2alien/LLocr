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
find_package(ZLIB REQUIRED)


# Enable Qt6 policies for modern CMake integration (qt_add_executable, etc.).
qt_standard_project_setup(REQUIRES 6.10)

# DjVu is required; dependency acquisition is deliberately outside this build.
set(DJVULIBRE_ROOT "" CACHE PATH "DjVuLibre installation prefix (or source tree with built libraries)")

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

    if(NOT DJVULIBRE_INCLUDE_DIR OR
            NOT EXISTS "${DJVULIBRE_INCLUDE_DIR}/libdjvu/ddjvuapi.h" OR
            (NOT DJVULIBRE_LIBRARY_RELEASE AND NOT DJVULIBRE_LIBRARY_DEBUG))
        message(FATAL_ERROR
            "DjVuLibre is required for DjVu input. Install its development package "
            "(Unix: pkg-config module ddjvuapi), or set DJVULIBRE_ROOT / "
            "DJVULIBRE_INCLUDE_DIR and DJVULIBRE_LIBRARY_RELEASE / "
            "DJVULIBRE_LIBRARY_DEBUG to a matching native build. Windows needs "
            "MSVC x64 DLL import libraries, not MinGW or Win32 binaries. "
            "No dependency is downloaded automatically. See docs/06-dev-setup.md."
        )
    endif()

    if(WIN32)
        add_library(DjVuLibre::DjVuLibre SHARED IMPORTED)
    else()
        add_library(DjVuLibre::DjVuLibre UNKNOWN IMPORTED)
    endif()
    set_target_properties(DjVuLibre::DjVuLibre PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DJVULIBRE_INCLUDE_DIR}"
    )
    foreach(config RELEASE DEBUG)
        if(DJVULIBRE_LIBRARY_${config})
            if(NOT EXISTS "${DJVULIBRE_LIBRARY_${config}}")
                message(FATAL_ERROR
                    "DJVULIBRE_LIBRARY_${config} does not exist: ${DJVULIBRE_LIBRARY_${config}}"
                )
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
                    message(FATAL_ERROR
                        "DjVuLibre ${config} DLL not found. Set DJVULIBRE_DLL_${config} "
                        "to the DLL matching DJVULIBRE_LIBRARY_${config}; "
                        "it is copied beside llocr.exe after linking."
                    )
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
        endif()
    endforeach()
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
