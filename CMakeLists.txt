cmake_minimum_required(VERSION 3.24)

project(music_metadata_studio VERSION 0.1.0 LANGUAGES CXX)

add_library(djmeta_core STATIC
    src/core/normalizer.cpp
    src/core/batch_plan.cpp
    src/core/write_guard.cpp
    src/core/physical_selection.cpp
    src/core/external_cue.cpp
    src/core/cue_metadata.cpp
    src/core/cue_association.cpp
    src/core/staging.cpp
    src/core/routing_overrides.cpp
    src/core/batch_preview.cpp
    src/core/table_layout.cpp
    src/core/metadata_diff.cpp
    src/core/track_review.cpp
    src/core/review_decisions.cpp
    src/core/rule_loader.cpp
    src/core/analysis_json.cpp
)
target_include_directories(djmeta_core PUBLIC include)
target_compile_features(djmeta_core PUBLIC cxx_std_20)
set_target_properties(djmeta_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(djmeta_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(djmeta_native SHARED
    src/interop/c_api.cpp
)
target_link_libraries(djmeta_native PRIVATE djmeta_core)
target_include_directories(djmeta_native PUBLIC include)
target_compile_features(djmeta_native PUBLIC cxx_std_20)
target_compile_definitions(djmeta_native PRIVATE DJMETA_BUILD_DLL=1)

if(MSVC)
    target_compile_options(djmeta_native PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_native PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_core_tests
    tests/core_tests.cpp
)
target_link_libraries(djmeta_core_tests PRIVATE djmeta_core)
target_compile_features(djmeta_core_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(djmeta_core_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_c_api_tests
    tests/c_api_tests.cpp
)
target_link_libraries(djmeta_c_api_tests PRIVATE djmeta_native)
target_include_directories(djmeta_c_api_tests PRIVATE include)
target_compile_features(djmeta_c_api_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(djmeta_c_api_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_c_api_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_physical_selection_tests tests/physical_selection_tests.cpp)
target_link_libraries(djmeta_physical_selection_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_selection_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_selection_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_selection_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_write_guard_tests tests/write_guard_tests.cpp)
target_link_libraries(djmeta_write_guard_tests PRIVATE djmeta_core)
target_compile_features(djmeta_write_guard_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_write_guard_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_write_guard_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_batch_plan_tests
    tests/batch_plan_tests.cpp
)
target_link_libraries(djmeta_batch_plan_tests PRIVATE djmeta_core)
target_compile_features(djmeta_batch_plan_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(djmeta_batch_plan_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_batch_plan_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_external_cue_tests
    tests/external_cue_tests.cpp
)
target_link_libraries(djmeta_external_cue_tests PRIVATE djmeta_core)
target_compile_features(djmeta_external_cue_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(djmeta_external_cue_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_external_cue_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_cue_association_tests
    tests/cue_association_tests.cpp
)
target_link_libraries(djmeta_cue_association_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_association_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(djmeta_cue_association_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_association_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_staging_tests
    tests/staging_tests.cpp
)
target_link_libraries(djmeta_staging_tests PRIVATE djmeta_core)
target_compile_features(djmeta_staging_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_staging_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_staging_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_route_profiles_tests
    tests/route_profiles_tests.cpp
)
target_include_directories(djmeta_route_profiles_tests PRIVATE src/foobar)
target_compile_features(djmeta_route_profiles_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_route_profiles_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_route_profiles_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_routing_overrides_tests
    tests/routing_overrides_tests.cpp
)
target_link_libraries(djmeta_routing_overrides_tests PRIVATE djmeta_core)
target_compile_features(djmeta_routing_overrides_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_routing_overrides_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_routing_overrides_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_batch_preview_tests
    tests/batch_preview_tests.cpp
)
target_link_libraries(djmeta_batch_preview_tests PRIVATE djmeta_core)
target_compile_features(djmeta_batch_preview_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_batch_preview_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_batch_preview_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_table_layout_tests
    tests/table_layout_tests.cpp
)
target_link_libraries(djmeta_table_layout_tests PRIVATE djmeta_core)
target_compile_features(djmeta_table_layout_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_table_layout_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_table_layout_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_metadata_diff_tests tests/metadata_diff_tests.cpp)
target_link_libraries(djmeta_metadata_diff_tests PRIVATE djmeta_core)
target_compile_features(djmeta_metadata_diff_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_metadata_diff_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_metadata_diff_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_track_review_tests tests/track_review_tests.cpp)
target_link_libraries(djmeta_track_review_tests PRIVATE djmeta_core)
target_compile_features(djmeta_track_review_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_track_review_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_track_review_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    # Link the real production dialog resource into a small SDK-free Windows
    # executable. These checks exercise actual HWND controls, not text grep.
    enable_language(RC)
    add_executable(djmeta_native_preview_controls_tests
        tests/native_preview_controls_tests.cpp
        src/foobar/component.rc
    )
    target_compile_features(djmeta_native_preview_controls_tests PRIVATE cxx_std_20)
    target_include_directories(djmeta_native_preview_controls_tests PRIVATE include)
    target_link_libraries(djmeta_native_preview_controls_tests PRIVATE
        comctl32 user32)
    if(MSVC)
        target_compile_options(djmeta_native_preview_controls_tests PRIVATE
            /W4 /WX /permissive-)
    endif()
endif()

add_executable(djmeta_review_decisions_tests tests/review_decisions_tests.cpp)
target_link_libraries(djmeta_review_decisions_tests PRIVATE djmeta_core)
target_compile_features(djmeta_review_decisions_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_review_decisions_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_review_decisions_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()


# Provider-independent online metadata candidate engine is read-only and
# header-only in P0; the test exercises it without network or foobar SDK.
add_executable(djmeta_online_match_tests tests/online_match_tests.cpp)
target_include_directories(djmeta_online_match_tests PRIVATE include)
target_compile_features(djmeta_online_match_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_online_match_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_online_match_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Read-only release / edition alignment tests. Independent of foobar and network.
add_executable(djmeta_online_release_tests tests/online_release_tests.cpp)
add_executable(djmeta_online_release_fuzz_tests tests/online_release_fuzz.cpp)
foreach(target IN ITEMS djmeta_online_release_tests djmeta_online_release_fuzz_tests)
    target_include_directories(${target} PRIVATE include)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# Immutable online field-evidence preview, independent of host/HTTP.
add_executable(djmeta_online_fields_tests tests/online_fields_tests.cpp)
target_include_directories(djmeta_online_fields_tests PRIVATE include)
target_compile_features(djmeta_online_fields_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_online_fields_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_online_fields_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Manual candidate intake is a strictly read-only local parser; no foobar SDK or HTTP.
add_executable(djmeta_online_intake_tests tests/online_intake_tests.cpp)
target_include_directories(djmeta_online_intake_tests PRIVATE include)
target_compile_features(djmeta_online_intake_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_online_intake_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_online_intake_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Identical read-only semantic inventory for external and embedded text CUE.
add_executable(djmeta_cue_metadata_tests tests/cue_metadata_tests.cpp)
target_link_libraries(djmeta_cue_metadata_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_metadata_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_metadata_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_metadata_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# External/embedded CUE track snapshots feed the source-independent release matcher.
add_executable(djmeta_cue_online_bridge_tests tests/cue_online_bridge_tests.cpp)
target_link_libraries(djmeta_cue_online_bridge_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_online_bridge_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_online_bridge_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_online_bridge_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Source attributed CUE album and virtual track field-diff preview.
add_executable(djmeta_cue_online_review_tests tests/cue_online_review_tests.cpp)
target_link_libraries(djmeta_cue_online_review_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_online_review_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_online_review_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_online_review_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# DPI-independent persisted Win32 dialog-size schema (core-only C++ test).
add_executable(djmeta_preview_window_geometry_tests tests/preview_window_geometry_tests.cpp)
target_include_directories(djmeta_preview_window_geometry_tests PRIVATE include)
target_compile_features(djmeta_preview_window_geometry_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_preview_window_geometry_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_preview_window_geometry_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Native-free CUE clipboard presentation parity, no writer or HTTP dependencies.
add_executable(djmeta_cue_manual_preview_tests tests/cue_manual_preview_tests.cpp)
target_link_libraries(djmeta_cue_manual_preview_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_manual_preview_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_manual_preview_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_manual_preview_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Official MusicBrainz WS/2 offline fixture parser and request policy, no network I/O.
add_executable(djmeta_musicbrainz_provider_tests tests/musicbrainz_provider_tests.cpp)
target_link_libraries(djmeta_musicbrainz_provider_tests PRIVATE djmeta_core)
target_compile_features(djmeta_musicbrainz_provider_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_musicbrainz_provider_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_musicbrainz_provider_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME djmeta_musicbrainz_provider_tests COMMAND djmeta_musicbrainz_provider_tests)
add_test(NAME djmeta_cue_manual_preview_tests COMMAND djmeta_cue_manual_preview_tests)
add_test(NAME djmeta_preview_window_geometry_tests COMMAND djmeta_preview_window_geometry_tests)
add_test(NAME djmeta_cue_online_review_tests COMMAND djmeta_cue_online_review_tests)
add_test(NAME djmeta_cue_online_bridge_tests COMMAND djmeta_cue_online_bridge_tests)
add_test(NAME djmeta_cue_metadata_tests COMMAND djmeta_cue_metadata_tests)
add_test(NAME djmeta_online_intake_tests COMMAND djmeta_online_intake_tests)
add_test(NAME djmeta_online_fields_tests COMMAND djmeta_online_fields_tests)
add_test(NAME djmeta_online_release_tests COMMAND djmeta_online_release_tests)
add_test(NAME djmeta_online_release_fuzz_tests COMMAND djmeta_online_release_fuzz_tests)
add_test(NAME djmeta_online_match_tests COMMAND djmeta_online_match_tests)
add_test(NAME djmeta_write_guard_tests COMMAND djmeta_write_guard_tests)
add_test(NAME djmeta_physical_selection_tests COMMAND djmeta_physical_selection_tests)
add_test(NAME djmeta_review_decisions_tests COMMAND djmeta_review_decisions_tests)
if(WIN32)
    add_test(NAME djmeta_native_preview_controls_tests
             COMMAND djmeta_native_preview_controls_tests)
endif()
add_test(NAME djmeta_track_review_tests COMMAND djmeta_track_review_tests)
add_test(NAME djmeta_metadata_diff_tests COMMAND djmeta_metadata_diff_tests)
add_test(NAME djmeta_table_layout_tests COMMAND djmeta_table_layout_tests)
add_test(NAME djmeta_batch_preview_tests COMMAND djmeta_batch_preview_tests)
add_test(NAME djmeta_routing_overrides_tests COMMAND djmeta_routing_overrides_tests)
add_test(NAME djmeta_route_profiles_tests COMMAND djmeta_route_profiles_tests)
add_test(NAME djmeta_staging_tests COMMAND djmeta_staging_tests)
add_test(NAME djmeta_cue_association_tests COMMAND djmeta_cue_association_tests)
add_test(NAME djmeta_external_cue_tests COMMAND djmeta_external_cue_tests)
add_test(NAME djmeta_batch_plan_tests COMMAND djmeta_batch_plan_tests)
add_test(NAME djmeta_core_tests COMMAND djmeta_core_tests)
set_tests_properties(djmeta_core_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME djmeta_c_api_tests COMMAND djmeta_c_api_tests)
