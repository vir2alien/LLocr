set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.21)

project(LLocr
    VERSION 0.2.0
    DESCRIPTION "Cross-platform OCR application powered by local LLMs"
    LANGUAGES CXX
)

# --- Global settings ---------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all binaries in a predictable location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Enable Qt automatic tooling (moc, rcc, uic).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Generate compile_commands.json for IDEs / clang-tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options -----------------------------------------------------------------

option(LLOCR_BUILD_TESTS "Build unit tests" ON)

# --- Dependencies ------------------------------------------------------------

# Qt6 modules used across the project.
find_package(Qt6 REQUIRED COMPONENTS
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


# Enable Qt6 policies for modern CMake integration (qt_add_executable, etc.).
qt_standard_project_setup(REQUIRES 6.5)

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
