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
    app/OcrImageProvider.h
    app/OcrImageProvider.cpp
    app/BoxListModel.h
    app/BoxListModel.cpp
    app/SettingsStore.h
    app/SettingsStore.cpp
    app/PageListModel.h
    app/PageListModel.cpp
)

qt_add_resources(llocr "app_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/qml/Main.qml"
        "${CMAKE_SOURCE_DIR}/resources/qml/SettingsDialog.qml"
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
