//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_resource_guard.h
 * @brief 明示CPU試用の開始余裕と協調停止を定義します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 開始余裕半分の試用policyを追加
 * 0.1.0 Shunji Ogawa 2026/10/19 policyを変えず実測用の読取専用値を公開
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1313の16GB共通予算policyを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 割当てのhard limitや測定peakを保証しません
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/types.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <stop_token>

namespace koefude {
/** @brief 同じ瞬間に照会したOSと当processのbyte値です */
struct WholeTextResourceSample {
	/// OS available physical
	std::uint64_t availablePhysical = 0 ;
	/// OS available page-file/commit
	std::uint64_t availableCommit = 0 ;
	/// 当process working set
	std::uint64_t workingSet = 0 ;
	/// 当process private usage
	std::uint64_t privateBytes = 0 ;
	/// OS usable total、搭載容量の分類には使いません
	std::uint64_t usableTotalPhysical = 0 ;
} ;
/** @brief 開始snapshotへ適用する明示的な工学予算です */
enum class WholeTextResourcePolicy {
	/// 従来の開始available半分予約
	RESEARCH_HALF_RESERVE,
	/// 16GB対象の10GiB capと2GiB available床
	LOW_MEMORY16_GB
} ;
using WholeTextResourceSnapshot = WholeTextResourceSample ; // 内部呼出しの互換名
using WholeTextResourceProbe = std::function<WholeTextResourceSample()> ; // 同期照会seam
/** @brief 最初の停止理由と元codeを一組で公開します */
struct WholeTextResourceFailure {
	/// 0正常、1予算超過、2probe失敗
	std::uint32_t reason = 0 ;
	/// 予算0x57550001、probeは元Win32 code
	std::uint32_t code = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 当processとOSの現在値を取得します
 * @return 現在snapshot
 * @throws OperationError API取得失敗、元code保持
 */
//------------------------------------------------------------------------------
[[nodiscard]] WholeTextResourceSnapshot probeWholeTextResources() ;
/** @brief 開始値を固定し最初の資源停止理由をatomicで保持します */
class WholeTextResourceGuard			final {
	   public:
	   //------------------------------------------------------------------------------
	   /**
	    * @brief 開始snapshotからpolicyの予算を一度だけ固定します
	    * @param[in] probe 同期中有効な所有probe。空不可
	    * @param[in] policy 明示予算、既定は従来研究用。installed容量を推定しません
	    * @throws OperationError 取得/予算/整数境界違反
	    */
	   //------------------------------------------------------------------------------
	   explicit WholeTextResourceGuard( WholeTextResourceProbe  probe,
											   WholeTextResourcePolicy policy = WholeTextResourcePolicy::RESEARCH_HALF_RESERVE) ;
	   //------------------------------------------------------------------------------
	   /**
	    * @brief callbackから最大100ms間隔で非blocking照会します
	    * @param[in] force phase境界で間隔にかかわらず検査
	    * @return sticky停止ならtrue
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] bool					  poll( bool force = false) noexcept ;
	   //------------------------------------------------------------------------------
	   /**
	    * @brief 最初の理由/codeを不可分に読みます
	    * @return sticky失敗の組
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] WholeTextResourceFailure failure() const noexcept ;
	   //------------------------------------------------------------------------------
	   /**
	    * @brief user stopを優先し、資源失敗を例外へ変換します
	    * @param[in] stop 利用者停止
	    * @param[in] nativeCode 非0なら元engine診断を維持
	    */
	   //------------------------------------------------------------------------------
	   void									  check( std::stop_token stop, std::uint32_t nativeCode = 0) ;
	   //------------------------------------------------------------------------------
	   /**
	    * @brief sticky診断を読みます
	    * @return 未停止0、予算の専用codeまたはprobe元code
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] std::uint32_t			  code() const noexcept
	   {
		   return failure().code ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief 開始値を読みます
	    * @return 不変のbyte snapshot
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] WholeTextResourceSnapshot baseline() const noexcept
	   {
		   return baseline_ ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief physical予約を読みます
	    * @return byte
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] std::uint64_t physicalReserve() const noexcept
	   {
		   return physicalReserve_ ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief commit予約を読みます
	    * @return byte
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] std::uint64_t commitReserve() const noexcept
	   {
		   return commitReserve_ ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief working上限を読みます
	    * @return byte
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] std::uint64_t workingLimit() const noexcept
	   {
		   return workingLimit_ ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief private上限を読みます
	    * @return byte
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] std::uint64_t privateLimit() const noexcept
	   {
		   return privateLimit_ ;
	   }
	   //------------------------------------------------------------------------------
	   /**
	    * @brief 固定したpolicyを読みます
	    * @return 開始時の明示policy
	    */
	   //------------------------------------------------------------------------------
	   [[nodiscard]] WholeTextResourcePolicy policy() const noexcept
	   {
		   return policy_ ;
	   }

	   private:
	   /// callback全生存期間の照会関数
	   WholeTextResourceProbe	  probe_ ;
	   /// 全段階で不変の予算選択
	   WholeTextResourcePolicy	  policy_ ;
	   /// policyで指定した開始境界の値
	   WholeTextResourceSnapshot  baseline_ ;
	   /// physicalの継続床
	   std::uint64_t			  physicalReserve_ = 0 ;
	   /// commitの継続床
	   std::uint64_t			  commitReserve_ = 0 ;
	   /// 開始時に固定したprocess上限
	   std::uint64_t			  workingLimit_ = 0 ;
	   /// 開始時に固定したprocess上限
	   std::uint64_t			  privateLimit_ = 0 ;
	   /// 同時callbackの一照会担当
	   std::atomic_flag			  polling_ = ATOMIC_FLAG_INIT ;
	   /// 単調時刻ms
	   std::atomic<std::uint64_t> lastPoll_ = 0 ;
	   /// 上位code/下位reasonの不可分な組
	   std::atomic<std::uint64_t> failure_ = 0 ;
} ;
} // namespace koefude
