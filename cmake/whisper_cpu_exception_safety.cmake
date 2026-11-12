# Copyright (c) 2026 Shunji Ogawa. MIT License.
# 1.0.0 Shunji Ogawa 2026/11/11: ADR1410の別test copy用C++ hook参照だけを現定義へ整合。
# 0.1.0 Shunji Ogawa 2026/10/21: ADR1301の初期化4関数だけを生成copyで修理します。

# hash固定に加え完全なpreimageと置換数を検査します。
function(koefude_whisper_replace output input before after expected)
    string(LENGTH "${input}" input_length)
    string(LENGTH "${before}" before_length)
    if(before_length EQUAL 0)
        message(FATAL_ERROR "Empty Whisper patch preimage")
    endif()
    string(REPLACE "${before}" "" removed "${input}")
    string(LENGTH "${removed}" removed_length)
    math(EXPR found "(${input_length} - ${removed_length}) / ${before_length}")
    if(NOT found EQUAL expected)
        message(FATAL_ERROR "Whisper exact patch count mismatch: expected ${expected}, found ${found}")
    endif()
    string(REPLACE "${before}" "${after}" changed "${input}")
    set("${output}" "${changed}" PARENT_SCOPE)
endfunction()

# 隣接する固定関数名を境界にし、残りのsourceへ変更を漏らしません。
function(koefude_whisper_function output source begin end)
    string(FIND "${source}" "${begin}" start)
    string(FIND "${source}" "${end}" finish)
    if(start LESS 0 OR finish LESS_EQUAL start)
        message(FATAL_ERROR "Whisper function boundary mismatch")
    endif()
    math(EXPR length "${finish} - ${start}")
    string(SUBSTRING "${source}" ${start} ${length} function_source)
    set("${output}" "${function_source}" PARENT_SCOPE)
endfunction()

