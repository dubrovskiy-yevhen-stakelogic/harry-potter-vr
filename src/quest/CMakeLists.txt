add_library(hpvr_quest_lifecycle STATIC
    src/quest_gesture.cpp
    src/quest_frontend.cpp
    src/quest_lighting.cpp
    src/quest_lifecycle.cpp
    src/quest_map_events.cpp
    src/quest_music_cache.cpp
    src/quest_spell_targets.cpp
    src/quest_view.cpp
)

target_include_directories(hpvr_quest_lifecycle PUBLIC include)
target_compile_features(hpvr_quest_lifecycle PUBLIC cxx_std_20)
target_link_libraries(hpvr_quest_lifecycle PUBLIC hpvr_wand)

if(MSVC)
    target_compile_options(hpvr_quest_lifecycle PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(hpvr_quest_lifecycle PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(hpvr_quest_package_resource_tests tests/quest_package_resource_tests.cpp)
    target_link_libraries(hpvr_quest_package_resource_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_package_resource_tests COMMAND hpvr_quest_package_resource_tests)
    add_executable(hpvr_quest_abyss_fog_tests tests/quest_abyss_fog_tests.cpp)
    target_link_libraries(hpvr_quest_abyss_fog_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_abyss_fog_tests COMMAND hpvr_quest_abyss_fog_tests)
    add_executable(hpvr_quest_effects_clock_tests tests/quest_effects_clock_tests.cpp)
    target_link_libraries(hpvr_quest_effects_clock_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_effects_clock_tests COMMAND hpvr_quest_effects_clock_tests)
    add_executable(hpvr_quest_pickup_audio_tests tests/quest_pickup_audio_tests.cpp)
    foreach(test_name IN ITEMS gnome gnome_visibility award_facing grid_push grid_motion grid_visual vertex_layout broom_flight broom_lesson broom_session broom_scene broom_avatar cast_permissions charms_lesson charms_block charms_scene chest campaign_progress report_menu peeves peeves_battle peeves_projectile malfoy dialogue return_scene)
        add_executable(hpvr_quest_${test_name}_tests tests/quest_${test_name}_tests.cpp)
        target_link_libraries(hpvr_quest_${test_name}_tests PRIVATE hpvr_quest_lifecycle)
        add_test(NAME hpvr_quest_${test_name}_tests COMMAND hpvr_quest_${test_name}_tests)
    endforeach()
    target_link_libraries(hpvr_quest_pickup_audio_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_pickup_audio_tests COMMAND hpvr_quest_pickup_audio_tests)
    add_executable(hpvr_quest_fixture_effects_tests tests/quest_fixture_effects_tests.cpp)
    target_link_libraries(hpvr_quest_fixture_effects_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_fixture_effects_tests COMMAND hpvr_quest_fixture_effects_tests)
    add_executable(hpvr_quest_mover_visibility_tests tests/quest_mover_visibility_tests.cpp)
    target_link_libraries(hpvr_quest_mover_visibility_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_mover_visibility_tests COMMAND hpvr_quest_mover_visibility_tests)
    add_executable(hpvr_quest_voice_hint_tests tests/quest_voice_hint_tests.cpp)
    target_link_libraries(hpvr_quest_voice_hint_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_voice_hint_tests COMMAND hpvr_quest_voice_hint_tests)
    add_executable(hpvr_quest_c44_runtime_tests tests/quest_c44_runtime_tests.cpp)
    target_link_libraries(hpvr_quest_c44_runtime_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c44_runtime_tests COMMAND hpvr_quest_c44_runtime_tests)
    add_executable(hpvr_quest_voice_cast_tests tests/quest_voice_cast_tests.cpp)
    target_link_libraries(hpvr_quest_voice_cast_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_voice_cast_tests COMMAND hpvr_quest_voice_cast_tests)
    add_executable(hpvr_quest_death_transition_tests tests/quest_death_transition_tests.cpp)
    target_link_libraries(hpvr_quest_death_transition_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_death_transition_tests COMMAND hpvr_quest_death_transition_tests)
    add_executable(hpvr_quest_prepare_assets tools/quest_prepare_assets.cpp)
    target_link_libraries(hpvr_quest_prepare_assets PRIVATE hpvr_quest_lifecycle)
    add_executable(hpvr_quest_ambient_effects_tests tests/quest_ambient_effects_tests.cpp)
    target_link_libraries(hpvr_quest_ambient_effects_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_ambient_effects_tests COMMAND hpvr_quest_ambient_effects_tests)
    add_executable(hpvr_quest_prop_orientation_tests tests/quest_prop_orientation_tests.cpp)
    target_link_libraries(hpvr_quest_prop_orientation_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_prop_orientation_tests COMMAND hpvr_quest_prop_orientation_tests)
    add_executable(hpvr_quest_asset_cache_tests tests/quest_asset_cache_tests.cpp)
    target_link_libraries(hpvr_quest_asset_cache_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_asset_cache_tests COMMAND hpvr_quest_asset_cache_tests)
    add_executable(hpvr_quest_prepared_codec_tests tests/quest_prepared_codec_tests.cpp)
    target_link_libraries(hpvr_quest_prepared_codec_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_prepared_codec_tests COMMAND hpvr_quest_prepared_codec_tests)
    add_executable(hpvr_quest_startup_tests tests/quest_startup_tests.cpp)
    target_link_libraries(hpvr_quest_startup_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_startup_tests COMMAND hpvr_quest_startup_tests)
    add_executable(hpvr_quest_load_trace_tests tests/quest_load_trace_tests.cpp)
    target_compile_features(hpvr_quest_load_trace_tests PRIVATE cxx_std_20)
    add_test(NAME hpvr_quest_load_trace_tests COMMAND hpvr_quest_load_trace_tests)
    add_executable(hpvr_quest_target_marker_tests tests/quest_target_marker_tests.cpp)
    target_link_libraries(hpvr_quest_target_marker_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_target_marker_tests COMMAND hpvr_quest_target_marker_tests)
    add_executable(hpvr_quest_c41_tests tools/quest_c41_tests.cpp)
    target_link_libraries(hpvr_quest_c41_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c41_tests COMMAND hpvr_quest_c41_tests)
    add_executable(hpvr_quest_music_cache_tests tests/quest_music_cache_tests.cpp)
    target_link_libraries(hpvr_quest_music_cache_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_music_cache_tests COMMAND hpvr_quest_music_cache_tests)
    add_executable(hpvr_quest_c38_scene_probe tools/quest_c38_scene_probe.cpp)
    target_link_libraries(hpvr_quest_c38_scene_probe PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c38_scene_tests COMMAND hpvr_quest_c38_scene_probe)
    add_executable(hpvr_quest_c38_event_tests tools/quest_c38_event_tests.cpp)
    target_link_libraries(hpvr_quest_c38_event_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c38_event_tests COMMAND hpvr_quest_c38_event_tests)
    add_executable(hpvr_quest_c38_progress_tests tools/quest_c38_progress_tests.cpp)
    target_link_libraries(hpvr_quest_c38_progress_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c38_progress_tests COMMAND hpvr_quest_c38_progress_tests)
    add_executable(hpvr_quest_c37_defaults_tests tools/quest_c37_defaults_tests.cpp)
    target_link_libraries(hpvr_quest_c37_defaults_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c37_defaults_tests COMMAND hpvr_quest_c37_defaults_tests)
    add_executable(hpvr_quest_c36_frontend_tests tools/quest_c36_frontend_tests.cpp)
    add_executable(hpvr_quest_c44_settings_tests tests/quest_c44_settings_tests.cpp)
    add_executable(hpvr_quest_prop_motion_tests tests/quest_prop_motion_tests.cpp)
    add_executable(hpvr_quest_challenge_props_tests tests/quest_challenge_props_tests.cpp)
    target_link_libraries(hpvr_quest_challenge_props_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_challenge_props_tests COMMAND hpvr_quest_challenge_props_tests)
    target_link_libraries(hpvr_quest_prop_motion_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_prop_motion_tests COMMAND hpvr_quest_prop_motion_tests)
    target_link_libraries(hpvr_quest_c44_settings_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c44_settings_tests COMMAND hpvr_quest_c44_settings_tests)
    target_link_libraries(hpvr_quest_c36_frontend_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c36_frontend_tests COMMAND hpvr_quest_c36_frontend_tests)
    add_executable(hpvr_quest_c36_camera_tests tools/quest_c36_camera_tests.cpp)
    target_link_libraries(hpvr_quest_c36_camera_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c36_camera_tests COMMAND hpvr_quest_c36_camera_tests)
    add_executable(hpvr_quest_c35_tests tools/quest_c35_tests.cpp)
    target_link_libraries(hpvr_quest_c35_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c35_tests COMMAND hpvr_quest_c35_tests)
    add_executable(hpvr_quest_c35_ssr_tests tools/quest_c35_ssr_tests.cpp)
    target_compile_features(hpvr_quest_c35_ssr_tests PRIVATE cxx_std_20)
    add_test(NAME hpvr_quest_c35_ssr_tests COMMAND hpvr_quest_c35_ssr_tests)
    add_executable(hpvr_quest_c34_tests tools/quest_c34_tests.cpp)
    target_link_libraries(hpvr_quest_c34_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c34_tests COMMAND hpvr_quest_c34_tests)
    add_executable(hpvr_quest_c32_tests tools/quest_c32_tests.cpp)
    add_executable(hpvr_quest_c33_tests tools/quest_c33_tests.cpp)
    target_link_libraries(hpvr_quest_c33_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c33_tests COMMAND hpvr_quest_c33_tests)
    target_link_libraries(hpvr_quest_c32_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c32_tests COMMAND hpvr_quest_c32_tests)
    add_executable(hpvr_quest_c31_tests tools/quest_c31_tests.cpp)
    target_link_libraries(hpvr_quest_c31_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c31_tests COMMAND hpvr_quest_c31_tests)
    add_executable(hpvr_quest_c30_tests tools/quest_c30_tests.cpp)
    target_link_libraries(hpvr_quest_c30_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c30_tests COMMAND hpvr_quest_c30_tests)
    add_executable(hpvr_quest_c29_probe tools/quest_c29_probe.cpp)
    target_link_libraries(hpvr_quest_c29_probe PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c29_tests COMMAND hpvr_quest_c29_probe)
    add_executable(hpvr_quest_c28_tests tools/quest_c28_tests.cpp)
    target_link_libraries(hpvr_quest_c28_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c28_tests COMMAND hpvr_quest_c28_tests)
    add_executable(hpvr_quest_c25_tests tools/quest_c25_tests.cpp)
    target_link_libraries(hpvr_quest_c25_tests PRIVATE hpvr_quest_lifecycle)
    add_executable(hpvr_quest_c24_tests tools/quest_c24_tests.cpp)
    target_link_libraries(hpvr_quest_c24_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c24_tests COMMAND hpvr_quest_c24_tests)
    add_executable(hpvr_quest_c22_tests tools/quest_c22_tests.cpp)
    target_link_libraries(hpvr_quest_c22_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c22_tests COMMAND hpvr_quest_c22_tests)
    add_executable(hpvr_quest_stairs_probe tools/quest_stairs_probe.cpp)
    target_link_libraries(hpvr_quest_stairs_probe PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_stairs_tests COMMAND hpvr_quest_stairs_probe)
    add_executable(hpvr_quest_frontend_tests tests/quest_frontend_tests.cpp)
    target_link_libraries(hpvr_quest_frontend_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_frontend_tests COMMAND hpvr_quest_frontend_tests)
    add_executable(hpvr_quest_frontend_probe tools/quest_frontend_probe.cpp)
    target_link_libraries(hpvr_quest_frontend_probe PRIVATE hpvr_quest_lifecycle)
    add_executable(hpvr_quest_intro_probe tools/quest_intro_probe.cpp)
    target_link_libraries(hpvr_quest_intro_probe PRIVATE hpvr_quest_lifecycle)
    add_executable(hpvr_quest_c27_tests tools/quest_c27_tests.cpp)
    target_link_libraries(hpvr_quest_c27_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c27_tests COMMAND hpvr_quest_c27_tests)
    add_executable(hpvr_quest_lifecycle_tests
        tests/quest_lifecycle_tests.cpp
    )
    target_link_libraries(hpvr_quest_lifecycle_tests PRIVATE
        hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_lifecycle_tests
        COMMAND hpvr_quest_lifecycle_tests)

    add_executable(hpvr_quest_view_tests
        tests/quest_view_tests.cpp
    )
    target_link_libraries(hpvr_quest_view_tests PRIVATE
        hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_view_tests
        COMMAND hpvr_quest_view_tests)

    add_executable(hpvr_quest_gesture_tests
        tests/quest_gesture_tests.cpp
    )
    target_link_libraries(hpvr_quest_gesture_tests PRIVATE
        hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_gesture_tests
        COMMAND hpvr_quest_gesture_tests)

    add_executable(hpvr_quest_spell_targets_tests
        tests/quest_spell_targets_tests.cpp
    )
    target_link_libraries(hpvr_quest_spell_targets_tests PRIVATE
        hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_spell_targets_tests
        COMMAND hpvr_quest_spell_targets_tests)

    add_executable(hpvr_quest_lighting_tests
        tests/quest_lighting_tests.cpp
    )
    target_link_libraries(hpvr_quest_lighting_tests PRIVATE
        hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_lighting_tests
        COMMAND hpvr_quest_lighting_tests)

    add_executable(hpvr_quest_world_lighting_probe
        tools/quest_world_lighting_probe.cpp
    )
    target_link_libraries(hpvr_quest_world_lighting_probe PRIVATE
        hpvr_quest_lifecycle)
endif()
