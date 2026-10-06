# ==== Layers =================================================================
#
# The code is split into targets that mirror the layer diagram in
# docs/03-architecture.md, so a layer can only reach the ones it links:
#
#   llocr_core     value types, request/response, transport
#        |
#   llocr_config   settings + profile stores + RuntimePaths   (below runtime!)
#        |          \
#   llocr_models   OCR/general model adapters        llocr_parsers  output parsers
#        |          \        /
#   llocr_runtime  managed runtime: server process, downloads, installs
#        |
#   llocr_app      document, view models, controllers
#        |
#   llocr          the executable: main.cpp + resources + the QML module
#
# Each layer owns a directory of the same name, and the include root stays
# ${CMAKE_SOURCE_DIR}/src, so a header's include spelling says which layer it
# belongs to. What a layer may *use* is decided by the link graph, not by
# convention. Moving the settings stores into their own layer is what breaks the
# old app <-> runtime cycle (runtime needs
# SettingsStore/LaunchProfileStore/RequestProfileStore, and the stores need
# RuntimePaths).

# ---- llocr_core --------------------------------------------------------------
qt_add_library(llocr_core STATIC
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
    core/StatusMessage.h
    core/StatusMessage.cpp
    core/ValueParsing.h
    core/ModelProfiles.h
    core/ModelProfiles.cpp
)
target_include_directories(llocr_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_core PUBLIC Qt6::Core Qt6::Network)

# ---- llocr_config ------------------------------------------------------------
# Settings and profile persistence, deliberately below llocr_runtime: the runtime
# reads the settings, the settings do not read the runtime (RuntimePaths is a path
# helper and lives here).
#
# LaunchProfileStore is NOT here: it drafts through LaunchParametersModel (a QML
# list model) and asks ReleaseCatalog::detectPlatform() for the active preset, so
# it belongs to llocr_runtime next to those. RequestProfileStore has no such
# dependency and stays.
qt_add_library(llocr_config STATIC
    config/ProfileStorage.h
    config/ProfileStorage.cpp
    config/ProfileStore.h
    config/SettingsStore.h
    config/SettingsStore.cpp
    config/RequestParametersModel.h
    config/RequestParametersModel.cpp
    config/RequestProfileListModel.h
    config/RequestProfileListModel.cpp
    config/RequestProfileStore.h
    config/RequestProfileStore.cpp
    config/RuntimePaths.h
    config/RuntimePaths.cpp
)
target_include_directories(llocr_config PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_config PUBLIC llocr_core Qt6::Core Qt6::Gui)

# ---- llocr_models ------------------------------------------------------------
qt_add_library(llocr_models STATIC
    models/ChatExchange.h
    models/OcrModel.h
    models/OcrModel.cpp
    models/ImageDataUrl.h
    models/ImageDataUrl.cpp
    models/GeneralPurposeModel.h
    models/GeneralPurposeModel.cpp
)
target_include_directories(llocr_models PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_models PUBLIC llocr_core Qt6::Core Qt6::Gui)

# ---- llocr_parsers -----------------------------------------------------------
qt_add_library(llocr_parsers STATIC
    parsers/IOutputParser.h
    parsers/ParserOptions.h
    parsers/BlockStyle.h
    parsers/BlockStyle.cpp
    parsers/RawParser.h
    parsers/RawParser.cpp
    parsers/DetTokenParserBase.h
    parsers/DetTokenParserBase.cpp
    parsers/UnlimitedOcrParser.h
    parsers/UnlimitedOcrParser.cpp
    parsers/Lfm25VlParser.h
    parsers/Lfm25VlParser.cpp
    parsers/DetTokenFormat.h
    parsers/DetTokenFormat.cpp
    parsers/OtslTable.h
    parsers/OtslTable.cpp
    parsers/Lfm25VlDrift.h
    parsers/Lfm25VlDrift.cpp
    parsers/ParserFactory.h
    parsers/ParserFactory.cpp
)
target_include_directories(llocr_parsers PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_parsers PUBLIC llocr_core Qt6::Core)

