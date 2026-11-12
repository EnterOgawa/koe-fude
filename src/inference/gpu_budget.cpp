//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file gpu_budget.cpp
 * @brief Vulkan実予算の取得とoverflowを避けた判定を実装します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 不明予算の拒否と同一GPU照合を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 callbackの契約・宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1367の任意PFNと作成前cleanup検証へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "inference/gpu_budget.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

#if KOEFUDE_HAS_VULKAN
#include "platform/vulkan_loader.h"
#endif

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 検証済みprofileの必要量を実予算へ照合します
 * @param[in] budget 同じdeviceの取得結果
 * @param[in] required 実行に確保する保守的な必要byte数
 * @param[in] reserve 他processのために残すbyte数
 * @return 12GiB以下で必要予算がある場合true。不明値はfalse
 * @see koefude::fitsGpuBudget
 */
//------------------------------------------------------------------------------
bool fitsGpuBudget( const GpuBudget &budget, const std::uint64_t required, const std::uint64_t reserve) noexcept
{
	return budget.known && required > 0 && budget.available <= budget.physical && budget.available > reserve &&
		   std::min( 12 * K_GI_B, budget.available - reserve) >= required ;
}

//------------------------------------------------------------------------------
/**
 * @brief PCI IDを照合してVulkanのmemory budgetを取得します
 * @param[in] pciId ggmlが返した同じGPUのPCI ID。UI/logへ渡さない
 * @return extension不足、照合不能、API失敗ならknown=false
 * @note 事前観測であり、以後のdriver割当てのhard limitではありません
 * @see koefude::queryGpuBudget
 */
