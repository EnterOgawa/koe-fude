//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file vulkan_loader.cpp
 * @brief Vulkanを固定System32から任意読込みし、成功moduleをprocessへpinします
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 起動importを伴わない不変loaderを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note OS依存表はCFG情報付き自作wrapperを参照します。import関数住所の直接格納は
 * 本環境の間接呼出しで0xC0000409となったため、Win32 APIをwrapper内で直接呼びます
 */
//------------------------------------------------------------------------------
#include "platform/vulkan_loader.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 指定された引数を変更せずLoadLibraryExWへ転送します
 * @param[in] name NUL終端UTF-16 DLL名のborrowです。実callerは固定vulkan-1.dllを渡します
 * @param[in] file Win32の予約handle引数です。実callerはNULLを渡します
 * @param[in] flags Win32 load flagです。実callerはLOAD_LIBRARY_SEARCH_SYSTEM32を渡します
 * @return 成功時は初期参照を所有するHMODULE、失敗時はNULLです。解放主体はcallerで、元GetLastErrorを変換しません
 * @note wrapper自身はDLL名やflagを検査しません
 */
//------------------------------------------------------------------------------
HMODULE WINAPI loadLibrary( LPCWSTR name, HANDLE file, DWORD flags)
{
	return LoadLibraryExW( name, file, flags) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有moduleのexportを直接照会します
 * @param[in] module 所有module
 * @param[in] name ASCII固定名
 * @return export住所又はNULL
 */
//------------------------------------------------------------------------------
FARPROC WINAPI librarySymbol( HMODULE module, LPCSTR name)
{
	return GetProcAddress( module, name) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定されたflagと住所をGetModuleHandleExWへ転送します
 * @param[in] flags 参照取得方法を指定するWin32 flagです。実callerはFROM_ADDRESSとPINを渡します
 * @param[in] address FROM_ADDRESS時に文字列として読まないmodule内code住所のborrowです
 * @param[out] module 非NULLの出力先で、成功時に対応するHMODULEを受け取ります。失敗時は成功値として使えません
 * @return 非0はWin32呼出し成功、0は失敗です。元GetLastErrorやpin条件をwrapperでは変換・検査しません
 */
//------------------------------------------------------------------------------
BOOL WINAPI pinLibrary( DWORD flags, LPCWSTR address, HMODULE *module)
{
	return GetModuleHandleExW( flags, address, module) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定moduleの一参照をFreeLibraryへ渡して解放要求します
 * @param[in] module callerが取得した非NULLの初期参照です。wrapperは参照を保持しません
 * @return 非0は参照解放要求の成功、0は失敗です。pinや他参照が残ればunloadされず、元GetLastErrorを変換しません
 */
//------------------------------------------------------------------------------
BOOL WINAPI closeLibrary( HMODULE module)
{
	return FreeLibrary( module) ;
}

} // namespace
namespace detail {

//------------------------------------------------------------------------------
/**
 * @brief 渡されたOS表で一回の限定読込みを行います
 * @param[in] api 全関数非NULL、同期中だけ借用する依存表
 * @return pin成功時だけ有効なPFN。不成立の所有参照は一回解放済み
 * @note productionは固定Win32表だけを渡します。呼出し先は例外を投げません
 * @see koefude::detail::loadVulkanLibrary
 */
//------------------------------------------------------------------------------
VulkanLoader loadVulkanLibrary( const VulkanLibraryApi &api) noexcept
{
	// CWD/PATH/app隣や利用者指定directoryへ検索を広げない
	const HMODULE module = api.load( L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) ;
	// 任意runtimeがなければCPU登録を妨げない
	if( !module) {
		return {} ;
	}
	const FARPROC entry = api.symbol( module, "vkGetInstanceProcAddr") ; // module内の固定export
	// NULLのGPAをHppへ渡す前に所有参照を閉じる
	if( !entry) {
		api.close( module) ;
		return {VulkanLoaderStatus::MISSING_ENTRY} ;
	}
	HMODULE pinned = nullptr ; // 住所に対応するpin先。追加の解放責任はOS終了に属する
	// GetModuleHandleExWのFROM_ADDRESS契約でcode住所を渡し、格納も参照解除もしない
	if( !api.pin( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>( entry), &pinned)) {
		api.close( module) ;
		return {VulkanLoaderStatus::PIN_FAILED} ;
	}
	VulkanLoader result ; // pin済みmoduleに属する不変tableの構築
	result.status = VulkanLoaderStatus::READY ;
	// Win32 export境界の同一ABI PFNへ変換する。moduleはprocess終了まで有効
	result.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>( entry) ;
	result.budget.createInstance = reinterpret_cast<PFN_vkCreateInstance>( api.symbol( module, "vkCreateInstance")) ;
	result.budget.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>( api.symbol( module, "vkDestroyInstance")) ;
	result.budget.enumerateDevices =
		reinterpret_cast<PFN_vkEnumeratePhysicalDevices>( api.symbol( module, "vkEnumeratePhysicalDevices")) ;
	result.budget.enumerateExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
		api.symbol( module, "vkEnumerateDeviceExtensionProperties")) ;
	result.budget.properties =
		reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>( api.symbol( module, "vkGetPhysicalDeviceProperties2")) ;
	result.budget.memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
		api.symbol( module, "vkGetPhysicalDeviceMemoryProperties2")) ;
	api.close( module) ; // pinは維持し、LoadLibraryExWが増やした一参照だけをbalanceする
	return result ;
}

} // namespace detail

//------------------------------------------------------------------------------
/**
 * @brief 固定System32 loaderを一度だけ読込み、成功moduleをprocessへpinします
 * @return process終了まで有効な不変表。失敗時のGPAはNULL
 * @note workerから呼びます。初回以外はloadせず、global dispatcherより先に解放しません
 * @see koefude::vulkanLoader
 */
//------------------------------------------------------------------------------
const VulkanLoader &vulkanLoader() noexcept
{
	// C++のthread-safe静的初期化で初回失敗も固定し、自動再loadを行わない
	// CFG対象は自作wrapperとし、import関数住所を可変tableから間接呼出ししません
	static const VulkanLoader result =
		detail::loadVulkanLibrary( {&loadLibrary, &librarySymbol, &pinLibrary, &closeLibrary}) ;
	return result ;
}

} // namespace koefude
