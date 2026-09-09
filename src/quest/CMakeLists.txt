add_library(hpvr_quest_lifecycle STATIC
    src/quest_gesture.cpp
    src/quest_frontend.cpp
    src/quest_lighting.cpp
    src/quest_lifecycle.cpp
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
    add_executable(hpvr_quest_c37_defaults_tests tools/quest_c37_defaults_tests.cpp)
    target_link_libraries(hpvr_quest_c37_defaults_tests PRIVATE hpvr_quest_lifecycle)
    add_test(NAME hpvr_quest_c37_defaults_tests COMMAND hpvr_quest_c37_defaults_tests)
    add_executable(hpvr_quest_c36_frontend_tests tools/quest_c36_frontend_tests.cpp)
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
