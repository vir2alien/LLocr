qt_add_executable(llocr
    main.cpp

    # core

    core/OcrResult.h
    core/OcrRequest.h
    core/CheckRequest.h
    core/CheckResult.h
    core/ConnectionConfig.h
    core/LlamaClient.h
    core/LlamaClient.cpp
    core/ServiceMarkers.h
    core/RequestProfile.h
    core/RequestProfile.cpp
    core/LaunchProfile.h
    core/LaunchProfile.cpp
    core/ValueParsing.h

    # models
    models/OcrModel.h
    models/OcrModel.cpp
    models/UnlimitedOcrModel.h
    models/UnlimitedOcrModel.cpp
    models/OcrModelFactory.h
    models/OcrModelFactory.cpp
    models/GeneralPurposeModel.h
    models/GeneralPurposeModel.cpp
    models/QwenGeneralModel.h
    models/QwenGeneralModel.cpp

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

    app/BlockGroupFilterModel.h
    app/BlockGroupFilterModel.cpp

    app/DocumentModel.h
    app/DocumentModel.cpp
    app/Exporter.h
    app/Exporter.cpp
    app/ExportRenderer.h
    app/ExportRenderer.cpp
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
    app/ProfileStorage.h
    app/ProfileStorage.cpp
    app/RecognitionController.h
    app/RecognitionController.cpp
    app/CheckController.h
    app/CheckController.cpp
    app/RequestParametersModel.h
    app/RequestParametersModel.cpp
    app/RequestProfileStore.h
    app/RequestProfileStore.cpp
    app/VerificationPromptStore.h
    app/VerificationPromptStore.cpp
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
    runtime/DownloadGroup.h
    runtime/DownloadGroup.cpp
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
    if(LLOCR_DJVU_AVAILABLE)
        add_custom_command(TARGET llocr POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:DjVuLibre::DjVuLibre>"
                "$<TARGET_FILE_DIR:llocr>"
            COMMENT "Copying DjVuLibre DLL beside llocr.exe"
            VERBATIM
        )
    endif()
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
        "${CMAKE_SOURCE_DIR}/resources/qml/ServerLogWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/ExportDialog.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Theme.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/BlockNames.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SetupWizard.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepIntro.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepWelcome.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepRuntime.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepBinary.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepExternal.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepModel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepOutput.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepLaunch.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Setup/StepDone.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Header.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbDelegate.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ThumbPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/ImagePreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/BlockEditPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/WorkPanel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/MarkdownPreview.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/MainWindow/Footer.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/VerificationSettingsWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/VerificationBlocksTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/VerificationSystemTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/VerificationPromptsTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/UiSettingsWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/OutputSettingsWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/RuntimeSettingsWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/ModelSettingsWindow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/UITab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/OutputTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/RequestTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/LaunchTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/RuntimeTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/RuntimeTabExternal.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/RuntimeTabInternal.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Settings/LocationTab.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/LLOButton.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/LLOCheckBox.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/LLOLabel.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/RuntimeBuildsList.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/ModelInstalledList.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/ModelPresetList.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/InstallerProgressRow.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/Common/InstallerStatusLabel.qml"
)

qt_add_resources(llocr "app_conf"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qtquickcontrols2.conf"
)