//------------------------------------------------------------------------------
GpuBudget queryGpuBudget( const std::string_view pciId)
{
#if KOEFUDE_HAS_VULKAN
    // 同一deviceを照合できない場合に別GPUの予算で許可しない
	if( pciId.empty()) {
		return {} ;
	}
	const auto &api = vulkanLoader().budget ; // pin済みmoduleの不変PFNを同期借用
	// 破棄entryも作成前に要求し、部分成功でcleanup不能なinstanceを作らない
	if( !api.createInstance || !api.destroyInstance || !api.enumerateDevices || !api.enumerateExtensions ||
	   !api.properties || !api.memory) {
		return {} ;
	}
	VkApplicationInfo application{} ; // 物理device照会に必要なVulkan 1.1だけを要求
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO ;
	application.apiVersion = VK_API_VERSION_1_1 ;
	VkInstanceCreateInfo configuration{} ; // networkもdevice allocationも行わないinstance設定
	configuration.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO ;
	configuration.pApplicationInfo = &application ;
	VkInstance rawInstance = VK_NULL_HANDLE ; // 成功後にRAIIへ移すinstance
	// loader非対応やdriver異常時は予算不明として返す
	if( api.createInstance( &configuration, nullptr, &rawInstance) != VK_SUCCESS || !rawInstance) {
		return {} ;
	}
	const auto destroy =
		//------------------------------------------------------------------------------
		/**
		 * @brief 所有するVulkan instanceを専用APIで解放します
		 * @param[in] value unique_ptrが所有する非NULLのinstance
		 * @note 戻り値はなく、捕捉を持たず、従属deviceの使用終了後にだけ呼び出します
		 */
		//------------------------------------------------------------------------------
		[&api]( VkInstance value)
		{
			api.destroyInstance( value, nullptr) ;
		} ; // instanceより長寿命の不変表を同期借用する解放責任
	// deviceの列挙と照会を含む全経路でinstanceを解放する所有RAII
	const std::unique_ptr<VkInstance_T, decltype(destroy)> instance( rawInstance, destroy) ;
	std::array<VkPhysicalDevice, 16>					   devices{} ; // 想定外のadapter数を無制限に確保しない上限
	std::uint32_t count = static_cast<std::uint32_t>( devices.size()) ; // 書込み可能なdevice数
	// 配列上限超過も部分列挙で進めず予算不明として扱う
	if( api.enumerateDevices( instance.get(), &count, devices.data()) != VK_SUCCESS || count > devices.size()) {
		return {} ;
	}
	// 全物理deviceからggmlと同じPCI IDを持つものだけを取得する
	for( std::uint32_t index = 0; index < count; ++index) {
		const VkPhysicalDevice				   device = devices[index] ; // instanceに従属する非所有device
		std::array<VkExtensionProperties, 512> extensions{} ; // boundedなextension一覧
		// 列挙先配列の容量を入力し、成功時に実extension数を受け取る値
		std::uint32_t						   extensionCount = static_cast<std::uint32_t>( extensions.size()) ;
		// extension一覧の欠落時にbudget supportを仮定しない
		if( api.enumerateExtensions( device, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS ||
		   extensionCount > extensions.size()) {
			continue ;
		}
		const auto has =
			//------------------------------------------------------------------------------
			/**
			 * @brief 列挙済みextensionの完全一致を調べます
			 * @param[in] name 同期検索の終了まで有効な非NULL・NUL終端のextension名
			 * @return 同じ名前が列挙済み範囲にあればtrue
			 * @note extension配列と件数は同じscopeで参照し、callbackを保存しません
			 */
			//------------------------------------------------------------------------------
			[&]( const char *name)
			{
				return std::any_of( extensions.begin(), extensions.begin() + extensionCount,
							   //------------------------------------------------------------------------------
							   /**
							    * @brief 一つのextension名を要求名と照合します
							    * @param[in] extension 同期any_of中だけ借用する列挙済み属性
							    * @return NUL終端名が完全一致すればtrue
							    * @note nameのpointerを値で捕捉し、指す文字列は同期検索中に有効です
							    */
							   //------------------------------------------------------------------------------
							   [name]( const VkExtensionProperties &extension)
							   {
								   return std::strcmp( extension.extensionName, name) == 0 ;
							   }) ;
			} ; // 完全一致のextension照合
		// 不明な空き量を全VRAMへ置き換えず、二つのextensionを必須にする
		if( !has( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) || !has( VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)) {
			continue ;
		}
		VkPhysicalDevicePCIBusInfoPropertiesEXT pci{} ; // 同一adapter照合専用のPCI情報
		pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT ;
		VkPhysicalDeviceProperties2 properties{} ; // 標準物理属性とPCI拡張を受領
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 ;
		properties.pNext = &pci ;
		api.properties( device, &properties) ;
		std::array<char, 48> identifier{} ; // ggmlと同じ表記の比較用ID
		std::snprintf( identifier.data(), identifier.size(), "%04x:%02x:%02x.%x", pci.pciDomain, pci.pciBus,
					  pci.pciDevice, pci.pciFunction) ;
		// 内蔵GPUや別adapterのshared RAMを専用VRAMとして扱わない
		if( pciId != identifier.data() || properties.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			continue ;
		}
		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{} ; // heapごとの実予算と使用量
		budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT ;
		VkPhysicalDeviceMemoryProperties2 memory{} ; // heap物理sizeとbudgetを同時に受領
		memory.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 ;
		memory.pNext = &budget ;
		api.memory( device, &memory) ;
		// 破損したdriver応答で固定heap配列を越えて読まない
		if( memory.memoryProperties.memoryHeapCount > VK_MAX_MEMORY_HEAPS) {
			return {} ;
		}
		GpuBudget result{true, 0, 0} ; // 照合済みdeviceの合計
		// Vulkanが定義した固定heap数だけを加算する
		for( std::uint32_t heap = 0; heap < memory.memoryProperties.memoryHeapCount; ++heap) {
			const auto &description = memory.memoryProperties.memoryHeaps[heap] ; // 非所有heap記述
			// system RAM heapをGPU専用予算へ混ぜない
			if( (description.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
				continue ;
			}
			const auto limit = std::min( description.size, budget.heapBudget[heap]) ; // 物理容量以下の予算
			// heap予算から既存使用量を差し引いた残byte数。予算超過時は0
			const auto available = limit > budget.heapUsage[heap] ? limit - budget.heapUsage[heap] : 0 ;
			// 複数heapでも評価上限で飽和させ、加算overflowを防ぐ
			result.physical += std::min<std::uint64_t>( description.size, 12 * K_GI_B - result.physical) ;
			result.available += std::min<std::uint64_t>( available, 12 * K_GI_B - result.available) ;
		}
		return result ;
	}
#else
    static_cast<void>( pciId) ; // CPU buildではVulkan loaderを要求しない
#endif
    return {} ;
}

} // namespace koefude
