cmake_minimum_required(VERSION 3.24)

project(music_metadata_studio VERSION 0.1.0 LANGUAGES CXX)

add_library(djmeta_core STATIC
    src/core/precision_cue.cpp
    src/core/precision_cue_match.cpp
    src/core/precision_cue_assemble.cpp
    src/core/precision_cue_apple.cpp
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
if(WIN32)
    target_sources(djmeta_core PRIVATE
        src/core/precision_cue_apple_http_win.cpp
        src/core/precision_cue_deezer_http_win.cpp)
    target_link_libraries(djmeta_core PRIVATE winhttp)
endif()
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

# Only an in-memory verified physical tag postimage, no SDK or writer.
# Selected online physical tags pass an independent exact postimage guard.
add_executable(djmeta_online_physical_postimage_tests tests/online_physical_postimage_tests.cpp)
target_link_libraries(djmeta_online_physical_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_online_physical_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_online_physical_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_online_physical_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Fresh OS/foobar source, target and physical tag evidence must reproduce
# one frozen batch. This produces only detached postimages, NEVER writes.
add_executable(djmeta_physical_apply_preflight_tests tests/physical_apply_preflight_tests.cpp)
target_link_libraries(djmeta_physical_apply_preflight_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_apply_preflight_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_apply_preflight_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_apply_preflight_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_physical_tag_postimage_tests tests/physical_tag_postimage_tests.cpp)
target_link_libraries(djmeta_physical_tag_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_tag_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_tag_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_tag_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Read-only existing CUE FILE same-directory locator; no generator or writer.
add_executable(djmeta_cue_reference_locator_tests tests/cue_reference_locator_tests.cpp)
target_link_libraries(djmeta_cue_reference_locator_tests PRIVATE djmeta_core)
target_include_directories(djmeta_cue_reference_locator_tests PRIVATE include)
target_compile_features(djmeta_cue_reference_locator_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_reference_locator_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_reference_locator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Complete captured-snapshot audit of normalizer decisions and raw FileOps
# candidates; deliberately never grants a writer approval.
add_executable(djmeta_workflow_preview_audit_tests
    tests/workflow_preview_audit_tests.cpp)
target_link_libraries(djmeta_workflow_preview_audit_tests PRIVATE djmeta_core)
target_compile_features(djmeta_workflow_preview_audit_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_workflow_preview_audit_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_workflow_preview_audit_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
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

# Online source selection cannot retain the previous track's auto query.
# Header-only, cross-platform UI policy regression, no host or network.
add_executable(djmeta_online_query_prefill_tests tests/online_query_prefill_tests.cpp)
target_include_directories(djmeta_online_query_prefill_tests PRIVATE include)
target_compile_features(djmeta_online_query_prefill_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_online_query_prefill_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_online_query_prefill_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Pure CUE metadata postimage; NO writer, filesystem, host or side effects.
add_executable(djmeta_cue_value_postimage_tests tests/cue_value_postimage_tests.cpp)
target_link_libraries(djmeta_cue_value_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_value_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_value_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_value_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Pure and fail-closed per-field online review -> CUE byte postimage.
# No SDK, filesystem write or automatic approval.
# Reconstruct exact CUE source-byte postimages from verified official edition
# before binding them into a write-disabled batch plan. No SDK or disk I/O.
add_executable(djmeta_cue_value_batch_adapter_tests tests/cue_value_batch_adapter_tests.cpp)
target_link_libraries(djmeta_cue_value_batch_adapter_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_value_batch_adapter_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_value_batch_adapter_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_value_batch_adapter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_cue_review_postimage_tests tests/cue_review_postimage_tests.cpp)
target_link_libraries(djmeta_cue_review_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_review_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_review_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_review_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Host file physical-key validity must be qualified without loading foobar.
# Strictly source/destination read-only verification; no native I/O in this test.
add_executable(djmeta_host_file_probe_identity_tests tests/host_file_probe_identity_tests.cpp)
target_include_directories(djmeta_host_file_probe_identity_tests PRIVATE src/foobar)
target_compile_features(djmeta_host_file_probe_identity_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_host_file_probe_identity_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_host_file_probe_identity_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# MusicBrainz CUE global-diff qualification uses the actual complete edition
# identity and one-to-one track alignment, without trusting partial matches.
add_executable(djmeta_cue_musicbrainz_release_gate_tests
    tests/cue_musicbrainz_release_gate_tests.cpp)
target_link_libraries(djmeta_cue_musicbrainz_release_gate_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_musicbrainz_release_gate_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_musicbrainz_release_gate_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_musicbrainz_release_gate_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
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

# Beatport v4 offline catalog parser: DJ mix/version, evidence and edition
# matching contracts. Never requests OAuth credentials or live HTTP.
add_executable(djmeta_beatport_provider_tests tests/beatport_provider_tests.cpp)
target_link_libraries(djmeta_beatport_provider_tests PRIVATE djmeta_core)
target_compile_features(djmeta_beatport_provider_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_beatport_provider_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_beatport_provider_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# User-initiated Beatport JSON clipboard -> complete read-only field comparison.
# No provider network access, scraper, credentials or write capability.
add_executable(djmeta_beatport_manual_intake_tests tests/beatport_manual_intake_tests.cpp)
target_link_libraries(djmeta_beatport_manual_intake_tests PRIVATE djmeta_core)
target_compile_features(djmeta_beatport_manual_intake_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_beatport_manual_intake_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_beatport_manual_intake_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Beatport exact-edition CUE source inventory and ambiguous-track fail-closed review.
add_executable(djmeta_beatport_cue_source_tests tests/beatport_cue_source_tests.cpp)
target_link_libraries(djmeta_beatport_cue_source_tests PRIVATE djmeta_core)
target_compile_features(djmeta_beatport_cue_source_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_beatport_cue_source_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_beatport_cue_source_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
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

# Official Discogs catalog offline fixture/parser and auth-gated URL contract.
# No network, credentials, user collection or filesystem mutation in tests.
add_executable(djmeta_discogs_release_review_tests tests/discogs_release_review_tests.cpp)
target_link_libraries(djmeta_discogs_release_review_tests PRIVATE djmeta_core)
target_compile_features(djmeta_discogs_release_review_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_discogs_release_review_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_discogs_release_review_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_discogs_provider_tests tests/discogs_provider_tests.cpp)
target_link_libraries(djmeta_discogs_provider_tests PRIVATE djmeta_core)
target_compile_features(djmeta_discogs_provider_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_discogs_provider_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_discogs_provider_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Native Windows external-CUE transaction is tested against an isolated
# synthetic temp directory; it is not compiled into the foobar component.
if(WIN32)
    add_executable(djmeta_external_cue_transaction_win_tests
        tests/external_cue_transaction_win_tests.cpp
        src/writer/external_cue_transaction_win.cpp)
    target_link_libraries(djmeta_external_cue_transaction_win_tests PRIVATE djmeta_core)
    target_include_directories(djmeta_external_cue_transaction_win_tests PRIVATE src/writer include)
    target_compile_features(djmeta_external_cue_transaction_win_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(djmeta_external_cue_transaction_win_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(djmeta_external_cue_transaction_win_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

# Fixture filesystem write tests only: never linked to native foobar component.
add_executable(djmeta_cue_fixture_writer_tests tests/cue_fixture_writer_tests.cpp)
target_link_libraries(djmeta_cue_fixture_writer_tests PRIVATE djmeta_core)
target_compile_features(djmeta_cue_fixture_writer_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_cue_fixture_writer_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_cue_fixture_writer_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_physical_sdk_postimage_tests tests/physical_sdk_postimage_tests.cpp)
target_link_libraries(djmeta_physical_sdk_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_sdk_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_sdk_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_sdk_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_embedded_cue_sdk_postimage_tests tests/embedded_cue_sdk_postimage_tests.cpp)
target_link_libraries(djmeta_embedded_cue_sdk_postimage_tests PRIVATE djmeta_core)
target_compile_features(djmeta_embedded_cue_sdk_postimage_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_embedded_cue_sdk_postimage_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_embedded_cue_sdk_postimage_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_physical_write_contract_tests tests/physical_write_contract_tests.cpp)
target_link_libraries(djmeta_physical_write_contract_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_write_contract_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_write_contract_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_write_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_physical_write_confirmation_tests tests/physical_write_confirmation_tests.cpp)
target_link_libraries(djmeta_physical_write_confirmation_tests PRIVATE djmeta_core)
target_compile_features(djmeta_physical_write_confirmation_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_physical_write_confirmation_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_physical_write_confirmation_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_filter_once_tests tests/filter_once_tests.cpp)
target_compile_features(djmeta_filter_once_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_filter_once_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_filter_once_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# Profile-native workspace defaults: stable versioned schema, no write rights.
add_executable(djmeta_workspace_defaults_tests tests/workspace_defaults_tests.cpp)
target_include_directories(djmeta_workspace_defaults_tests PRIVATE include)
target_compile_features(djmeta_workspace_defaults_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_workspace_defaults_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_workspace_defaults_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Explicit local + verified online decisions share one physical postimage,
# one batch fingerprint, and the existing disabled SDK preflight boundary.
add_executable(djmeta_unified_review_batch_tests
    tests/unified_review_batch_tests.cpp)
target_link_libraries(djmeta_unified_review_batch_tests PRIVATE djmeta_core)
target_compile_features(djmeta_unified_review_batch_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_unified_review_batch_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_unified_review_batch_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

# Shared exact-edition Precision CUE generator and synthetic safety suite
add_executable(djmeta_precision_cue_tests tests/precision_cue_tests.cpp)
target_link_libraries(djmeta_precision_cue_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    # Never contact external providers from CI; these are pure request-policy tests.
    add_executable(djmeta_precision_cue_deezer_http_tests
        tests/precision_cue_deezer_http_tests.cpp)
    target_link_libraries(djmeta_precision_cue_deezer_http_tests PRIVATE djmeta_core)
    target_compile_features(djmeta_precision_cue_deezer_http_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(djmeta_precision_cue_deezer_http_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(djmeta_precision_cue_deezer_http_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_executable(djmeta_precision_cue_apple_http_tests
        tests/precision_cue_apple_http_tests.cpp)
    target_link_libraries(djmeta_precision_cue_apple_http_tests PRIVATE djmeta_core)
    target_compile_features(djmeta_precision_cue_apple_http_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(djmeta_precision_cue_apple_http_tests PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(djmeta_precision_cue_apple_http_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

add_executable(djmeta_precision_cue_apple_tests
    tests/precision_cue_apple_tests.cpp)
target_link_libraries(djmeta_precision_cue_apple_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_apple_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_apple_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_apple_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Strict, transport-free Deezer official album JSON parsing fixtures.
add_executable(djmeta_precision_cue_deezer_tests
    tests/precision_cue_deezer_tests.cpp)
target_link_libraries(djmeta_precision_cue_deezer_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_deezer_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_deezer_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_deezer_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_precision_cue_apple_link_tests
    tests/precision_cue_apple_link_tests.cpp)
target_link_libraries(djmeta_precision_cue_apple_link_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_apple_link_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_apple_link_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_apple_link_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_precision_cue_source_consensus_tests
    tests/precision_cue_source_consensus_tests.cpp)
target_link_libraries(djmeta_precision_cue_source_consensus_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_source_consensus_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_source_consensus_tests
        PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_source_consensus_tests
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# One offline, synthetic end-to-end album -> timing -> explicit external create gate.
add_executable(djmeta_precision_cue_e2e_tests tests/precision_cue_e2e_tests.cpp)
target_link_libraries(djmeta_precision_cue_e2e_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_e2e_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_e2e_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_e2e_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_precision_cue_assemble_tests tests/precision_cue_assemble_tests.cpp)
target_link_libraries(djmeta_precision_cue_assemble_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_assemble_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_assemble_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_assemble_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_precision_cue_write_preflight_tests
    tests/precision_cue_write_preflight_tests.cpp)
target_link_libraries(djmeta_precision_cue_write_preflight_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_write_preflight_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_write_preflight_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_write_preflight_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(djmeta_precision_cue_embedded_probe_tests
    tests/precision_cue_embedded_probe_tests.cpp)
target_link_libraries(djmeta_precision_cue_embedded_probe_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_embedded_probe_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_embedded_probe_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_embedded_probe_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Conservatively plan an ID3 TXXX CUESHEET tag postimage WITHOUT writing.
add_executable(djmeta_precision_cue_id3_patch_tests
    tests/precision_cue_id3_patch_tests.cpp)
target_link_libraries(djmeta_precision_cue_id3_patch_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_id3_patch_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_id3_patch_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_id3_patch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Pure lossless FLAC Vorbis CUESHEET postimage + bounded read-only prefix
# tests. A productive FLAC metadata writer is intentionally NOT linked.
add_executable(djmeta_precision_cue_flac_patch_tests
    tests/precision_cue_flac_patch_tests.cpp)
target_link_libraries(djmeta_precision_cue_flac_patch_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_flac_patch_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_flac_patch_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_flac_patch_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Full-file SHA256 and immutable physical audio snapshot prerequisites for
# future transaction/rollback, with no productive media-write API.
add_executable(djmeta_precision_cue_sha256_stream_tests
    tests/precision_cue_sha256_stream_tests.cpp)
target_link_libraries(djmeta_precision_cue_sha256_stream_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_sha256_stream_tests PRIVATE cxx_std_20)
add_executable(djmeta_precision_cue_full_file_snapshot_tests
    tests/precision_cue_full_file_snapshot_tests.cpp)
target_link_libraries(djmeta_precision_cue_full_file_snapshot_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_full_file_snapshot_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_sha256_stream_tests PRIVATE /W4 /WX /permissive-)
    target_compile_options(djmeta_precision_cue_full_file_snapshot_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_sha256_stream_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(djmeta_precision_cue_full_file_snapshot_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Embedded CUE candidate verification is strictly temporary and
# never provides a publication or physical-tag overwrite operation.
add_executable(djmeta_precision_cue_embedded_stage_dryrun_tests
    tests/precision_cue_embedded_stage_dryrun_tests.cpp)
target_link_libraries(djmeta_precision_cue_embedded_stage_dryrun_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_embedded_stage_dryrun_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_embedded_stage_dryrun_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_embedded_stage_dryrun_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Create-only recovery backup preparation for an eventual embedded CUE
# transaction. Only synthetic fixtures use this backend; there is no host
# write, replace-original, recovery, or automatic rollback entry point.
add_executable(djmeta_precision_cue_embedded_recovery_backup_tests
    tests/precision_cue_embedded_recovery_backup_tests.cpp)
target_link_libraries(djmeta_precision_cue_embedded_recovery_backup_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_embedded_recovery_backup_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_embedded_recovery_backup_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_embedded_recovery_backup_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Crash recovery inventory is READ ONLY. Pending backup clones are untrusted;
# verified full-file SHA matches never authorize automatic restoration.
add_executable(djmeta_precision_cue_embedded_recovery_inventory_tests
    tests/precision_cue_embedded_recovery_inventory_tests.cpp)
target_link_libraries(djmeta_precision_cue_embedded_recovery_inventory_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_embedded_recovery_inventory_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_embedded_recovery_inventory_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_embedded_recovery_inventory_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Independent synthetic commit-evidence review and physical storage envelope.
# These tests NEVER enable an embedded write path or touch user media.
add_executable(djmeta_precision_cue_embedded_commit_review_tests
    tests/precision_cue_embedded_commit_review_tests.cpp)
target_link_libraries(djmeta_precision_cue_embedded_commit_review_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_embedded_commit_review_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_embedded_commit_review_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_embedded_commit_review_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_precision_cue_physical_envelope_tests
    tests/precision_cue_physical_envelope_tests.cpp)
target_link_libraries(djmeta_precision_cue_physical_envelope_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_physical_envelope_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_physical_envelope_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_physical_envelope_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(djmeta_precision_cue_match_tests tests/precision_cue_match_tests.cpp)
target_link_libraries(djmeta_precision_cue_match_tests PRIVATE djmeta_core)
target_compile_features(djmeta_precision_cue_match_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(djmeta_precision_cue_match_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(djmeta_precision_cue_match_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
if(WIN32)

endif()
add_test(NAME djmeta_precision_cue_apple_tests COMMAND djmeta_precision_cue_apple_tests)
add_test(NAME djmeta_precision_cue_apple_link_tests COMMAND djmeta_precision_cue_apple_link_tests)
add_test(NAME djmeta_precision_cue_deezer_tests COMMAND djmeta_precision_cue_deezer_tests)
add_test(NAME djmeta_precision_cue_source_consensus_tests COMMAND djmeta_precision_cue_source_consensus_tests)
add_test(NAME djmeta_precision_cue_e2e_tests COMMAND djmeta_precision_cue_e2e_tests)
add_test(NAME djmeta_precision_cue_assemble_tests COMMAND djmeta_precision_cue_assemble_tests)
add_test(NAME djmeta_precision_cue_write_preflight_tests COMMAND djmeta_precision_cue_write_preflight_tests)
add_test(NAME djmeta_precision_cue_match_tests COMMAND djmeta_precision_cue_match_tests)
add_test(NAME djmeta_precision_cue_embedded_probe_tests COMMAND djmeta_precision_cue_embedded_probe_tests)
add_test(NAME djmeta_precision_cue_id3_patch_tests COMMAND djmeta_precision_cue_id3_patch_tests)
add_test(NAME djmeta_precision_cue_flac_patch_tests COMMAND djmeta_precision_cue_flac_patch_tests)
add_test(NAME djmeta_precision_cue_sha256_stream_tests COMMAND djmeta_precision_cue_sha256_stream_tests)
add_test(NAME djmeta_precision_cue_full_file_snapshot_tests COMMAND djmeta_precision_cue_full_file_snapshot_tests)
add_test(NAME djmeta_precision_cue_embedded_stage_dryrun_tests COMMAND djmeta_precision_cue_embedded_stage_dryrun_tests)
add_test(NAME djmeta_precision_cue_embedded_recovery_backup_tests COMMAND djmeta_precision_cue_embedded_recovery_backup_tests)
add_test(NAME djmeta_precision_cue_embedded_recovery_inventory_tests COMMAND djmeta_precision_cue_embedded_recovery_inventory_tests)
add_test(NAME djmeta_precision_cue_embedded_commit_review_tests COMMAND djmeta_precision_cue_embedded_commit_review_tests)
add_test(NAME djmeta_precision_cue_physical_envelope_tests COMMAND djmeta_precision_cue_physical_envelope_tests)
add_test(NAME djmeta_precision_cue_tests COMMAND djmeta_precision_cue_tests)
add_test(NAME djmeta_unified_review_batch_tests COMMAND djmeta_unified_review_batch_tests)
add_test(NAME djmeta_workspace_defaults_tests COMMAND djmeta_workspace_defaults_tests)
add_test(NAME djmeta_filter_once_tests COMMAND djmeta_filter_once_tests)
add_test(NAME djmeta_physical_write_confirmation_tests COMMAND djmeta_physical_write_confirmation_tests)
add_test(NAME djmeta_physical_write_contract_tests COMMAND djmeta_physical_write_contract_tests)
add_test(NAME djmeta_embedded_cue_sdk_postimage_tests COMMAND djmeta_embedded_cue_sdk_postimage_tests)
add_test(NAME djmeta_physical_sdk_postimage_tests COMMAND djmeta_physical_sdk_postimage_tests)
add_test(NAME djmeta_cue_fixture_writer_tests COMMAND djmeta_cue_fixture_writer_tests)
add_test(NAME djmeta_beatport_provider_tests COMMAND djmeta_beatport_provider_tests)
add_test(NAME djmeta_beatport_manual_intake_tests COMMAND djmeta_beatport_manual_intake_tests)
add_test(NAME djmeta_beatport_cue_source_tests COMMAND djmeta_beatport_cue_source_tests)
add_test(NAME djmeta_discogs_provider_tests COMMAND djmeta_discogs_provider_tests)
add_test(NAME djmeta_discogs_release_review_tests COMMAND djmeta_discogs_release_review_tests)
add_test(NAME djmeta_musicbrainz_provider_tests COMMAND djmeta_musicbrainz_provider_tests)
add_test(NAME djmeta_cue_manual_preview_tests COMMAND djmeta_cue_manual_preview_tests)
add_test(NAME djmeta_preview_window_geometry_tests COMMAND djmeta_preview_window_geometry_tests)
add_test(NAME djmeta_host_file_probe_identity_tests COMMAND djmeta_host_file_probe_identity_tests)
add_test(NAME djmeta_cue_online_review_tests COMMAND djmeta_cue_online_review_tests)
add_test(NAME djmeta_cue_online_bridge_tests COMMAND djmeta_cue_online_bridge_tests)
add_test(NAME djmeta_cue_metadata_tests COMMAND djmeta_cue_metadata_tests)
add_test(NAME djmeta_cue_musicbrainz_release_gate_tests COMMAND djmeta_cue_musicbrainz_release_gate_tests)
add_test(NAME djmeta_cue_value_postimage_tests COMMAND djmeta_cue_value_postimage_tests)
add_test(NAME djmeta_cue_review_postimage_tests COMMAND djmeta_cue_review_postimage_tests)
add_test(NAME djmeta_cue_value_batch_adapter_tests COMMAND djmeta_cue_value_batch_adapter_tests)
add_test(NAME djmeta_online_intake_tests COMMAND djmeta_online_intake_tests)
add_test(NAME djmeta_online_query_prefill_tests COMMAND djmeta_online_query_prefill_tests)
add_test(NAME djmeta_online_fields_tests COMMAND djmeta_online_fields_tests)
add_test(NAME djmeta_online_release_tests COMMAND djmeta_online_release_tests)
add_test(NAME djmeta_online_release_fuzz_tests COMMAND djmeta_online_release_fuzz_tests)
add_test(NAME djmeta_online_match_tests COMMAND djmeta_online_match_tests)
add_test(NAME djmeta_write_guard_tests COMMAND djmeta_write_guard_tests)
add_test(NAME djmeta_physical_tag_postimage_tests COMMAND djmeta_physical_tag_postimage_tests)
add_test(NAME djmeta_physical_apply_preflight_tests COMMAND djmeta_physical_apply_preflight_tests)
add_test(NAME djmeta_online_physical_postimage_tests COMMAND djmeta_online_physical_postimage_tests)
add_test(NAME djmeta_physical_selection_tests COMMAND djmeta_physical_selection_tests)
add_test(NAME djmeta_review_decisions_tests COMMAND djmeta_review_decisions_tests)
if(WIN32)
    add_test(NAME djmeta_native_preview_controls_tests
             COMMAND djmeta_native_preview_controls_tests)
    add_test(NAME djmeta_external_cue_transaction_win_tests
             COMMAND djmeta_external_cue_transaction_win_tests)
endif()
add_test(NAME djmeta_track_review_tests COMMAND djmeta_track_review_tests)
add_test(NAME djmeta_metadata_diff_tests COMMAND djmeta_metadata_diff_tests)
add_test(NAME djmeta_table_layout_tests COMMAND djmeta_table_layout_tests)
add_test(NAME djmeta_workflow_preview_audit_tests COMMAND djmeta_workflow_preview_audit_tests)
add_test(NAME djmeta_batch_preview_tests COMMAND djmeta_batch_preview_tests)
add_test(NAME djmeta_routing_overrides_tests COMMAND djmeta_routing_overrides_tests)
add_test(NAME djmeta_route_profiles_tests COMMAND djmeta_route_profiles_tests)
add_test(NAME djmeta_staging_tests COMMAND djmeta_staging_tests)
add_test(NAME djmeta_cue_reference_locator_tests COMMAND djmeta_cue_reference_locator_tests)
add_test(NAME djmeta_cue_association_tests COMMAND djmeta_cue_association_tests)
add_test(NAME djmeta_external_cue_tests COMMAND djmeta_external_cue_tests)
add_test(NAME djmeta_batch_plan_tests COMMAND djmeta_batch_plan_tests)
add_test(NAME djmeta_core_tests COMMAND djmeta_core_tests)
set_tests_properties(djmeta_core_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME djmeta_c_api_tests COMMAND djmeta_c_api_tests)
