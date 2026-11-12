//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file vulkan_loader.h
 * @brief System32の任意Vulkan loaderとprocess寿命の契約を定義します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1367の限定読込みと所有権を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <windows.h>

namespace koefude {

// module/entry/pinの失敗を自由文やpathなしで区別する状態
enum class VulkanLoaderStatus {
	/// 固定System32のmoduleを取得できない
	MISSING,
	/// 必須GPA exportがない
	MISSING_ENTRY,
	/// moduleをprocess寿命へ保持できない
	PIN_FAILED,
	/// GPAはprocess終了まで利用可能
	READY
} ;

/** @brief instance作成前に解放関数も照合する予算照会用PFNです */
struct VulkanBudgetApi {
	/// instanceの作成
	PFN_vkCreateInstance					 createInstance = nullptr ;
	/// 全終了経路の解放
	PFN_vkDestroyInstance					 destroyInstance = nullptr ;
	/// bounded物理device列挙
	PFN_vkEnumeratePhysicalDevices			 enumerateDevices = nullptr ;
	/// 必要拡張の列挙
	PFN_vkEnumerateDeviceExtensionProperties enumerateExtensions = nullptr ;
	/// 同一PCI/discrete照合
	PFN_vkGetPhysicalDeviceProperties2		 properties = nullptr ;
	/// 実heap予算の照会
	PFN_vkGetPhysicalDeviceMemoryProperties2 memory = nullptr ;
} ;

/** @brief 成功時だけprocessへpin済みの不変関数表です */
struct VulkanLoader {
	/// 初回結果をprocess内で固定
	VulkanLoaderStatus		  status = VulkanLoaderStatus::MISSING ;
	/// NULLならHpp初期化を禁止
	PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr ;
	/// 欠落entryは予算不明として扱う
	VulkanBudgetApi			  budget ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定System32 loaderを一度だけ読込み、成功moduleをprocessへpinします
 * @return process終了まで有効な不変表。失敗時のGPAはNULL
 * @note workerから呼びます。初回以外はloadせず、global dispatcherより先に解放しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] const VulkanLoader &vulkanLoader() noexcept ;

namespace detail {

/** @brief 独立した所有権試験にも使う同期OS依存表です。mutableなglobal hookはありません */
struct VulkanLibraryApi {
	/// 固定System32の取得
	decltype(&LoadLibraryExW)	  load ;
	/// 所有moduleだけのexport照会
	decltype(&GetProcAddress)	  symbol ;
	/// 住所で同じmoduleをprocess保持
	decltype(&GetModuleHandleExW) pin ;
	/// 元の参照を一回balance
	decltype(&FreeLibrary)		  close ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 渡されたOS表で一回の限定読込みを行います
 * @param[in] api 全関数非NULL、同期中だけ借用する依存表
 * @return pin成功時だけ有効なPFN。不成立の所有参照は一回解放済み
 * @note productionは固定Win32表だけを渡します。呼出し先は例外を投げません
 */
//------------------------------------------------------------------------------
[[nodiscard]] VulkanLoader loadVulkanLibrary( const VulkanLibraryApi &api) noexcept ;

} // namespace detail
} // namespace koefude
