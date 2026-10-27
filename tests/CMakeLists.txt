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

# The model profiles: one file per model, and a test that reads a model contract
# needs the shipped data. Declared once so adding a model does not mean editing
# every test target (docs/11-profile-findings.md, finding 6).
set(LLOCR_MODEL_PROFILE_FILES
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/unlimited-ocr.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/lfm25-vl-3b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/qwen3.5-4b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/qwen3.5-9b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/gemma-4-e4b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/gemma-4-12b.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/teleocr.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/glm-ocr.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/clef-flash.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/d3-flash.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/d3-mini.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/lightonocr.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/mineru.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/paddleocr-vl.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/models/chandra-ocr-2.json"
)

# The request profiles: the shared request texts of the fix and decision tasks
# (ADR 182). A model profile names the prompt profile it takes its texts from,
# so a test that reads a model contract needs these too.
set(LLOCR_REQUEST_PROFILE_FILES
    "${CMAKE_SOURCE_DIR}/resources/profiles/requests/fix-requests.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/requests/decision-requests.json"
    "${CMAKE_SOURCE_DIR}/resources/profiles/requests/orientation-requests.json"
)

# The shipped profiles a test reads: the model profiles and the request
# profiles they point at. One call per target so adding a profile does not mean
# editing every test target (docs/11-profile-findings.md, finding 6).
function(llocr_add_profile_resources target)
    qt_add_resources(${target} "${target}_model_profiles"
        PREFIX "/profiles/models"
        BASE "${CMAKE_SOURCE_DIR}/resources/profiles/models"
        FILES ${LLOCR_MODEL_PROFILE_FILES}
    )
    qt_add_resources(${target} "${target}_request_profiles"
        PREFIX "/profiles/requests"
        BASE "${CMAKE_SOURCE_DIR}/resources/profiles/requests"
        FILES ${LLOCR_REQUEST_PROFILE_FILES}
    )
endfunction()

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
llocr_add_profile_resources(test_det_parser)
add_test(NAME test_det_parser COMMAND test_det_parser)

qt_add_executable(test_profile_storage test_profile_storage.cpp)

target_link_libraries(test_profile_storage PRIVATE
    Qt6::Test
    llocr_config
)
add_test(NAME test_profile_storage COMMAND test_profile_storage)

# The OS secret store: a key is stored, read back and removed (ADR 227). The
# test skips itself where the OS has no store.
qt_add_executable(test_secret_store test_secret_store.cpp)

target_link_libraries(test_secret_store PRIVATE
    Qt6::Test
    llocr_config
)
add_test(NAME test_secret_store COMMAND test_secret_store)

# The external server tab: the six task rows and what they write (ADR 226).
qt_add_executable(test_runtime_tab_external_qml test_runtime_tab_external_qml.cpp)

target_link_libraries(test_runtime_tab_external_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    llocr_config
)
add_test(NAME test_runtime_tab_external_qml COMMAND test_runtime_tab_external_qml)

qt_add_executable(test_verification_prompts test_verification_prompts.cpp)

target_link_libraries(test_verification_prompts PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_verification_prompts COMMAND test_verification_prompts)

qt_add_executable(test_ocr_models test_ocr_models.cpp)

