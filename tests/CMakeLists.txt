qt_add_executable(test_det_parser test_det_parser.cpp)

target_include_directories(test_det_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)

# QRectF lives in QtCore, so Qt6::Gui is not required here.
target_link_libraries(test_det_parser PRIVATE
    Qt6::Test
    Qt6::Core
)

target_sources(test_det_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/parsers/DetTokensParser.cpp
    ${CMAKE_SOURCE_DIR}/src/parsers/RawParser.cpp
    ${CMAKE_SOURCE_DIR}/src/parsers/ParserFactory.cpp
)
add_test(NAME test_det_parser COMMAND test_det_parser)

qt_add_executable(test_pagemodel test_pagemodel.cpp)

target_include_directories(test_pagemodel PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_pagemodel PRIVATE
    Qt6::Test
    Qt6::Core
)

target_sources(test_pagemodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/PageListModel.cpp
)
add_test(NAME test_pagemodel COMMAND test_pagemodel)

qt_add_executable(test_settings_store test_settings_store.cpp)

target_include_directories(test_settings_store PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_settings_store PRIVATE
    Qt6::Test
    Qt6::Core
)

target_sources(test_settings_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/SettingsStore.cpp
)
add_test(NAME test_settings_store COMMAND test_settings_store)

qt_add_executable(test_launch_config test_launch_config.cpp)

target_include_directories(test_launch_config PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_launch_config PRIVATE
    Qt6::Test
    Qt6::Core
)

target_sources(test_launch_config PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/ServerLaunchConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SettingsStore.cpp
)
add_test(NAME test_launch_config COMMAND test_launch_config)

qt_add_executable(test_runtime_lifetime test_runtime_lifetime.cpp)

target_include_directories(test_runtime_lifetime PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_runtime_lifetime PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Concurrent
    Qt6::Qml
)

target_sources(test_runtime_lifetime PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/RuntimeController.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SettingsStore.cpp
)
add_test(NAME test_runtime_lifetime COMMAND test_runtime_lifetime)

qt_add_executable(test_exporter test_exporter.cpp)

target_include_directories(test_exporter PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_exporter PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

target_sources(test_exporter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/Exporter.cpp
)
add_test(NAME test_exporter COMMAND test_exporter)