# ---- llocr_runtime -----------------------------------------------------------
qt_add_library(llocr_runtime STATIC
    runtime/ConnectionMode.h
    runtime/HttpClient.h
    runtime/HttpClient.cpp
    runtime/InstalledReconcile.h
    runtime/InstalledReconcile.cpp
    runtime/InstalledState.h
    runtime/InstalledState.cpp
    runtime/InstalledBuildsModel.h
    runtime/InstalledBuildsModel.cpp
    runtime/ModelQuantModel.h
    runtime/ModelQuantModel.cpp
    runtime/StagedInstall.h
    runtime/StagedInstall.cpp
    runtime/InstallLockGuard.h
    runtime/InstallLockGuard.cpp
    runtime/ResolvedConnection.h
    runtime/RuntimeState.h
    runtime/ServerCapabilities.h
    runtime/ServerCapabilities.cpp
    runtime/ServerLaunchConfig.h
    runtime/ServerLaunchConfig.cpp
        runtime/FileDigest.h
        runtime/FileDigest.cpp
    runtime/RuntimeLocator.h
    runtime/RuntimeLocator.cpp
    runtime/LlamaServerProcess.h
    runtime/LlamaServerProcess.cpp
    runtime/ProcessGuard.h
    runtime/ServerOwner.h
    runtime/ServerOwner.cpp
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
    runtime/ReleaseCatalog.h
    runtime/ReleaseCatalog.cpp
    runtime/RuntimeInstaller.h
    runtime/RuntimeInstaller.cpp
    runtime/ArchiveExtractor.h
    runtime/ArchiveExtractor.cpp
    runtime/ZipWriter.h
    runtime/ZipWriter.cpp
    runtime/InstallTransaction.h
    runtime/InstallTransaction.cpp
    runtime/ModelPreset.h
    runtime/ModelPreset.cpp
    runtime/ModelCatalog.h
    runtime/ModelCatalog.cpp
    runtime/ModelInstallTransaction.h
    runtime/ModelInstallTransaction.cpp
    runtime/ModelPresetCatalog.h
    runtime/ModelPresetCatalog.cpp
    runtime/ModelRegistry.h
    runtime/ModelRegistry.cpp
    runtime/ModelInstaller.h
    runtime/ModelInstaller.cpp
    runtime/ModelMemoryEstimator.h
    runtime/ModelMemoryEstimator.cpp
    runtime/LaunchProfileStore.h
    runtime/LaunchProfileStore.cpp
    runtime/LaunchParametersModel.h
    runtime/LaunchParametersModel.cpp
)
target_include_directories(llocr_runtime PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_runtime
    # Qt6::Qml is PUBLIC: SelfTestController.h / ModelInstaller.h include
    # <QQmlEngine>, so consumers need the include path
    PUBLIC llocr_config llocr_core llocr_models Qt6::Qml
    PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent ZLIB::ZLIB
)

# One ProcessGuard implementation per platform; the other two are stubs, so the
# class always has a definition.
if(WIN32)
    target_sources(llocr_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_win.cpp)
elseif(APPLE)
    target_sources(llocr_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_mac.cpp)
else()
    target_sources(llocr_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src/runtime/ProcessGuard_linux.cpp)
endif()