target_link_libraries(test_ocr_models PRIVATE
    Qt6::Test
    llocr_models
    llocr_parsers
)
llocr_add_profile_resources(test_ocr_models)
qt_add_resources(test_ocr_models "ocr_model_labels"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES "${CMAKE_SOURCE_DIR}/resources/profiles/labels.json"
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

# The decision-model client: /v1/systemone request build, the answer
# spellings, and the failure matrix — against a loopback double, no mock
# server needed.
qt_add_executable(test_decision_model test_decision_model.cpp)

target_link_libraries(test_decision_model PRIVATE
    Qt6::Test
    llocr_models
)
add_test(NAME test_decision_model COMMAND test_decision_model)

# The fix protocol (ADR 163, ADR 179): the OK / FIX / REVIEW / EMPTY answers
# the universal model gives over a block's candidate text, the span/EDITS
# diff gate, and the request shape that carries the candidate.
qt_add_executable(test_fix_protocol test_fix_protocol.cpp)

target_link_libraries(test_fix_protocol PRIVATE
    Qt6::Test
    llocr_models
)
add_test(NAME test_fix_protocol COMMAND test_fix_protocol)

qt_add_executable(test_fix_diff_gate test_fix_diff_gate.cpp)

target_link_libraries(test_fix_diff_gate PRIVATE
    Qt6::Test
    llocr_models
)
add_test(NAME test_fix_diff_gate COMMAND test_fix_diff_gate)

# The external runtime (ADR 226): the client's own trace of the requests (the
# server writes no log the app could read) and the fix task's answer to a
# server that ignores the reasoning budget.
qt_add_executable(test_external_runtime test_external_runtime.cpp)

target_link_libraries(test_external_runtime PRIVATE
    Qt6::Test
    llocr_models
    llocr_runtime
)
add_test(NAME test_external_runtime COMMAND test_external_runtime)

# The page statuses (the rework plan's §6): the verification aggregate over a
# page's subject blocks, the markup-only completion, and the page list roles
# that carry both to the thumbnails.
qt_add_executable(test_page_status test_page_status.cpp)

target_link_libraries(test_page_status PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_page_status COMMAND test_page_status)

# The manual markup pass (layout role): the request shape it sends through the
# block-recognition transport and the reply the layout parser consumes.
qt_add_executable(test_layout test_layout.cpp)

target_link_libraries(test_layout PRIVATE
    Qt6::Test
    llocr_models
    llocr_parsers
)
add_test(NAME test_layout COMMAND test_layout)
set_tests_properties(test_layout PROPERTIES TIMEOUT 60)

# The project container (.llocr): ZipWriter <-> ArchiveExtractor round-trip and
# the ProjectStore serialization (sources, boxes, edits, verification state).
qt_add_executable(test_project_store test_project_store.cpp)

target_link_libraries(test_project_store PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_project_store COMMAND test_project_store)

# DjVu input is optional (ADR 69): llocr_app compiles DjVuDocument.cpp and
# publishes LLOCR_HAVE_DJVU, so this target needs no per-test wiring — the
# define and the library now travel together (ADR 95).
qt_add_executable(test_document_model test_document_model.cpp)

target_link_libraries(test_document_model PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_document_model COMMAND test_document_model)

# The page's turn: the quarter turns, the rendered page and the sizes the
# model's geometry is read against.
qt_add_executable(test_page_rotation test_page_rotation.cpp)

target_link_libraries(test_page_rotation PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_page_rotation COMMAND test_page_rotation)

# The orientation check: the axis of the text and the way up, read from the
# picture alone.
qt_add_executable(test_page_orientation test_page_orientation.cpp)

target_link_libraries(test_page_orientation PRIVATE
    Qt6::Test
    llocr_app
)
# The renders of the corpus the check is measured against. The test skips when
# the checkout carries no corpus.
target_compile_definitions(test_page_orientation PRIVATE
    LLOCR_CORPUS_RENDERS="${CMAKE_SOURCE_DIR}/OCRtest/ocr_models/materials/renders"
)
add_test(NAME test_page_orientation COMMAND test_page_orientation)

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
    Qt6::Quick
    llocr_app
)
qt_add_resources(test_app_import "app_import_profiles"
    PREFIX "/profiles"
    BASE "${CMAKE_SOURCE_DIR}/resources/profiles"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/profiles/serverLaunch.json"
        "${CMAKE_SOURCE_DIR}/resources/profiles/labels.json"
)
llocr_add_profile_resources(test_app_import)
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
llocr_add_profile_resources(test_settings_store)
# The QML signal-handler check needs the source tree, not just the resources.
target_compile_definitions(test_settings_store PRIVATE
    LLOCR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME test_settings_store COMMAND test_settings_store)

qt_add_executable(test_request_profile test_request_profile.cpp)

target_link_libraries(test_request_profile PRIVATE
    Qt6::Test
    llocr_app
)
add_test(NAME test_request_profile COMMAND test_request_profile)

qt_add_executable(test_request_budget test_request_budget.cpp)

target_link_libraries(test_request_budget PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_request_budget COMMAND test_request_budget)

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
llocr_add_profile_resources(test_launch_config)
add_test(NAME test_launch_config COMMAND test_launch_config)

# The Launch settings tab as QML: the memory-estimate label must not throw
# when the estimate map is empty (the tab's placeholder before the popup
# opens). The file is read from the source tree, so the test covers the
# shipped QML rather than a copy.
qt_add_executable(test_launch_tab_qml test_launch_tab_qml.cpp)

target_link_libraries(test_launch_tab_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    # QQuickStyle: the app runs the Fusion style, and the native macOS style
    # warns about every customized control.
    Qt6::QuickControls2
    llocr_config
)
add_test(NAME test_launch_tab_qml COMMAND test_launch_tab_qml)

# The models table as QML: an unsaved edit must survive the sync that runs
# when the window is shown again, and the Cancel path must throw it away
# (ADR 200). The file is read from the source tree, so the test covers the
# shipped QML rather than a copy.
qt_add_executable(test_task_models_table_qml test_task_models_table_qml.cpp)

