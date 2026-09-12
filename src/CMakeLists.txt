qt_add_executable(llocr
    main.cpp

    # core

    core/OcrResult.h
    core/ProviderConfig.h
    core/RequestProfile.h
    core/RequestProfile.cpp
    core/LaunchProfile.h
    core/LaunchProfile.cpp

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
    app/RequestParametersModel.h
    app/RequestParametersModel.cpp
    app/RequestProfileStore.h
    app/RequestProfileStore.cpp
    app/LaunchParametersModel.h
    app/LaunchParametersModel.cpp
    app/LaunchProfileStore.h
    app/LaunchProfileStore.cpp
    app/UiController.h
    app/UiController.cpp

    # runtime
    runtime/ConnectionMode.h
    runtime/ResolvedConnection.h
    runtime/RuntimeState.h
    runtime/RuntimePaths.h
    runtime/RuntimePaths.cpp
    runtime/ServerCapabilities.h
    runtime/ServerCapabilities.cpp
    runtime/ServerLaunchConfig.h
    runtime/ServerLaunchConfig.cpp
    runtime/RuntimeLocator.h
    runtime/RuntimeLocator.cpp
    runtime/LlamaServerProcess.h
    runtime/LlamaServerProcess.cpp
    runtime/ProcessGuard.h
    runtime/DownloadTask.h
    runtime/DownloadTask.cpp
    runtime/DownloadManager.h
    runtime/DownloadManager.cpp
    runtime/RuntimeController.h
    runtime/RuntimeController.cpp
    runtime/RuntimeLog.h
    runtime/RuntimeLog.cpp
    runtime/SelfTestController.h
    runtime/SelfTestController.cpp
    runtime/SingleInstanceGuard.h
    runtime/SingleInstanceGuard.cpp
    runtime/ReleaseAsset.h
    runtime/ReleaseAsset.cpp
    runtime/ReleaseCatalog.h
    runtime/ReleaseCatalog.cpp
    runtime/RuntimeInstaller.h
    runtime/RuntimeInstaller.cpp
    runtime/ArchiveExtractor.h
    runtime/ArchiveExtractor.cpp
    runtime/InstallTransaction.h
    runtime/InstallTransaction.cpp
    runtime/ModelPreset.h
    runtime/ModelPreset.cpp
    runtime/ModelCatalog.h
    runtime/ModelCatalog.cpp
    runtime/ModelPresetCatalog.h
    runtime/ModelPresetCatalog.cpp
    runtime/ModelRegistry.h
    runtime/ModelRegistry.cpp
    runtime/ModelInstaller.h
    runtime/ModelInstaller.cpp
    runtime/ModelMemoryEstimator.h
    runtime/ModelMemoryEstimator.cpp
)

if(WIN32)
    target_sources(llocr PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_win.cpp)
elseif(APPLE)
    target_sources(llocr PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_mac.cpp)
else()
    target_sources(llocr PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_linux.cpp)
endif()

qt_add_resources(llocr "app_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qml/Main.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/WindowSettings.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/ServerLogWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/ExportDialog.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Theme.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SetupWizard.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepWelcome.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepRuntime.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepModel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepLaunch.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepDone.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Header.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbDelegate.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/WorkPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/MarkdownPreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Footer.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/SDTabBar.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/UITab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/RequestTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/LaunchTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/RuntimeTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/RuntimeTabExternal.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/RuntimeTabInternal.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog/ModelsTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/LLOButton.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/LLOLabel.qml"
)

qt_add_resources(llocr "app_conf"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qtquickcontrols2.conf"
)

qt_add_resources(llocr "app_models"
    PREFIX "/models"
    BASE "${CMAKE_SOURCE_DIR}/resources/models"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/models/default-presets.json"
)

qt_add_resources(llocr "app_profiles"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/request.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunch.json"
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
    ZLIB::ZLIB
)

target_compile_definitions(llocr PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

qt_add_translations(llocr
    TS_FILES
        "${CMAKE_SOURCE_DIR}/resources/i18n/llocr_ru.ts"
)
