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
        "${CMAKE_SOURCE_DIR}/resources/qml/Theme.qml"
)

qt_add_resources(llocr "app_conf"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qtquickcontrols2.conf"
)

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
)

target_compile_definitions(llocr PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

qt_add_translations(llocr
    TS_FILES
        "${CMAKE_SOURCE_DIR}/resources/i18n/llocr_ru.ts"
)