qt_add_resources(llocr "app_profiles"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/requestOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/requestValidate.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/verifyPrompts.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunchOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunchValidate.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/defaultLlmPresetsOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/defaultLlmPresetsValidate.json"
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
# A self-contained .app bundle is the reliable way to ship on macOS: Qt
# frameworks and plugins are copied into the bundle and their install names
# rewritten to @executable_path (via qt_generate_deploy_app_script, hardened
# runtime enabled). A bundle does NOT depend on DYLD_* environment variables,
# so it is immune to the Homebrew QtCore mix-up that breaks a dev-run launched
# from Qt Creator (see the DYLD_FRAMEWORK_PATH guard in CMakeLists.txt and
# scripts/run-macos.sh). Build a bundle with:
#   cmake -S . -B build/bundle -DLLOCR_MACOS_APP_BUNDLE=ON ...
#   cmake --build build/bundle
#   cmake --install build/bundle --prefix build/bundle/install
#   open build/bundle/install/llocr.app
option(LLOCR_MACOS_APP_BUNDLE "Build llocr as a macOS .app bundle" OFF)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE llocr.icns)
    set(app_icon_macos "${CMAKE_SOURCE_DIR}/resources/icons/llocr.icns")
    set_source_files_properties(${app_icon_macos} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    if(LLOCR_MACOS_APP_BUNDLE)
        set_target_properties(llocr PROPERTIES MACOSX_BUNDLE TRUE)
        target_sources(llocr PRIVATE ${app_icon_macos})

        # Stage the .app bundle and deploy Qt runtime dependencies into it
        # (Qt frameworks, plugins incl. Qt WebEngine, translations).
        install(TARGETS llocr
            BUNDLE DESTINATION .
        )
        qt_generate_deploy_app_script(
            TARGET llocr
            OUTPUT_SCRIPT deploy_llocr_script
            NO_UNSUPPORTED_PLATFORM_ERROR
            DEPLOY_TOOL_OPTIONS -hardened-runtime
        )
        install(SCRIPT "${deploy_llocr_script}")
    endif()
endif()

target_include_directories(llocr PRIVATE ${CMAKE_SOURCE_DIR}/src)

# DjVu input support is compiled only when DjVuLibre was found (ADR 69).
# Without it the app builds and opening .djvu files reports a clear error.
if(LLOCR_DJVU_AVAILABLE)
    target_sources(llocr PRIVATE
        app/DjVuDocument.h
        app/DjVuDocument.cpp
    )
endif()

target_link_libraries(llocr PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Pdf
    Qt6::WebEngineCore
    Qt6::WebEngineQuick
    ZLIB::ZLIB
)

if(LLOCR_DJVU_AVAILABLE)
    target_link_libraries(llocr PRIVATE DjVuLibre::DjVuLibre)
    target_compile_definitions(llocr PRIVATE LLOCR_HAVE_DJVU)
endif()

target_compile_definitions(llocr PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

qt_add_translations(llocr
    TS_FILES
        "${CMAKE_SOURCE_DIR}/resources/i18n/llocr_ru.ts"
)

# --- macOS dev-build launcher -------------------------------------------------
# Qt Creator's run step translates its internal _QTC_DYLD_FRAMEWORK_PATH into
# DYLD_FRAMEWORK_PATH for the launched process; that value may list /opt/homebrew
# lib BEFORE the installed Qt's dirs (Homebrew Qt). Because dyld searches
# DYLD_FRAMEWORK_PATH first for any ".framework" name, the app can mix Qt
# versions and die at startup ("Symbol not found: ... QtPrivate_6_10_3"). No
# binary-side change can beat that variable, so for DEV builds we ship a small
# launcher script (the build output "llocr" is a wrapper; the real binary is
# "llocr.real" beside it) that re-execs the binary with a clean environment.
# The .app bundle does NOT need this: macdeployqt copies the frameworks into the
# bundle and rewrites install names (@executable_path) + hardened runtime strips
# DYLD_*, so we skip the wrapper in bundle mode.
if(APPLE AND NOT LLOCR_MACOS_APP_BUNDLE)
    configure_file(
        "${CMAKE_SOURCE_DIR}/scripts/llocr-launcher.in"
        "${CMAKE_CURRENT_BINARY_DIR}/llocr-launcher.sh"
        @ONLY
    )
    add_custom_command(TARGET llocr POST_BUILD
        COMMENT "Wrapping llocr in the macOS DYLD_*-clean launcher"
        # 1) keep the freshly linked real binary as llocr.real (the linker just
        #    rewrote $<TARGET_FILE:llocr>, so this always captures the real file)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:llocr>" "$<TARGET_FILE:llocr>.real"
        # 2) replace the build output with the launcher script
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/llocr-launcher.sh" "$<TARGET_FILE:llocr>"
        COMMAND chmod +x "$<TARGET_FILE:llocr>"
        VERBATIM
    )
endif()
