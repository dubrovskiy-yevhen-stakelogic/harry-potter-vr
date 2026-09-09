add_library(hpvr_wand STATIC
    src/hp1_gesture.cpp
    src/hp1_gesture_c.cpp
    src/hp1_package_graph.cpp
    src/hp1_package_linker.cpp
    src/wand_trajectory.cpp
    src/wand_trajectory_c.cpp
)

target_include_directories(hpvr_wand
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(hpvr_wand PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(hpvr_wand PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(hpvr_wand PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_library(hpvr_wand_c_header_test OBJECT
        tests/hp1_gesture_c_header_test.c
        tests/wand_trajectory_c_header_test.c
    )
    target_include_directories(hpvr_wand_c_header_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    add_executable(hpvr_wand_tests
        tests/wand_trajectory_tests.cpp
    )
    target_link_libraries(hpvr_wand_tests PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_gesture_tests
        tests/hp1_gesture_tests.cpp
    )
    target_link_libraries(hpvr_hp1_gesture_tests PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_profile_probe
        tools/hp1_profile_probe.cpp
    )
    target_link_libraries(hpvr_hp1_profile_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_package_census
        tools/hp1_package_census.cpp
    )
    target_link_libraries(hpvr_hp1_package_census PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_package_graph
        tools/hp1_package_graph.cpp
    )
    target_link_libraries(hpvr_hp1_package_graph PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_package_linker
        tools/hp1_package_linker.cpp
    )
    target_link_libraries(hpvr_hp1_package_linker PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_level_probe
        tools/hp1_level_probe.cpp
    )
    target_link_libraries(hpvr_hp1_level_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_model_probe
        tools/hp1_model_probe.cpp
    )
    target_link_libraries(hpvr_hp1_model_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_skeletal_mesh_probe
        tools/hp1_skeletal_mesh_probe.cpp
    )
    target_link_libraries(hpvr_hp1_skeletal_mesh_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_bsp_probe
        tools/hp1_bsp_probe.cpp
    )
    target_link_libraries(hpvr_hp1_bsp_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_bsp_mesh_probe
        tools/hp1_bsp_mesh_probe.cpp
    )
    target_link_libraries(hpvr_hp1_bsp_mesh_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_bsp_material_probe
        tools/hp1_bsp_material_probe.cpp
    )
    target_link_libraries(hpvr_hp1_bsp_material_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_texture_probe
        tools/hp1_texture_probe.cpp
    )
    target_link_libraries(hpvr_hp1_texture_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_textured_bsp_probe
        tools/hp1_textured_bsp_probe.cpp
    )
    target_link_libraries(hpvr_hp1_textured_bsp_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_actor_census
        tools/hp1_actor_census.cpp
    )
    target_link_libraries(hpvr_hp1_actor_census PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_actor_visual_probe
        tools/hp1_actor_visual_probe.cpp
    )
    target_link_libraries(hpvr_hp1_actor_visual_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_class_visual_probe
        tools/hp1_class_visual_probe.cpp
    )
    target_link_libraries(hpvr_hp1_class_visual_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_character_manifest_probe
        tools/hp1_character_manifest_probe.cpp
    )
    target_link_libraries(hpvr_hp1_character_manifest_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_animation_probe
        tools/hp1_animation_probe.cpp
    )
    target_link_libraries(hpvr_hp1_animation_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_sound_probe
        tools/hp1_sound_probe.cpp
    )
    target_link_libraries(hpvr_hp1_sound_probe PRIVATE hpvr_wand)

    add_executable(hpvr_hp1_world_probe
        tools/hp1_world_probe.cpp
    )
    target_link_libraries(hpvr_hp1_world_probe PRIVATE hpvr_wand)
    add_executable(hpvr_hp1_content_probe tools/hp1_content_probe.cpp)
    target_link_libraries(hpvr_hp1_content_probe PRIVATE hpvr_wand)

    if(MSVC)
        target_compile_options(hpvr_wand_c_header_test PRIVATE /W4 /WX)
        target_compile_options(hpvr_wand_tests PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_gesture_tests PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_profile_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_package_census PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_package_graph PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_package_linker PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_level_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_model_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_skeletal_mesh_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_bsp_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_bsp_mesh_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_bsp_material_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_texture_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_textured_bsp_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_actor_census PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_actor_visual_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_class_visual_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_character_manifest_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_animation_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_sound_probe PRIVATE /W4 /WX /permissive-)
        target_compile_options(hpvr_hp1_world_probe PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(hpvr_wand_c_header_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_wand_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_gesture_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_profile_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_package_census PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_package_graph PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_package_linker PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_level_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_model_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_skeletal_mesh_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_bsp_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_bsp_mesh_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_bsp_material_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_texture_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_textured_bsp_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_actor_census PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_actor_visual_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_class_visual_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_character_manifest_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_animation_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_sound_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(hpvr_hp1_world_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME hpvr_wand_tests COMMAND hpvr_wand_tests)
    add_test(NAME hpvr_hp1_gesture_tests COMMAND hpvr_hp1_gesture_tests)
endif()
