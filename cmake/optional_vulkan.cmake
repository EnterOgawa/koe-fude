# Copyright (c) 2026 Shunji Ogawa. MIT License.
# 1.0.0 Shunji Ogawa 2026/11/11: ADR1410の自作C++ patch参照とlocal名を現宣言へ整合。
# 0.1.0 Shunji Ogawa 2026/11/09: ADR1367の任意loaderを固定vendor生成copyへ接続します。

# 完全preimageと期待件数が異なるvendorへpatchを流用しません。
function(koefude_vulkan_replace output input before after expected)
    string(LENGTH "${input}" input_length)
    string(LENGTH "${before}" before_length)
    if(before_length EQUAL 0)
        message(FATAL_ERROR "Empty optional Vulkan patch preimage")
    endif()
    string(REPLACE "${before}" "" removed "${input}")
    string(LENGTH "${removed}" removed_length)
    math(EXPR found "(${input_length} - ${removed_length}) / ${before_length}")
    if(NOT found EQUAL expected)
        message(FATAL_ERROR "Optional Vulkan patch count mismatch: expected ${expected}, found ${found}")
    endif()
    string(REPLACE "${before}" "${after}" changed "${input}")
    set("${output}" "${changed}" PARENT_SCOPE)
endfunction()

function(koefude_optional_vulkan target loader_target)
    set(source "${PROJECT_SOURCE_DIR}/third_party/whisper.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp")
    file(SHA256 "${source}" source_sha)
    if(NOT source_sha STREQUAL "f1043f819ed330c7f3e4d65c76918ef32fcc11830753cc4a7ac9b56a045c0e00")
        message(FATAL_ERROR "Optional Vulkan vendor source SHA256 mismatch")
    endif()
    file(READ "${source}" original)
    string(REPLACE "\r\n" "\n" original "${original}")
    set(patched "${original}")
    koefude_vulkan_replace(patched "${patched}" "#include \"ggml-vulkan.h\""
        "#include \"ggml-vulkan.h\"\n#include \"platform/vulkan_loader.h\"\n#include <stdexcept>" 1)
    # 元の全14直接参照を同じABIのdispatcher PFNへ移します。
    koefude_vulkan_replace(patched "${patched}" "vkGetInstanceProcAddr("
        "VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(" 7)
    koefude_vulkan_replace(patched "${patched}" "vkGetDeviceProcAddr("
        "VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(" 1)
    koefude_vulkan_replace(patched "${patched}" "vkGetPhysicalDeviceFeatures2("
        "VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2(" 3)
    koefude_vulkan_replace(patched "${patched}" "vkCmdCopyBuffer("
        "VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdCopyBuffer(" 2)
    # NULL GPA又はglobal entry不足をHppのassert/間接呼出しより前に拒否します。
    set(bootstrap [=[    const auto & optionalLoader = koefude::vulkanLoader();
    const auto gpa = optionalLoader.getInstanceProcAddr;
    if (!gpa || !optionalLoader.budget.destroyInstance ||
        !gpa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion") ||
        !gpa(VK_NULL_HANDLE, "vkCreateInstance") ||
        !gpa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties") ||
        !gpa(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties")) {
        throw std::runtime_error("Optional Vulkan bootstrap unavailable");
    }
    ggml_vk_default_dispatcher_instance.init(gpa);]=])
    koefude_vulkan_replace(patched "${patched}"
        "    ggml_vk_default_dispatcher_instance.init(vkGetInstanceProcAddr);" "${bootstrap}" 1)
    # instanceの部分成功はflagへ公開せず、以後の全早期return/例外で一回だけ破棄します。
    set(create_guard [=[    vk_instance.instance = vk::createInstance(instance_create_info);
    /** @brief 部分成功instanceを一回破棄し、再登録可能な未初期化状態へ戻します。 */
    struct OptionalInstanceGuard {
        PFN_vkDestroyInstance destroy; // 作成前に確保した同じmoduleの解放PFN。
        bool committed = false; // 全device情報の正常準備後だけ保持へ移す状態。
        /** @brief 未commitの早期return又は例外では所有instanceを破棄します。 */
        ~OptionalInstanceGuard() noexcept {
            // 完全成功していない初期化をglobalな部分成功として残しません。
            if (!committed) {
                // create成功後の非NULL instanceだけを一回破棄します。
                if (vk_instance.instance) {
                    destroy(static_cast<VkInstance>(vk_instance.instance), nullptr);
                }
                vk_instance.instance = vk::Instance{};
                vk_instance.device_indices.clear();
                vk_instance.device_supports_membudget.clear();
                vk_instance_initialized = false;
            }
        }
    } optionalInstanceGuard{optionalLoader.budget.destroyInstance}; // 本初期化だけの所有guard。
    // 成功codeでもNULL handleをHppへ渡しません。
    if (!vk_instance.instance) {
        throw std::runtime_error("Optional Vulkan instance unavailable");
    }
    // 同じinstanceの使用対象core PFN全11個を一度ずつ検証してからHppへ渡します。
    for (const char * name : {
            "vkDestroyInstance", "vkEnumeratePhysicalDevices", "vkGetDeviceProcAddr",
            "vkEnumerateDeviceExtensionProperties", "vkGetPhysicalDeviceProperties",
            "vkGetPhysicalDeviceProperties2", "vkGetPhysicalDeviceMemoryProperties",
            "vkGetPhysicalDeviceMemoryProperties2", "vkGetPhysicalDeviceFeatures2",
            "vkGetPhysicalDeviceQueueFamilyProperties", "vkCreateDevice"}) {
        // 欠落したtableでNULL間接呼出しをせず、guardへcleanupを戻します。
        if (!gpa(static_cast<VkInstance>(vk_instance.instance), name)) {
            throw std::runtime_error("Optional Vulkan instance entry unavailable");
        }
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_instance.instance);
    // Existing info helpers require this flag; the uncommitted guard still rolls it back.
    vk_instance_initialized = true;]=])
    koefude_vulkan_replace(patched "${patched}"
        "    vk_instance.instance = vk::createInstance(instance_create_info);\n    vk_instance_initialized = true;"
        "${create_guard}" 1)
    koefude_vulkan_replace(patched "${patched}"
        "    // See https://github.com/KhronosGroup/Vulkan-Hpp?tab=readme-ov-file#extensions--per-device-function-pointers-\n    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_instance.instance);"
        "    // Instance PFNs were validated before publishing any initialized state." 1)
    koefude_vulkan_replace(patched "${patched}"
        "        ggml_vk_print_gpu_info(i);\n    }\n}\n\nstatic void ggml_vk_init("
        "        ggml_vk_print_gpu_info(i);\n    }\n    optionalInstanceGuard.committed = true;\n}\n\nstatic void ggml_vk_init(" 1)
    set(destination "${PROJECT_BINARY_DIR}/generated/optional-vulkan/ggml-vulkan.cpp")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/optional-vulkan")
    # 同じ内容でmtimeを変えず、不必要なvendor再buildを避けます。
    set(candidate "${destination}.candidate")
    file(WRITE "${candidate}" "${patched}")
    configure_file("${candidate}" "${destination}" COPYONLY)
    get_target_property(sources ${target} SOURCES)
    list(FIND sources "ggml-vulkan.cpp" source_index)
    if(source_index LESS 0)
        message(FATAL_ERROR "Optional Vulkan source target preimage mismatch")
    endif()
    list(REMOVE_AT sources ${source_index})
    list(APPEND sources "${destination}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    # 唯一のVulkan import linkだけを外し、headers/その他linkを保持します。
    get_target_property(links ${target} LINK_LIBRARIES)
    set(removed_links "${links}")
    list(FILTER removed_links INCLUDE REGEX "^Vulkan::Vulkan$")
    list(LENGTH removed_links removed_count)
    if(NOT removed_count EQUAL 1)
        message(FATAL_ERROR "Optional Vulkan link preimage mismatch")
    endif()
    list(REMOVE_ITEM links Vulkan::Vulkan)
    set_property(TARGET ${target} PROPERTY LINK_LIBRARIES "${links}")
    # STATICのPRIVATE依存はLINK_ONLYにも残るため、推移的import libraryも厳密除去します。
    get_target_property(interface_links ${target} INTERFACE_LINK_LIBRARIES)
    set(vulkan_interface "$<LINK_ONLY:Vulkan::Vulkan>")
    set(interface_removed "${interface_links}")
    list(FILTER interface_removed INCLUDE REGEX "^\\$<LINK_ONLY:Vulkan::Vulkan>$")
    list(LENGTH interface_removed interface_removed_count)
    if(NOT interface_removed_count EQUAL 1)
        message(FATAL_ERROR "Optional Vulkan transitive link preimage mismatch")
    endif()
    list(REMOVE_ITEM interface_links "${vulkan_interface}")
    set_property(TARGET ${target} PROPERTY INTERFACE_LINK_LIBRARIES "${interface_links}")
    target_link_libraries(${target} PRIVATE ${loader_target})
    target_include_directories(${target} SYSTEM PRIVATE "${Vulkan_INCLUDE_DIRS}")
    target_compile_definitions(${target} PRIVATE VK_NO_PROTOTYPES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()
