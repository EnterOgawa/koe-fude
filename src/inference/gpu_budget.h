//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file gpu_budget.h
 * @brief 同じGPUの実予算を確認する契約を定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 未知予算を全容量として扱わない判定を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <string_view>

namespace koefude {

constexpr std::uint64_t K_GI_B = 1024ULL * 1024 * 1024 ; // GPU容量の換算単位

/** @brief device-local heapの実予算です */
struct GpuBudget {
	/// 拡張機能と同一device照合が成立した場合だけtrue
	bool		  known = false ;
	/// 報告された物理byte数
	std::uint64_t physical = 0 ;
	/// heap予算から使用量を安全に差し引いたbyte数
	std::uint64_t available = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 検証済みprofileの必要量を実予算へ照合します
 * @param[in] budget 同じdeviceの取得結果
 * @param[in] required 実行に確保する保守的な必要byte数
 * @param[in] reserve 他processのために残すbyte数
 * @return 12GiB以下で必要予算がある場合true。不明値はfalse
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool		fitsGpuBudget( const GpuBudget &budget, std::uint64_t required, std::uint64_t reserve) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief PCI IDを照合してVulkanのmemory budgetを取得します
 * @param[in] pciId ggmlが返した同じGPUのPCI ID。UI/logへ渡さない
 * @return extension不足、照合不能、API失敗ならknown=false
 * @note 事前観測であり、以後のdriver割当てのhard limitではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] GpuBudget queryGpuBudget( std::string_view pciId) ;

} // namespace koefude