# ---- llocr_app ---------------------------------------------------------------
qt_add_library(llocr_app STATIC
    app/AppController.h
    app/AppController.cpp
    app/BlockGroupFilterModel.h
    app/BlockGroupFilterModel.cpp
    app/BlockTextHighlighter.h
    app/BlockTextHighlighter.cpp
    app/DocumentModel.h
    app/DocumentModel.cpp
    app/Exporter.h
    app/Exporter.cpp
    app/LruImageCache.h
    app/ExportRenderer.h
    app/ExportRenderer.cpp
    app/I18n.h
    app/I18n.cpp
    app/OcrImageProvider.h
    app/OcrImageProvider.cpp
    app/BoxListModel.h
    app/BoxListModel.cpp
    app/PageListModel.h
    app/PageListModel.cpp
    app/PageEditStore.h
    app/PageEditStore.cpp
    app/PageIndex.h
    app/ProblemLog.h
    app/ProblemLog.cpp
    app/ProjectStore.h
    app/ProjectStore.cpp
    app/RecognitionController.h
    app/RecognitionController.cpp
    app/CheckController.h
    app/CheckController.cpp
    app/VerificationPromptStore.h
    app/VerificationPromptStore.cpp
    app/VerificationQueueController.h
    app/VerificationQueueController.cpp
    app/ExportController.h
    app/ExportController.cpp
    app/UiController.h
    app/UiController.cpp
    app/PdfDocument.h
    app/PdfDocument.cpp
)
target_include_directories(llocr_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(llocr_app
    # Qt6::Qml is PUBLIC for the same reason as in llocr_runtime (UiController.h).
    PUBLIC llocr_runtime llocr_config llocr_core llocr_models llocr_parsers Qt6::Qml
    PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Pdf Qt6::Quick
            Qt6::WebEngineCore ZLIB::ZLIB
)

# DjVu input support is compiled only when DjVuLibre was found.
# Without it the app builds and opening .djvu files reports a clear error.
# Only the *implementation* is conditional: DocumentModel.h includes
# DjVuDocument.h unconditionally (it only forward-declares the ddjvu_* structs),
# so the class layout does not depend on the dependency.
if(LLOCR_DJVU_AVAILABLE)
    target_sources(llocr_app PRIVATE
        app/DjVuDocument.h
        app/DjVuDocument.cpp
    )
    target_link_libraries(llocr_app PRIVATE DjVuLibre::DjVuLibre)
    target_compile_definitions(llocr_app PUBLIC LLOCR_HAVE_DJVU)
endif()

# ---- llocr (the executable) --------------------------------------------------
qt_add_executable(llocr
    main.cpp
)

target_link_libraries(llocr PRIVATE
    llocr_app
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
    if(WIN32)
        add_custom_command(TARGET llocr POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:DjVuLibre::DjVuLibre>"
                "$<TARGET_FILE_DIR:llocr>"
            COMMENT "Copying DjVuLibre DLL beside llocr.exe"
            VERBATIM
        )
    endif()
endif()

# The QML tree is a real module: the file list is
# globbed, so a new .qml file no longer has to be added by hand, and
# qmllint/qmlcachegen can resolve the module. Files land as
# qrc:/qt/qml/LLocr/… (main.cpp loads that URL; the old hand-maintained
# resource list put them at qrc:/qml/…). The C++ singletons are still
# registered by hand in main.cpp (qmlRegisterSingletonInstance) — making them
# module types needs the create() factories from the layering work in stage 4;
# the QML API is unchanged either way.
#
# The module must be declared in the directory that owns the target: the
# qmltyperegistrar tooling target reads the target's autogen rules, which are
# only visible in that directory scope. CONFIGURE_DEPENDS re-globs on
# add/remove, so the list is no longer maintained by hand (49 lines before).
file(GLOB_RECURSE LLOCR_QML_FILES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/resources/qml/*.qml")
list(FILTER LLOCR_QML_FILES EXCLUDE REGEX "/scratch/")  # scratch: not shipped

# qt_add_qml_module resolves every QML file to an absolute path and then needs
# QT_RESOURCE_ALIAS to derive its path inside the module. The alias is relative
# to resources/qml, so the module keeps the subdirectory layout (MainWindow/,
# Settings/, Common/, Setup/).
foreach(_qml IN LISTS LLOCR_QML_FILES)
    file(RELATIVE_PATH _alias "${CMAKE_SOURCE_DIR}/resources/qml" "${_qml}")
    set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_alias}")
endforeach()

# The two QML singletons must be marked as such for the build system too:
# without QT_QML_SINGLETON_TYPE the generated qmldir lists them as ordinary
# component types, and `Theme.accent` then reads a property off the *type*
# (undefined) instead of off the singleton instance. `pragma Singleton` in the
# file is what the engine sees; this property is what qmldir/qmllint see.
# BlockNames lives in the module root, not in Common/, because qmllint resolves
# a singleton through the *nearest* qmldir and Common/qmldir (generated per
# directory) does not declare it.
foreach(_singleton Theme.qml BlockNames.qml)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/qml/${_singleton}"
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
endforeach()

qt_add_qml_module(llocr
    URI LLocr
    VERSION 1.0
    QML_FILES ${LLOCR_QML_FILES}
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
        "${CMAKE_SOURCE_DIR}/resources/profiles/verifyPrompts.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunch.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/labels.json"
)

# One file per model, all under :/profiles/models — the loader walks the
# directory, so a new model is a new file and no list to maintain.
# CONFIGURE_DEPENDS keeps that promise at build time too: without it a profile
# added after the configure is simply absent from the binary, and the catalog
# looks like it never existed.
file(GLOB MODEL_PROFILE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/profiles/models/*.json")
qt_add_resources(llocr "app_model_profiles"
    PREFIX "/profiles/models"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles/models"
    FILES ${MODEL_PROFILE_FILES}
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