function(koefude_whisper_generate_safe_copy source destination)
    file(SHA256 "${source}" source_sha)
    if(NOT source_sha STREQUAL "362eb497c8b38d2af5e32f0ff0d338d79759fcab33bfe6d3a6f5437eea200b89")
        message(FATAL_ERROR "Whisper initialization source SHA256 mismatch")
    endif()
    file(READ "${source}" original)
    string(REPLACE "\r\n" "\n" original "${original}")
    set(patched "${original}")
    koefude_whisper_replace(patched "${patched}" "#include <map>"
        "#include <map>\n#include <memory>\n#include <new>" 1)

    koefude_whisper_function(state "${original}"
        "struct whisper_state * whisper_init_state(whisper_context * ctx) {"
        "int whisper_ctx_init_openvino_encoder_with_state(")
    set(state_safe "${state}")
    # batch等のraw fieldも零初期化し、初期allocation直後のcleanupを安全にします。
    koefude_whisper_replace(state_safe "${state_safe}"
        "    whisper_state * state = new whisper_state;"
        "    std::unique_ptr<whisper_state, decltype(&whisper_free_state)> state_owner(\n        new whisper_state{}, whisper_free_state);\n    whisper_state * state = state_owner.get();" 1)
    # 元の10箇所の失敗freeはownerの一回解放へ統合します。
    koefude_whisper_replace(state_safe "${state_safe}"
        "\n        whisper_free_state(state);" "" 5)
    koefude_whisper_replace(state_safe "${state_safe}"
        "\n            whisper_free_state(state);" "" 5)
    koefude_whisper_replace(state_safe "${state_safe}"
        "    return state;" "    return state_owner.release();" 1)
    koefude_whisper_replace(patched "${patched}" "${state}" "${state_safe}" 1)

    koefude_whisper_function(model "${original}"
        "struct whisper_context * whisper_init_with_params_no_state(struct whisper_model_loader * loader, struct whisper_context_params params) {"
        "struct whisper_context * whisper_init_from_file_with_params(const char * path_model, struct whisper_context_params params) {")
    set(model_safe "${model}")
    # loaderを先に所有してnew、registry又はlog例外時もcloseを一回試みます。
    set(loader_guard [=[
    /** @brief 同期loaderを全終了経路で一回だけ閉じます。 */
    struct LoaderGuard {
        whisper_model_loader * loader; // caller所有の同期借用。
        bool closed = false; // closeが例外を投げても再試行しません。
        /** @brief 通常終了ではclose例外を外側thunkへ伝えます。 */
        void close() {
            // 二重closeを抑止してから外部callbackへ入ります。
            if (!closed) {
                closed = true;
                loader->close(loader->context);
            }
        }
        /** @brief unwinding中のclose例外は既存失敗を保ち再送出しません。 */
        ~LoaderGuard() noexcept {
            try {
                close();
            } catch (...) {
                // pathや本文を含めず、cleanup失敗を診断します。
                std::fputs("Whisper loader cleanup failed\n", stderr);
            }
        }
    } loader_owner{loader};
]=])
    koefude_whisper_replace(model_safe "${model_safe}" "    ggml_time_init();"
        "${loader_guard}\n    ggml_time_init();" 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "    whisper_context * ctx = new whisper_context;"
        "    std::unique_ptr<whisper_context, decltype(&whisper_free)> context_owner(\n        new whisper_context{}, whisper_free);\n    whisper_context * ctx = context_owner.get();" 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "    // NULL-return failure path instead of letting it cross the C ABI."
        "    // NULL-return failure path. This private build forwards bad_alloc only to\n    // the module-local nonthrow thunk so the host can retain its OOM classification." 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "    } catch (const std::exception & e) {"
        "    } catch (const std::bad_alloc &) {\n        throw;\n    } catch (const std::exception & e) {" 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "\n        loader->close(loader->context);" "\n        loader_owner.close();" 1)
    koefude_whisper_replace(model_safe "${model_safe}" "        delete ctx;\n" "" 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "\n    loader->close(loader->context);" "\n    loader_owner.close();" 1)
    koefude_whisper_replace(model_safe "${model_safe}"
        "    return ctx;" "    return context_owner.release();" 1)
    koefude_whisper_replace(patched "${patched}" "${model}" "${model_safe}" 1)

    koefude_whisper_function(path "${original}"
        "struct whisper_context * whisper_init_from_file_with_params_no_state(const char * path_model, struct whisper_context_params params) {"
        "struct whisper_context * whisper_init_from_buffer_with_params_no_state(")
    set(path_safe "${path}")
    koefude_whisper_replace(path_safe "${path_safe}"
        "    auto ctx = whisper_init_with_params_no_state(&loader, params);"
        "    std::unique_ptr<whisper_context, decltype(&whisper_free)> context_owner(\n        whisper_init_with_params_no_state(&loader, params), whisper_free);\n    auto ctx = context_owner.get();" 1)
    koefude_whisper_replace(path_safe "${path_safe}"
        "    return ctx;" "    return context_owner.release();" 1)
    koefude_whisper_replace(patched "${patched}" "${path}" "${path_safe}" 1)

    koefude_whisper_function(attach "${original}"
        "struct whisper_context * whisper_init_from_file_with_params(const char * path_model, struct whisper_context_params params) {"
        "struct whisper_context * whisper_init_from_buffer_with_params(")
    set(attach_safe "${attach}")
    koefude_whisper_replace(attach_safe "${attach_safe}"
        "    whisper_context * ctx = whisper_init_from_file_with_params_no_state(path_model, params);"
        "    std::unique_ptr<whisper_context, decltype(&whisper_free)> context_owner(\n        whisper_init_from_file_with_params_no_state(path_model, params), whisper_free);\n    whisper_context * ctx = context_owner.get();" 1)
    koefude_whisper_replace(attach_safe "${attach_safe}" "        whisper_free(ctx);\n" "" 1)
    koefude_whisper_replace(attach_safe "${attach_safe}"
        "    return ctx;" "    return context_owner.release();" 1)
    koefude_whisper_replace(patched "${patched}" "${attach}" "${attach_safe}" 1)
    file(WRITE "${destination}" "${patched}")
    file(SHA256 "${destination}" generated_sha)
    file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" patch_sha)
    execute_process(COMMAND "${GIT_EXECUTABLE}" --no-pager diff --no-index --no-ext-diff
        -- "${source}" "${destination}" RESULT_VARIABLE diff_result OUTPUT_VARIABLE diff ENCODING UTF-8)
    if(NOT diff_result EQUAL 1)
        message(FATAL_ERROR "Cannot record generated Whisper ownership diff")
    endif()
    # build directoryを除いた同じ論理diffをDebug/Releaseで記録します。
    string(REPLACE "a/${source}" "a/third_party/whisper.cpp/src/whisper.cpp" diff "${diff}")
    string(REPLACE "b/${destination}" "b/generated/whisper.cpp" diff "${diff}")
    file(WRITE "${destination}.diff" "${diff}")
    file(SHA256 "${destination}.diff" diff_sha)
    set(WC_GENERATED_SHA256 "${generated_sha}" PARENT_SCOPE)
    set(WC_PATCH_SHA256 "${patch_sha}" PARENT_SCOPE)
    set(WC_PATCH_DIFF_SHA256 "${diff_sha}" PARENT_SCOPE)

    # 以下は別test target用copyだけです。production copyへhookを混ぜません。
    set(test_source "${patched}")
    koefude_whisper_replace(test_source "${test_source}" "#include <new>"
        "#include <new>\nextern void koefudeWhisperCpuTestPoint(int point);\nextern bool koefudeWhisperCpuTestModel();\nextern bool koefudeWhisperCpuTestState();\nextern whisper_model_loader koefudeWhisperCpuTestLoader();" 1)
    # 空stateで初期owner、state付加、借用結果を検査します。
    set(state_hook [=[
    koefudeWhisperCpuTestPoint(1);
    if (!koefudeWhisperCpuTestState()) {
        return nullptr;
    }
    whisper_segment test_segment{};
    test_segment.t0 = 11;
    test_segment.t1 = 29;
    test_segment.text = "synthetic";
    whisper_token_data test_token{};
    test_token.id = 7;
    test_token.t0 = 12;
    test_token.t1 = 28;
    test_segment.tokens.push_back(test_token);
    state->result_all.push_back(test_segment);
    return state_owner.release();
]=])
    koefude_whisper_replace(test_source "${test_source}"
        "    whisper_state * state = state_owner.get();"
        "    whisper_state * state = state_owner.get();\n${state_hook}" 1)
    koefude_whisper_replace(test_source "${test_source}"
        "    ggml_time_init();\n\n    if (params.flash_attn"
        "    koefudeWhisperCpuTestPoint(2);\n    ggml_time_init();\n\n    if (params.flash_attn" 1)
    koefude_whisper_replace(test_source "${test_source}"
        "        model_loaded = whisper_model_load(loader, *ctx);"
        "        koefudeWhisperCpuTestPoint(3);\n        ctx->model.ctxs.push_back(ggml_init({1024, nullptr, true}));\n        ctx->vocab.id_to_token[7] = \"piece\";\n        koefudeWhisperCpuTestPoint(8);\n        model_loaded = koefudeWhisperCpuTestModel();" 1)
    # file I/O前半だけを合成loaderへ差替え、同じpath owner以降を通します。
    string(FIND "${path_safe}" "    std::unique_ptr<whisper_context" path_owner_start)
    string(FIND "${path_safe}" "    WHISPER_LOG_INFO" path_io_start)
    math(EXPR path_io_length "${path_owner_start} - ${path_io_start}")
    string(SUBSTRING "${path_safe}" ${path_io_start} ${path_io_length} file_io)
    koefude_whisper_replace(test_source "${test_source}" "${file_io}"
        "    auto loader = koefudeWhisperCpuTestLoader();\n\n" 1)
    koefude_whisper_replace(test_source "${test_source}" "        ctx->path_model = path_model;"
        "        koefudeWhisperCpuTestPoint(4);\n        ctx->path_model = path_model;\n        koefudeWhisperCpuTestPoint(5);" 1)
    # file版のstate付加だけを限定置換し、他のinit関数は変更しません。
    set(attach_test "${attach_safe}")
    koefude_whisper_replace(attach_test "${attach_test}"
        "    ctx->state = whisper_init_state(ctx);"
        "    koefudeWhisperCpuTestPoint(6);\n    ctx->state = whisper_init_state(ctx);\n    koefudeWhisperCpuTestPoint(7);" 1)
    koefude_whisper_replace(test_source "${test_source}" "${attach_safe}" "${attach_test}" 1)
    koefude_whisper_replace(test_source "${test_source}"
        "void whisper_free_state(struct whisper_state * state) {\n    if (state) {"
        "void whisper_free_state(struct whisper_state * state) {\n    if (state) {\n        koefudeWhisperCpuTestPoint(20);" 1)
    koefude_whisper_replace(test_source "${test_source}"
        "void whisper_free(struct whisper_context * ctx) {\n    if (ctx) {"
        "void whisper_free(struct whisper_context * ctx) {\n    if (ctx) {\n        koefudeWhisperCpuTestPoint(21);" 1)
    file(WRITE "${destination}.test.cpp" "${test_source}")
endfunction()
