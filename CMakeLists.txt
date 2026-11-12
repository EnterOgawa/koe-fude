cmake_minimum_required(VERSION 3.25)
project(KoeFude VERSION 1.0.0 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KoeFude requires Windows, MSVC, and an x64 toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 日本語のみのMSVCとUTF-8環境の組合せで、CMakeのprefix検出が文字化けする場合を補正する。
if(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX MATCHES "繝")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "メモ: インクルード ファイル: ")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/models.lock.json")
include(cmake/model_registry.cmake)
koefude_generate_model_registry("${CMAKE_CURRENT_SOURCE_DIR}/models.lock.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/model_manifest.h")
configure_file(cmake/version.h.in generated/version.h @ONLY)

set_property(SOURCE src/app/koefude.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/koefude.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/add.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/save.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/copy.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/cancel.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/clear.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/icons/rename.ico")

option(KOEFUDE_VULKAN "Build the Vulkan transcription backend" ON)
option(KOEFUDE_CUDA "Use the fixed CUDA runtime for Parakeet transcription" ON)
# 固定したwhisper.cppを静的に組み込み、network機能やsample appを配布しない。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ${KOEFUDE_VULKAN} CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
# 共用PATHのccacheが日本語MSVCのRC依存情報を解釈できないため、標準compilerを直接使う。
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(GGML_AVX OFF CACHE BOOL "" FORCE)
set(GGML_AVX2 OFF CACHE BOOL "" FORCE)
set(GGML_FMA OFF CACHE BOOL "" FORCE)
set(GGML_F16C OFF CACHE BOOL "" FORCE)
if(KOEFUDE_VULKAN)
    include(ExternalProject)
    get_property(koefude_saved_ep_base DIRECTORY PROPERTY EP_BASE)
    get_property(koefude_saved_ep_base_set DIRECTORY PROPERTY EP_BASE SET)
    set_property(DIRECTORY PROPERTY EP_BASE "${CMAKE_BINARY_DIR}/e")
endif()
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL SYSTEM)
if(KOEFUDE_VULKAN)
    # 既存の上位property状態へ戻し、確定済み子targetの配置だけを保持します。
    if(koefude_saved_ep_base_set)
        set_property(DIRECTORY PROPERTY EP_BASE "${koefude_saved_ep_base}")
    else()
        set_property(DIRECTORY PROPERTY EP_BASE)
    endif()
    unset(koefude_saved_ep_base)
    unset(koefude_saved_ep_base_set)
endif()

# 同じexeへ静的linkする第三者objectにもCFGの間接呼出し先情報を持たせる。
# 警告の厳格化とは分け、自作targetのCFGを無効化して初期化失敗を回避しない。
foreach(inference_target whisper ggml ggml-base ggml-cpu ggml-vulkan)
    if(TARGET ${inference_target} AND MSVC)
        target_compile_options(${inference_target} PRIVATE /guard:cf /experimental:deterministic
            "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.")
    endif()
endforeach()