target_link_libraries(test_task_models_table_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    llocr_config
)
# The table's icons come from the app's icon set; without them every Icon
# would warn about a missing file.
qt_add_resources(test_task_models_table_qml "task_models_table_ui_icons"
    PREFIX "/icons/ui"
    BASE "${CMAKE_SOURCE_DIR}/resources/icons/ui"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/icons/ui/x.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/ui/settings.svg"
)
add_test(NAME test_task_models_table_qml COMMAND test_task_models_table_qml)

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

# minBuildIsComparedByNumber reads a model profile's minBuild, so the shipped
# profiles have to be a resource of this binary too.
llocr_add_profile_resources(test_ensure_connection)

# llocr_app also carries CheckController: the check-stop contract is about what
# a job must NOT do to the runtime, so it belongs next to the resolve tests.
target_link_libraries(test_ensure_connection PRIVATE
    Qt6::Test
    llocr_runtime
    llocr_app
)
add_test(NAME test_ensure_connection COMMAND test_ensure_connection)

# ---- §H.2: model memory estimator ---------------------------------------
qt_add_executable(test_model_memory_estimator test_model_memory_estimator.cpp)

target_link_libraries(test_model_memory_estimator PRIVATE
    Qt6::Test
    llocr_runtime
)
llocr_add_profile_resources(test_model_memory_estimator)
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

# The test builds ZIP fixtures with crc32 and includes <zlib.h> directly;
# llocr_runtime links ZLIB PRIVATE, so its headers do not propagate here.
target_link_libraries(test_archive_extractor PRIVATE
    Qt6::Test
    llocr_runtime
    ZLIB::ZLIB
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
    ZLIB::ZLIB
)
add_test(NAME test_install_transaction COMMAND test_install_transaction)

# ---- Stage E: model catalog -------------------------------------------
qt_add_executable(test_model_catalog test_model_catalog.cpp)

target_link_libraries(test_model_catalog PRIVATE
    Qt6::Test
    llocr_runtime
)
# The quantization of a shipped file comes from the model profile, so the
# profiles have to be a resource of this binary.
llocr_add_profile_resources(test_model_catalog)
add_test(NAME test_model_catalog COMMAND test_model_catalog)

# ---- Stage E: model registry -----------------------------------------
qt_add_executable(test_model_registry test_model_registry.cpp)

target_link_libraries(test_model_registry PRIVATE
    Qt6::Test
    llocr_runtime
)
llocr_add_profile_resources(test_model_registry)
add_test(NAME test_model_registry COMMAND test_model_registry)

# ---- Wizard hardware probe (ADR 170) -------------------------------------
qt_add_executable(test_hardware_info test_hardware_info.cpp)

target_link_libraries(test_hardware_info PRIVATE
    Qt6::Test
    llocr_runtime
)
add_test(NAME test_hardware_info COMMAND test_hardware_info)

# ---- Stage E: model installer (removeModel / multi-quant) ----------------
qt_add_executable(test_model_installer test_model_installer.cpp)

# The built-in preset catalogs are Qt resources; register them under the
# same :/profiles/ prefix so ModelInstaller::reloadPresetsInternal resolves
# them.
llocr_add_profile_resources(test_model_installer)

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
llocr_add_profile_resources(test_model_preset_catalog)

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
# ---- Core transport: LlamaClient reentrancy (C++-I4) ------------------------
qt_add_executable(test_llama_client test_llama_client.cpp)

target_link_libraries(test_llama_client PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_llama_client COMMAND test_llama_client)

# ---- The block state machine (ADR 164): two layers, pure transitions -------
qt_add_executable(test_box_transitions test_box_transitions.cpp)

target_link_libraries(test_box_transitions PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_box_transitions COMMAND test_box_transitions)

# ---- What a correction changed, word by word (ADR 186) ----------------------
qt_add_executable(test_text_diff test_text_diff.cpp)

target_link_libraries(test_text_diff PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_text_diff COMMAND test_text_diff)

# ---- The formula text: the canonical form and the syntax verdict (ADR 194) --
qt_add_executable(test_latex_text test_latex_text.cpp)

target_link_libraries(test_latex_text PRIVATE
    Qt6::Test
    llocr_core
)
add_test(NAME test_latex_text COMMAND test_latex_text)

# ---- Log view sync: incremental QTextDocument mirroring (QML-I3) ------------
qt_add_executable(test_log_document_sync test_log_document_sync.cpp)

target_link_libraries(test_log_document_sync PRIVATE
    Qt6::Test
    Qt6::Gui
    llocr_runtime
)
add_test(NAME test_log_document_sync COMMAND test_log_document_sync)

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
