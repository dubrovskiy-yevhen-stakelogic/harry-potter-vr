# HPVR integration for the pinned, CPU-only sherpa-onnx C API.
include_guard(GLOBAL)

function(hpvr_add_voice_decoder hpvr_root)
    if(TARGET hpvr_voice_decoder)
        return()
    endif()
    set(hpvr_voice_root "${hpvr_root}/local/neural-voice-dependencies")
    if(ANDROID)
        set(HPVR_SHERPA_ANDROID_ROOT "${hpvr_voice_root}/android-arm64" CACHE PATH
            "CPU-only sherpa-onnx static runtime prepared by BUILD-NEURAL-VOICE-RUNTIME.ps1")
        if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
            message(FATAL_ERROR "HPVR neural voice runtime supports Android arm64-v8a only")
        endif()
        set(hpvr_sherpa_include "${HPVR_SHERPA_ANDROID_ROOT}/include")
        set(hpvr_archive_names
            sherpa-onnx-c-api sherpa-onnx-core kaldi-decoder-core
            sherpa-onnx-kaldifst-core sherpa-onnx-fstfar sherpa-onnx-fst
            kaldi-native-fbank-core kissfft-float onnxruntime ssentencepiece_core)
        set(hpvr_sherpa_archives)
        foreach(name IN LISTS hpvr_archive_names)
            set(archive "${HPVR_SHERPA_ANDROID_ROOT}/lib/lib${name}.a")
            if(NOT EXISTS "${archive}")
                message(FATAL_ERROR "Missing ${archive}; run tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1")
            endif()
            list(APPEND hpvr_sherpa_archives "${archive}")
        endforeach()
        set(hpvr_hidden_archives)
        foreach(name IN LISTS hpvr_archive_names)
            list(APPEND hpvr_hidden_archives "lib${name}.a")
        endforeach()
        list(JOIN hpvr_hidden_archives ":" hpvr_hidden_archive_option)
        add_library(hpvr_sherpa INTERFACE IMPORTED GLOBAL)
        set_target_properties(hpvr_sherpa PROPERTIES
            INTERFACE_LINK_LIBRARIES "-Wl,--start-group;${hpvr_sherpa_archives};-Wl,--end-group;android;log;m;dl"
            INTERFACE_LINK_OPTIONS "-Wl,--exclude-libs,${hpvr_hidden_archive_option};-Wl,--gc-sections")
    elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(HPVR_SHERPA_HOST_ROOT
            "${hpvr_voice_root}/host/sherpa-onnx-v1.13.7-win-x64-shared-MD-Release-no-tts"
            CACHE PATH "Pinned official sherpa-onnx Windows x64 no-TTS runtime")
        set(hpvr_sherpa_include "${HPVR_SHERPA_HOST_ROOT}/include")
        foreach(file IN ITEMS sherpa-onnx-c-api.lib sherpa-onnx-c-api.dll onnxruntime.dll)
            if(NOT EXISTS "${HPVR_SHERPA_HOST_ROOT}/lib/${file}")
                message(FATAL_ERROR "Missing ${file}; run tools/voice/FETCH-VOICE-DEPENDENCIES.ps1")
            endif()
        endforeach()
        add_library(hpvr_sherpa SHARED IMPORTED GLOBAL)
        set_target_properties(hpvr_sherpa PROPERTIES
            IMPORTED_LOCATION "${HPVR_SHERPA_HOST_ROOT}/lib/sherpa-onnx-c-api.dll"
            IMPORTED_IMPLIB "${HPVR_SHERPA_HOST_ROOT}/lib/sherpa-onnx-c-api.lib")
        set(hpvr_dlls "${HPVR_SHERPA_HOST_ROOT}/lib/sherpa-onnx-c-api.dll"
                      "${HPVR_SHERPA_HOST_ROOT}/lib/onnxruntime.dll")
        if(EXISTS "${HPVR_SHERPA_HOST_ROOT}/lib/onnxruntime_providers_shared.dll")
            list(APPEND hpvr_dlls "${HPVR_SHERPA_HOST_ROOT}/lib/onnxruntime_providers_shared.dll")
        endif()
        set_property(GLOBAL PROPERTY HPVR_VOICE_HOST_DLLS "${hpvr_dlls}")
    else()
        message(FATAL_ERROR "Neural voice checks currently support Windows x64 and Android ARM64")
    endif()
    if(NOT EXISTS "${hpvr_sherpa_include}/sherpa-onnx/c-api/c-api.h")
        message(FATAL_ERROR "Pinned sherpa-onnx C API header was not found")
    endif()
    set_target_properties(hpvr_sherpa PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${hpvr_sherpa_include}")
    add_library(hpvr_voice_decoder STATIC "${hpvr_root}/src/quest/src/quest_voice_decoder.cpp")
    target_include_directories(hpvr_voice_decoder PUBLIC "${hpvr_root}/src/quest/include")
    target_compile_features(hpvr_voice_decoder PUBLIC cxx_std_20)
    target_link_libraries(hpvr_voice_decoder PUBLIC hpvr_sherpa)
    set_target_properties(hpvr_voice_decoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# Put the pinned ORT beside each executable: Windows' System32 ORT must not
# silently shadow the version used by the pinned sherpa binary.
function(hpvr_copy_voice_runtime target)
    if(WIN32)
        get_property(hpvr_dlls GLOBAL PROPERTY HPVR_VOICE_HOST_DLLS)
        if(NOT hpvr_dlls)
            message(FATAL_ERROR "Call hpvr_add_voice_decoder before hpvr_copy_voice_runtime")
        endif()
        foreach(dll IN LISTS hpvr_dlls)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endforeach()
    endif()
endfunction()

function(hpvr_add_android_voice target hpvr_root)
    option(HPVR_VOICE_DIAGNOSTICS "Local one-shot consented microphone capture; never for release" OFF)
    if(HPVR_VOICE_DIAGNOSTICS)
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT ANDROID)
            message(FATAL_ERROR "Voice recording diagnostics require an Android non-release build")
        endif()
        target_compile_definitions(${target} PRIVATE HPVR_VOICE_DIAGNOSTICS=1)
    endif()
    hpvr_add_voice_decoder("${hpvr_root}")
    target_sources(${target} PRIVATE "${hpvr_root}/android/app/src/main/cpp/quest_voice_cast_android.cpp")
    target_link_libraries(${target} PRIVATE hpvr_voice_decoder aaudio)
endfunction()