set(KOEFUDE_SHERPA_CPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx-v1.13.7-win-x64-shared-MT-Release-no-tts")
include(cmake/cuda_runtime.cmake)
add_library(koefude_sherpa SHARED IMPORTED)
set_target_properties(koefude_sherpa PROPERTIES
    IMPORTED_IMPLIB "${KOEFUDE_SHERPA_DIR}/lib/sherpa-onnx-c-api.lib"
    IMPORTED_LOCATION "${KOEFUDE_SHERPA_DIR}/lib/sherpa-onnx-c-api.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${KOEFUDE_SHERPA_DIR}/include")

# 自作targetだけに厳格な警告設定を適用し、第三者sourceの方針と分離する。
function(koefude_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /permissive- /Zc:__cplusplus /utf-8 /EHsc /guard:cf>
        /experimental:deterministic "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.")
    target_link_options(${target} PRIVATE "/PDBALTPATH:%_PDB%")
    target_link_options(${target} PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf)
endfunction()

if(KOEFUDE_VULKAN)
    find_package(Vulkan REQUIRED)
    add_library(koefude_vulkan_loader STATIC src/platform/vulkan_loader.cpp)
    target_include_directories(koefude_vulkan_loader PUBLIC src)
    target_include_directories(koefude_vulkan_loader SYSTEM PUBLIC "${Vulkan_INCLUDE_DIRS}")
    target_compile_definitions(koefude_vulkan_loader PUBLIC VK_NO_PROTOTYPES)
    koefude_target(koefude_vulkan_loader)
    include(cmake/optional_vulkan.cmake)
    koefude_optional_vulkan(ggml-vulkan koefude_vulkan_loader)
endif()
include(cmake/ctc_features.cmake)
# 同じEXEへ静的linkする特徴抽出にも開発PCの絶対pathを残しません。
foreach(feature_target koefude_ctc_fft koefude_ctc_fbank)
    target_compile_options(${feature_target} PRIVATE /experimental:deterministic
        "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.")
endforeach()
add_library(koefude_core STATIC
    src/platform/local_diagnostics.cpp
    src/core/local_diagnostic_hooks.cpp
    src/core/app_update.cpp
    src/platform/app_update_native.cpp
    src/core/ctc_decoder.cpp
    src/core/ctc_terms.cpp
    src/core/ctc_word_spotter.cpp
    src/core/ctc_rescore.cpp
    src/core/ctc_text_alignment.cpp
    src/core/cross_asr_selection.cpp
    src/core/asr_tokens.cpp
    src/core/asr_windows.cpp
    src/core/shared_asr_windows.cpp
    src/core/asr_context_windows.cpp
    src/core/audio_data.cpp
    src/core/audio_identity.cpp
    src/platform/temporary_store.cpp
    src/core/api_model_catalog.cpp
    src/platform/api_catalog_http.cpp
    src/platform/api_catalog_store.cpp
    src/core/api_text_correction.cpp
    src/core/api_text_speaker_turns.cpp
    src/core/api_text_provenance.cpp
    src/inference/api_text_corrector.cpp
    src/platform/api_key_store.cpp
    src/platform/api_text_http.cpp
    src/platform/api_audio_http.cpp
    src/core/api_audio_transcription.cpp
    src/inference/api_audio_transcriber.cpp
    src/inference/api_audio_pipeline.cpp
    src/platform/api_text_store.cpp
    src/core/history.cpp
    src/core/history_controller.cpp
    src/core/transcription_settings.cpp
    src/core/correction_diagnostics.cpp
    src/core/job_controller.cpp
    src/core/pipeline.cpp
    src/core/reading_view.cpp
    src/core/reading_session.cpp
    src/core/whole_text_persistence.cpp
    src/platform/whole_text_persistence_store.cpp
    src/core/whole_text_generation_contract.cpp
    src/core/whole_text_bounded_result.cpp
    src/platform/whole_text_journal_store.cpp
    src/inference/whole_text_bounded_corrector.cpp
    src/core/whole_text_plan.cpp
    native/whole_text/generator.cpp
    src/inference/whole_text_native_session.cpp
    src/inference/whole_text_bounded_preparation.cpp
    src/core/text_context_protection.cpp
    src/core/whole_text_correction.cpp
    src/core/whole_text_export.cpp
    src/inference/whole_text_model_import.cpp
    src/inference/whole_text_model_download.cpp
    src/inference/whole_text_preparation.cpp
    src/inference/whole_text_preparation_controller.cpp
    src/inference/whole_text_runtime.cpp
    native/whole_text/cpu_candidate_contract.cpp
    src/inference/whole_text_application.cpp
    src/inference/whole_text_application_resources.cpp
    src/inference/whole_text_resource_guard.cpp
    src/inference/primary_pipeline_application.cpp
    src/inference/whole_text_native_corrector.cpp
    src/inference/whole_text_native_preparation.cpp
    src/core/reading_export.cpp
    src/core/speaker_attribution.cpp
    src/core/speaker_text_projection.cpp
    src/core/speaker_text_record.cpp
    src/platform/speaker_text_store.cpp
    src/inference/text_alignment_reference.cpp
    src/inference/speaker_text_application.cpp
    src/core/transcript_export.cpp
    src/core/text_spacing.cpp
    src/platform/audio_decoder.cpp
    src/platform/aac_packet_bounds.cpp
    src/platform/wave_format.cpp
    src/platform/wave_decode_checks.cpp
    src/platform/mp3_format.cpp
    src/platform/mp3_decode_checks.cpp
    src/platform/flac_format.cpp
    src/platform/flac_decode_checks.cpp
    src/platform/alac_format.cpp
    src/platform/alac_decode_checks.cpp
    src/platform/heaac_format.cpp
    src/platform/heaac_decode_checks.cpp
    src/platform/m4a_reference_gate.cpp
    src/platform/history_store.cpp
    src/platform/win_helpers.cpp
    src/inference/model_store.cpp
    src/inference/model_preparation.cpp
    src/inference/model_download.cpp
    src/inference/model_preparation_controller.cpp
    src/platform/model_download_io.cpp
    src/platform/model_http.cpp
    src/platform/model_archive.cpp
    src/inference/gpu_budget.cpp
    src/inference/cuda_device.cpp
    src/inference/asr_retry.cpp
    src/inference/whisper_engine.cpp
    src/inference/whisper_cpu_runtime.cpp
    src/inference/parakeet_engine.cpp
    src/inference/parakeet_ctc.cpp
    src/platform/word_boundaries.cpp
    src/inference/spectral_clustering.cpp
    src/inference/speaker_stream.cpp
    src/inference/spectral_diarizer.cpp
    src/inference/sherpa_diarizer.cpp)
target_include_directories(koefude_core PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(koefude_core SYSTEM PRIVATE third_party/eigen third_party/onnxruntime-headers third_party/whisper.cpp/examples)
# GPU archiveにimport libraryがないため、同じ固定API版のCPU import libraryを使う。
target_link_libraries(koefude_core PRIVATE "${KOEFUDE_SHERPA_CPU_DIR}/lib/onnxruntime.lib" koefude_ctc_fbank)
target_link_libraries(koefude_core PUBLIC mfplat mfreadwrite mfuuid ole32 propsys whisper koefude_sherpa bcrypt shell32 advapi32 runtimeobject winhttp uxtheme wintrust crypt32 version)
target_compile_definitions(koefude_core PUBLIC KOEFUDE_HAS_VULKAN=$<BOOL:${KOEFUDE_VULKAN}>
    KOEFUDE_HAS_CUDA=$<BOOL:${KOEFUDE_CUDA}>)
koefude_target(koefude_core)
if(KOEFUDE_VULKAN)
    target_link_libraries(koefude_core PRIVATE koefude_vulkan_loader)
    target_include_directories(koefude_core SYSTEM PRIVATE "${Vulkan_INCLUDE_DIRS}")
    target_compile_definitions(koefude_core PRIVATE VK_NO_PROTOTYPES)
endif()
if(KOEFUDE_CUDA)
    target_include_directories(koefude_core SYSTEM PRIVATE third_party/nvidia-cuda_cudart/include)
    target_link_libraries(koefude_core PRIVATE dxgi)
endif()
add_executable(koefude WIN32 src/app/main_window.cpp src/app/diagnostic_log.cpp src/app/control_accessibility.cpp src/app/model_preparation_window.cpp src/app/whole_text_preparation_window.cpp src/app/api_text_correction_window.cpp src/app/chat_document.cpp src/app/chat_document_controller.cpp src/app/status_progress.cpp src/app/smooth_scroll.cpp src/app/button_icons.cpp src/app/message_dialog.cpp src/app/localization.cpp src/app/koefude.rc)
set_target_properties(koefude PROPERTIES OUTPUT_NAME "KoeFude")
target_link_libraries(koefude PRIVATE koefude_core comctl32 comdlg32 shell32 advapi32 oleacc uuid)
target_include_directories(koefude PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_options(koefude PRIVATE /MANIFEST:NO)
koefude_target(koefude)
foreach(runtime IN LISTS KOEFUDE_RUNTIME_FILES)
    add_custom_command(TARGET koefude POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${runtime}" "$<TARGET_FILE_DIR:koefude>")
endforeach()

set(KOEFUDE_WHOLE_TEXT_RUNTIME "" CACHE FILEPATH "Explicit portable whole text DLL")
set(KOEFUDE_WHOLE_TEXT_MINISTRAL_16GB_RUNTIME "" CACHE FILEPATH "Explicit Ministral16GB AVX2 no-repack DLL")
set(KOEFUDE_WHOLE_TEXT_AVX2_NOREPACK_RUNTIME "" CACHE FILEPATH "Explicit AVX2 no-repack DLL")
set(KOEFUDE_WHOLE_TEXT_LLAMA_LICENSE "" CACHE FILEPATH "Explicit pinned llama.cpp LICENSE")
option(KOEFUDE_WHOLE_TEXT_ACCOUNTING_V2 "Require current V2 correction runtime receipts" OFF)
set(WT_ACCOUNTING_V2 "${KOEFUDE_WHOLE_TEXT_ACCOUNTING_V2}")
set(KOEFUDE_WT_ACCOUNTING_ARGS)
if(KOEFUDE_WHOLE_TEXT_ACCOUNTING_V2)
    set(WT_CAPABILITIES_MASK 3)
    set(WT_REQUIRED_CAPABILITIES_MASK 3)
    set(KOEFUDE_WT_ACCOUNTING_ARGS -DWT_ACCOUNTING_V2=ON
        -DWT_CAPABILITIES_MASK=3 -DWT_REQUIRED_CAPABILITIES_MASK=3)
endif()
include(cmake/whole_text_runtime.cmake)
foreach(slot PORTABLE AVX2 MINISTRAL)
    set(WT_${slot}_AVAILABLE 0)
    set(WT_${slot}_BYTES 0)
    set(WT_${slot}_SHA "")
    set(WT_${slot}_RECEIPT_SHA "")
    set(WT_${slot}_ACCOUNTING_V2 0)
    set(WT_${slot}_CAPABILITIES_MASK 0)
    if(slot STREQUAL "PORTABLE")
        set(runtime "${KOEFUDE_WHOLE_TEXT_RUNTIME}")
        set(variant portable)
    elseif(slot STREQUAL "AVX2")
        set(runtime "${KOEFUDE_WHOLE_TEXT_AVX2_NOREPACK_RUNTIME}")
        set(variant avx2-norepack)
    else()
        set(runtime "${KOEFUDE_WHOLE_TEXT_MINISTRAL_16GB_RUNTIME}")
        set(variant ministral14b-avx2-norepack)
        if(runtime AND NOT KOEFUDE_WHOLE_TEXT_ACCOUNTING_V2)
            message(FATAL_ERROR "Ministral16GB requires V2 compiled pin")
        endif()
    endif()
    if(runtime)
        if(NOT KOEFUDE_WHOLE_TEXT_LLAMA_LICENSE)
            message(FATAL_ERROR "Whole text runtime requires the pinned license")
        endif()
        koefude_wt_validate("${runtime}" "${KOEFUDE_WHOLE_TEXT_LLAMA_LICENSE}" "${CMAKE_BUILD_TYPE}" "${variant}")
        set(WT_${slot}_AVAILABLE 1)
        set(WT_${slot}_BYTES "${WT_VALIDATED_DLL_BYTES}")
        set(WT_${slot}_SHA "${WT_VALIDATED_DLL_SHA}")
        set(WT_${slot}_RECEIPT_SHA "${WT_VALIDATED_RECEIPT_SHA}")
        # 同じslotの厳密検証に成功した現行V2構成だけを生成headerへ伝播します。
        if(KOEFUDE_WHOLE_TEXT_ACCOUNTING_V2)
            set(WT_${slot}_ACCOUNTING_V2 1)
            set(WT_${slot}_CAPABILITIES_MASK "${WT_CAPABILITIES_MASK}")
        endif()
        add_custom_command(TARGET koefude POST_BUILD COMMAND "${CMAKE_COMMAND}" -DWT_MODE=STAGE
            ${KOEFUDE_WT_ACCOUNTING_ARGS}
            "-DWT_RUNTIME=${runtime}" "-DWT_LICENSE=${KOEFUDE_WHOLE_TEXT_LLAMA_LICENSE}"
            "-DWT_VARIANT=${variant}" "-DWT_CONFIG=$<CONFIG>" "-DWT_DEST=$<TARGET_FILE_DIR:koefude>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/whole_text_runtime.cmake" VERBATIM)
    endif()
endforeach()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/src/inference/whole_text_native_abi.h" WT_ABI_HEADER_SHA)
configure_file(cmake/whole_text_runtime_manifest.h.in generated/whole_text_runtime_manifest.h @ONLY)

set(KOEFUDE_WHISPER_CPU_RUNTIME "" CACHE FILEPATH "Explicit completed auxiliary Whisper AVX2 DLL")
include(cmake/whisper_cpu_runtime.cmake)
koefude_whisper_cpu_manifest("${KOEFUDE_WHISPER_CPU_RUNTIME}" "${CMAKE_BUILD_TYPE}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/whisper_cpu_runtime_manifest.h")
if(KOEFUDE_WHISPER_CPU_RUNTIME)
    get_filename_component(whisper_cpu_runtime_dir "${KOEFUDE_WHISPER_CPU_RUNTIME}" DIRECTORY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${KOEFUDE_WHISPER_CPU_RUNTIME}"
        "${whisper_cpu_runtime_dir}/KoeFude.WhisperCpu.Avx2.runtime.json")
    foreach(target koefude)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${KOEFUDE_WHISPER_CPU_RUNTIME}"
                "${whisper_cpu_runtime_dir}/KoeFude.WhisperCpu.Avx2.runtime.json"
                "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endforeach()
endif()

