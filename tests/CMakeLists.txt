# Unit tests.
#
# Every test links a layer target instead of re-listing the production sources
# (ADR 94): the list used to be 241 lines of hand-maintained .cpp paths, and a
# missed entry was a confusing link error while a wrong entry silently compiled
# the test against a different translation-unit set than the app. Linking the
# same targets the app links also means a test cannot drift from the build.
#
#   llocr_core      value types, transport
#   llocr_config    settings + profile stores + RuntimePaths
#   llocr_parsers   output parsers            llocr_models  model adapters
#   llocr_runtime   managed runtime           llocr_app      document + view models
#
# Per-test resources (JSON catalogs, the preview bundle) stay on the test target:
# they are fixtures, not code.

# Test double: a local llama-server stand-in. It is NOT registered via add_test;
# the runtime tests spawn it as a real child process to exercise probe/health.
qt_add_executable(mock_llama_server mock_llama_server.cpp)

target_include_directories(mock_llama_server PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(mock_llama_server PRIVATE
    Qt6::Core
    Qt6::Network
)

qt_add_executable(test_det_parser test_det_parser.cpp)

target_link_libraries(test_det_parser PRIVATE
    Qt6::Test
    llocr_parsers
)

qt_add_resources(test_det_parser "det_parser_labels_res"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/labels.json"
)
add_test(NAME test_det_parser COMMAND test_det_parser)

qt_add_executable(test_profile_storage test_profile_storage.cpp)

target_link_libraries(test_profile_storage PRIVATE
    Qt6::Test
    llocr_config
)
add_test(NAME test_profile_storage COMMAND test_profile_storage)

qt_add_executable(test_verification_prompts test_verification_prompts.cpp)

target_link_libraries(test_verification_prompts PRIVATE
    Qt6::Test
    llocr_app
)
qt_add_resources(test_verification_prompts "verify_prompts_resources"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/verifyPrompts.json"
)
add_test(NAME test_verification_prompts COMMAND test_verification_prompts)

qt_add_executable(test_ocr_models test_ocr_models.cpp)

target_link_libraries(test_ocr_models PRIVATE
    Qt6::Test
    llocr_models
    llocr_parsers
)
add_test(NAME test_ocr_models COMMAND test_ocr_models)

qt_add_executable(test_pagemodel test_pagemodel.cpp)

target_link_libraries(test_pagemodel PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_pagemodel COMMAND test_pagemodel)

# Per-page edit bookkeeping: the revert baseline and the "edited" marker. The
# current text lives on the page itself (ADR 102), so this store is pure state.
qt_add_executable(test_page_edit_store test_page_edit_store.cpp)

target_link_libraries(test_page_edit_store PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_page_edit_store COMMAND test_page_edit_store)

qt_add_executable(test_box_model test_box_model.cpp)

target_link_libraries(test_box_model PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_box_model COMMAND test_box_model)

# DjVu input is optional (ADR 69): llocr_app compiles DjVuDocument.cpp and
# publishes LLOCR_HAVE_DJVU, so this target needs no per-test wiring — the
# define and the library now travel together (ADR 95).
qt_add_executable(test_document_model test_document_model.cpp)

target_link_libraries(test_document_model PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_document_model COMMAND test_document_model)

# The DjVu suite exercises the real decoder end-to-end; it is built only when
# DjVuLibre was found (ADR 69).
if(LLOCR_DJVU_AVAILABLE)
    qt_add_executable(test_djvu_document test_djvu_document.cpp)

    target_link_libraries(test_djvu_document PRIVATE
        Qt6::Test
        llocr_app
    )
    add_test(NAME test_djvu_document COMMAND test_djvu_document)
endif()

# Exercise the real AppController and its app/runtime dependencies, without the
# GUI entry point or the llocr target's QML/resources. Built unconditionally
# (ADR 99): the import/recognition/export flows run on generated PDF + raster
# fixtures, so the ViewModel layer keeps its coverage in a build without
# DjVuLibre. Only the DjVu-specific case (blank replacement pages + import
# warnings) is compiled in when the optional codec is present.
qt_add_executable(test_app_import test_app_import.cpp)

target_link_libraries(test_app_import PRIVATE
    Qt6::Test
    llocr_app
)
qt_add_resources(test_app_import "app_import_profiles"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/requestOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/requestValidate.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/verifyPrompts.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunchOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunchValidate.json"
)
if(WIN32 AND LLOCR_DJVU_AVAILABLE)
    add_custom_command(TARGET test_app_import POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:DjVuLibre::DjVuLibre>"
            "$<TARGET_FILE_DIR:test_app_import>"
        VERBATIM
    )
endif()
add_test(NAME test_app_import COMMAND test_app_import)
set_tests_properties(test_app_import PROPERTIES TIMEOUT 90)

qt_add_executable(test_settings_store test_settings_store.cpp)

target_link_libraries(test_settings_store PRIVATE
    Qt6::Test
    llocr_config
)
add_test(NAME test_settings_store COMMAND test_settings_store)

qt_add_executable(test_request_profile test_request_profile.cpp)

target_link_libraries(test_request_profile PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_request_profile COMMAND test_request_profile)

qt_add_executable(test_launch_profile test_launch_profile.cpp)

# LaunchProfileStore::activeProfileId() uses ReleaseCatalog::detectPlatform,
# which lives in llocr_runtime.
target_link_libraries(test_launch_profile PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_launch_profile COMMAND test_launch_profile)

qt_add_executable(test_launch_config test_launch_config.cpp)

target_link_libraries(test_launch_config PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_launch_config COMMAND test_launch_config)

qt_add_executable(test_runtime_lifetime test_runtime_lifetime.cpp)

target_link_libraries(test_runtime_lifetime PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_runtime_lifetime COMMAND test_runtime_lifetime)

qt_add_executable(test_exporter test_exporter.cpp)

target_link_libraries(test_exporter PRIVATE
    Qt6::Test
    llocr_app
)

# Minimal fixture for the export-assembly tests: a fake katex.min.css with
# data-URI-able font references. The real bundle lives in the llocr target
# only, so the test registers its own copy under the same :/preview/ prefix.
qt_add_resources(test_exporter "exporter_preview_fixture"
    PREFIX "/preview"
    BASE "${CMAKE_SOURCE_DIR}/tests/fixtures/exporter-preview"
    FILES
        "${CMAKE_SOURCE_DIR}/tests/fixtures/exporter-preview/katex.min.css"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/exporter-preview/fonts/KaTeX_Fake-Regular.woff2"
)
add_test(NAME test_exporter COMMAND test_exporter)

# ---- Export renderer: headless WebEngine integration -----------------------
# Reuses the same :/preview/ bundle as the application (export.html drives the
# render); needs the WebEngine runtime, so it is the slowest unit target.
qt_add_executable(test_export_renderer test_export_renderer.cpp)

target_link_libraries(test_export_renderer PRIVATE
    Qt6::Test
    # The test calls QtWebEngineQuick::initialize() itself, so it links the
    # Quick module directly rather than relying on the app's private link.
    Qt6::WebEngineQuick
    llocr_app
)

file(GLOB_RECURSE TEST_PREVIEW_FILES
    "${CMAKE_SOURCE_DIR}/resources/preview/*")
list(FILTER TEST_PREVIEW_FILES EXCLUDE REGEX "\\.DS_Store$")

qt_add_resources(test_export_renderer "export_renderer_preview_res"
    PREFIX "/preview"
    BASE "${CMAKE_SOURCE_DIR}/resources/preview"
    FILES ${TEST_PREVIEW_FILES}
)
add_test(NAME test_export_renderer COMMAND test_export_renderer)

# ---- Shared HTTP policy (ADR 108) -----------------------------------------
qt_add_executable(test_http_client test_http_client.cpp)

target_link_libraries(test_http_client PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_http_client COMMAND test_http_client)

# ---- Stage B: capabilities -------------------------------------------------
qt_add_executable(test_capabilities test_capabilities.cpp)

target_link_libraries(test_capabilities PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_capabilities COMMAND test_capabilities)

# ---- Stage B: runtime locator ---------------------------------------------
qt_add_executable(test_runtime_locator test_runtime_locator.cpp)

target_compile_definitions(test_runtime_locator PRIVATE
    LLOCR_MOCK_SERVER="$<TARGET_FILE:mock_llama_server>"
)

target_link_libraries(test_runtime_locator PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_runtime_locator COMMAND test_runtime_locator)

# ---- Stage B: server process ----------------------------------------------
qt_add_executable(test_server_process test_server_process.cpp)

target_compile_definitions(test_server_process PRIVATE
    LLOCR_MOCK_SERVER="$<TARGET_FILE:mock_llama_server>"
)

target_link_libraries(test_server_process PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_server_process COMMAND test_server_process)

# ---- Stage G-core: connection readiness ----------------------------------
qt_add_executable(test_ensure_connection test_ensure_connection.cpp)

target_compile_definitions(test_ensure_connection PRIVATE
    LLOCR_MOCK_SERVER="$<TARGET_FILE:mock_llama_server>"
)

target_link_libraries(test_ensure_connection PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_ensure_connection COMMAND test_ensure_connection)

# ---- §H.2: model memory estimator ---------------------------------------
qt_add_executable(test_model_memory_estimator test_model_memory_estimator.cpp)

target_link_libraries(test_model_memory_estimator PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_memory_estimator COMMAND test_model_memory_estimator)

# ---- Stage C: download manager ------------------------------------------
qt_add_executable(test_download_manager test_download_manager.cpp)

target_link_libraries(test_download_manager PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_download_manager COMMAND test_download_manager)

# ---- Stage D: release catalog -------------------------------------------
qt_add_executable(test_release_catalog test_release_catalog.cpp)

target_link_libraries(test_release_catalog PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_release_catalog COMMAND test_release_catalog)

# ---- Stage D: archive extractor -----------------------------------------
qt_add_executable(test_archive_extractor test_archive_extractor.cpp)

target_link_libraries(test_archive_extractor PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_archive_extractor COMMAND test_archive_extractor)

# ---- Publish-by-rename shared by both install transactions (ADR 112) -------
qt_add_executable(test_staged_install test_staged_install.cpp)

target_link_libraries(test_staged_install PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_staged_install COMMAND test_staged_install)

# ---- Stage D: install transaction ----------------------------------------
qt_add_executable(test_install_transaction test_install_transaction.cpp)

target_compile_definitions(test_install_transaction PRIVATE
    LLOCR_MOCK_SERVER="$<TARGET_FILE:mock_llama_server>"
)

target_link_libraries(test_install_transaction PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_install_transaction COMMAND test_install_transaction)

# ---- Stage E: model catalog -------------------------------------------
qt_add_executable(test_model_catalog test_model_catalog.cpp)

target_link_libraries(test_model_catalog PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_catalog COMMAND test_model_catalog)

# ---- Stage E: model registry -----------------------------------------
qt_add_executable(test_model_registry test_model_registry.cpp)

target_link_libraries(test_model_registry PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_registry COMMAND test_model_registry)

# ---- Stage E: model installer (removeModel / multi-quant) ----------------
qt_add_executable(test_model_installer test_model_installer.cpp)

# The built-in preset catalogs are Qt resources; register them under the
# same :/profiles/ prefix so ModelInstaller::reloadPresetsInternal resolves
# them.
qt_add_resources(test_model_installer "model_installer_preset_res"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/defaultLlmPresetsOcr.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/defaultLlmPresetsValidate.json"
)

target_link_libraries(test_model_installer PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_installer COMMAND test_model_installer)

# ---- Stage 4 refactoring: model install transaction ---------------------
qt_add_executable(test_model_install_transaction test_model_install_transaction.cpp)

target_link_libraries(test_model_install_transaction PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_install_transaction COMMAND test_model_install_transaction)

# ---- Stage E: model preset catalog -------------------------------------
qt_add_executable(test_model_preset_catalog test_model_preset_catalog.cpp)

# The shipped built-in preset catalog is a Qt resource; register it under the
# same :/profiles/ prefix so ModelPresetCatalog::kBuiltInOcrPath resolves
# in-test.
qt_add_resources(test_model_preset_catalog "preset_res"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/defaultLlmPresetsOcr.json"
)

target_link_libraries(test_model_preset_catalog PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_model_preset_catalog COMMAND test_model_preset_catalog)

# ---- H.6: separate install lock ------------------------------------------
qt_add_executable(test_install_lock test_install_lock.cpp)

target_link_libraries(test_install_lock PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_install_lock COMMAND test_install_lock)

# ---- Stage 0.3: default test properties (ADR 99) ---------------------------
#
# A test without an explicit TIMEOUT gets 300 s and a headless platform, so a
# hung waitForStarted() fails the run instead of blocking ctest forever. An
# explicit entry wins: the loop only appends a platform that is not set yet,
# and test_app_import's own 90 s above is kept.
get_property(_llocr_all_tests DIRECTORY PROPERTY TESTS)
foreach(_llocr_test IN LISTS _llocr_all_tests)
    get_property(_llocr_timeout TEST ${_llocr_test} PROPERTY TIMEOUT)
    if(NOT _llocr_timeout)
        set_tests_properties(${_llocr_test} PROPERTIES TIMEOUT 300)
    endif()
    get_property(_llocr_env TEST ${_llocr_test} PROPERTY ENVIRONMENT)
    if(_llocr_test STREQUAL "test_export_renderer")
        # WebEngine needs a real render stack: keep the native platform plugin
        # and disable the GPU/sandbox instead.
        if(NOT "${_llocr_env}" MATCHES "QTWEBENGINE_CHROMIUM_FLAGS")
            list(APPEND _llocr_env
                 "QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu --no-sandbox")
            set_tests_properties(${_llocr_test} PROPERTIES ENVIRONMENT "${_llocr_env}")
        endif()
    else()
        list(FIND _llocr_env "QT_QPA_PLATFORM=offscreen" _llocr_has_offscreen)
        if(_llocr_has_offscreen EQUAL -1)
            list(APPEND _llocr_env "QT_QPA_PLATFORM=offscreen")
            set_tests_properties(${_llocr_test} PROPERTIES ENVIRONMENT "${_llocr_env}")
        endif()
    endif()
endforeach()
unset(_llocr_all_tests)
unset(_llocr_test)
unset(_llocr_timeout)
unset(_llocr_env)
unset(_llocr_has_offscreen)
