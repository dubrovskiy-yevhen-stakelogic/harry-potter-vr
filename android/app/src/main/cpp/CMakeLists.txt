cmake_minimum_required(VERSION 3.22.1)
project(hpvr_quest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(HPVR_OPENXR_INCLUDE_DIR AND HPVR_OPENXR_LOADER_LIBRARY)
    add_library(OpenXR::openxr_loader SHARED IMPORTED)
    set_target_properties(OpenXR::openxr_loader PROPERTIES
        IMPORTED_LOCATION "${HPVR_OPENXR_LOADER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HPVR_OPENXR_INCLUDE_DIR}")
else()
    find_package(OpenXR REQUIRED CONFIG)
endif()

get_filename_component(HPVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

set(HPVR_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(HPVR_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(HPVR_GLSLC "${ANDROID_NDK}/shader-tools/windows-x86_64/glslc.exe")
if(NOT EXISTS "${HPVR_GLSLC}")
    message(FATAL_ERROR "Pinned NDK glslc was not found: ${HPVR_GLSLC}")
endif()

function(hpvr_embed_shader source_file output_name symbol_name shader_stage)
    set(spv_file "${HPVR_SHADER_GENERATED_DIR}/${output_name}.spv")
    set(header_file "${HPVR_SHADER_GENERATED_DIR}/${output_name}.spv.h")
    add_custom_command(
        OUTPUT "${header_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HPVR_SHADER_GENERATED_DIR}"
        COMMAND "${HPVR_GLSLC}" -O -fshader-stage=${shader_stage}
                "${HPVR_SHADER_SOURCE_DIR}/${source_file}" -o "${spv_file}"
        COMMAND "${CMAKE_COMMAND}"
                "-DINPUT=${spv_file}"
                "-DOUTPUT=${header_file}"
                "-DSYMBOL=${symbol_name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        DEPENDS
            "${HPVR_SHADER_SOURCE_DIR}/${source_file}"
            "${HPVR_SHADER_SOURCE_DIR}/reflection_hit_policy.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        VERBATIM)
    set(HPVR_GENERATED_SHADER_HEADERS
        ${HPVR_GENERATED_SHADER_HEADERS} "${header_file}" PARENT_SCOPE)
endfunction()

hpvr_embed_shader(hogwarts.vert hogwarts_vert kHogwartsVertexSpirv vertex)
hpvr_embed_shader(hogwarts.frag hogwarts_frag kHogwartsFragmentSpirv fragment)
hpvr_embed_shader(wand.vert wand_vert kWandVertexSpirv vertex)
hpvr_embed_shader(wand.frag wand_frag kWandFragmentSpirv fragment)
hpvr_embed_shader(particle.vert particle_vert kParticleVertexSpirv vertex)
hpvr_embed_shader(particle.frag particle_frag kParticleFragmentSpirv fragment)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${HPVR_ROOT}/src/wand" "${CMAKE_CURRENT_BINARY_DIR}/hpvr_wand")
set_target_properties(hpvr_wand PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue")
target_compile_options(native_app_glue PRIVATE -Wall -Wextra -Werror
    -Wno-unused-parameter)

add_library(hpvr_quest_lifecycle STATIC
    "${HPVR_ROOT}/src/quest/src/quest_gesture.cpp"
    "${HPVR_ROOT}/src/quest/src/quest_frontend.cpp"
    "${HPVR_ROOT}/src/quest/src/quest_lighting.cpp"
    "${HPVR_ROOT}/src/quest/src/quest_lifecycle.cpp"
    "${HPVR_ROOT}/src/quest/src/quest_spell_targets.cpp"
    "${HPVR_ROOT}/src/quest/src/quest_view.cpp")
target_include_directories(hpvr_quest_lifecycle PUBLIC
    "${HPVR_ROOT}/src/quest/include")
target_compile_options(hpvr_quest_lifecycle PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(hpvr_quest_lifecycle PUBLIC hpvr_wand)

add_library(hpvr_quest SHARED
    android_main.cpp
    quest_audio.cpp
    quest_reflections.cpp
    quest_scene.cpp
    ${HPVR_GENERATED_SHADER_HEADERS}
    xr_vulkan_smoke.cpp)
target_compile_definitions(hpvr_quest PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_VULKAN
    VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(hpvr_quest PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
target_include_directories(hpvr_quest PRIVATE "${HPVR_SHADER_GENERATED_DIR}")
target_link_libraries(hpvr_quest PRIVATE
    hpvr_wand
    hpvr_quest_lifecycle
    native_app_glue
    OpenXR::openxr_loader
    vulkan
    aaudio
    mediandk
    android
    log)

target_link_options(hpvr_quest PRIVATE "-u" "ANativeActivity_onCreate")
