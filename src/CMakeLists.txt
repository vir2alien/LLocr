qt_add_executable(llocr
    main.cpp

    # core

    core/OcrResult.h
    core/ProviderConfig.h

    # providers
    providers/ILlmProvider.h
    providers/OpenAiProvider.h
    providers/OpenAiProvider.cpp

    # parsers
    parsers/IOutputParser.h
    parsers/BlockStyle.h
    parsers/RawParser.h
    parsers/RawParser.cpp
    parsers/DetTokensParser.h
    parsers/DetTokensParser.cpp
    parsers/ParserFactory.h
    parsers/ParserFactory.cpp

    # app
    app/AppController.h
    app/AppController.cpp

    app/DocumentModel.h
    app/DocumentModel.cpp
    app/Exporter.h
    app/Exporter.cpp
    app/I18n.h
    app/I18n.cpp
    app/OcrImageProvider.h
    app/OcrImageProvider.cpp
    app/BoxListModel.h
    app/BoxListModel.cpp
    app/SettingsStore.h
    app/SettingsStore.cpp
    app/PageListModel.h
    app/PageListModel.cpp
    app/PageEditStore.h
    app/PageEditStore.cpp
    app/PageIndex.h
    app/RecognitionController.h
    app/RecognitionController.cpp
    app/UiController.h
    app/UiController.cpp
)

qt_add_resources(llocr "app_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qml/Main.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/WindowSettings.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/ExportDialog.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Theme.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbDelegate.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Header.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/WorkPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/MarkdownPreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Footer.qml"
)

qt_add_resources(llocr "app_conf"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qtquickcontrols2.conf"
)

file(GLOB_RECURSE PREVIEW_FILES
    "${CMAKE_SOURCE_DIR}/resources/preview/*")
list(FILTER PREVIEW_FILES EXCLUDE REGEX "\\.DS_Store$")

qt_add_resources(llocr "preview"
    PREFIX "/preview"
    BASE "${CMAKE_SOURCE_DIR}/resources/preview"
    FILES ${PREVIEW_FILES}
)

# --- App icon ------------------------------------------------------------------
# Multi-resolution raster icons embedded for the window / taskbar icon on every
# platform, plus the vector source for reference and future use.
file(GLOB_RECURSE ICON_PNG_FILES
    "${CMAKE_SOURCE_DIR}/resources/icons/llocr-[0-9]*.png")

qt_add_resources(llocr "app_icons"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/resources/icons"
    FILES ${ICON_PNG_FILES}
        "${CMAKE_SOURCE_DIR}/resources/icons/app-icon.svg"
)

# --- Windows: embed the .ico as the executable resource icon -------------------
if(WIN32)
    target_sources(llocr PRIVATE
        "${CMAKE_SOURCE_DIR}/resources/icons/llocr.rc")
endif()

# --- macOS: .icns for the application bundle -----------------------------------
option(LLOCR_MACOS_APP_BUNDLE "Build llocr as a macOS .app bundle" OFF)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE llocr.icns)
    set(app_icon_macos "${CMAKE_SOURCE_DIR}/resources/icons/llocr.icns")
    set_source_files_properties(${app_icon_macos} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    if(LLOCR_MACOS_APP_BUNDLE)
        set_target_properties(llocr PROPERTIES MACOSX_BUNDLE TRUE)
        target_sources(llocr PRIVATE ${app_icon_macos})
    endif()
endif()

target_include_directories(llocr PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(llocr PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Pdf
    Qt6::WebEngineQuick
)

target_compile_definitions(llocr PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

qt_add_translations(llocr
    TS_FILES
        "${CMAKE_SOURCE_DIR}/resources/i18n/llocr_ru.ts"
)

