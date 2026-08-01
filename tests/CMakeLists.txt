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

qt_add_executable(test_profile test_profile.cpp)
target_include_directories(test_profile PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_profile PRIVATE Qt6::Test Qt6::Core)
target_sources(test_profile PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/ModelProfile.cpp
)
add_test(NAME test_profile COMMAND test_profile)
