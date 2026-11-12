//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_resource_guard.cpp
 * @brief OS実値で明示CPU試用の協調停止を実装します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 半余裕policyとsticky停止を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1313のtotal検証と固定16GB予算を追加
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
#include "inference/whole_text_resource_guard.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <windows.h>

// Win32基本型を定義してから自processのmemory照会を宣言します
#include <psapi.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 当processとOSの現在値を取得します
 * @return 現在snapshot
 * @throws OperationError API取得失敗、元code保持
 * @see koefude::probeWholeTextResources
 */
//------------------------------------------------------------------------------
WholeTextResourceSnapshot probeWholeTextResources()
{
	MEMORYSTATUSEX			   memory{sizeof(memory)} ; // OS全体の実値
	PROCESS_MEMORY_COUNTERS_EX process{} ; // 自processだけの実値
	process.cb = sizeof(process) ;
	// 取得失敗を0容量やMissingへ隠しません
	if( !GlobalMemoryStatusEx( &memory) ||
	   !K32GetProcessMemoryInfo( GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>( &process),
								sizeof(process))) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, GetLastError()) ;
	}
	return {memory.ullAvailPhys, memory.ullAvailPageFile, process.WorkingSetSize, process.PrivateUsage,
			memory.ullTotalPhys} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 開始snapshotからpolicyの予算を一度だけ固定します
 * @param[in] probe 同期中有効な所有probe。空不可
 * @param[in] policy 明示予算、既定は従来研究用。installed容量を推定しません
 * @throws OperationError 取得/予算/整数境界違反
 * @see koefude::WholeTextResourceGuard::WholeTextResourceGuard
 */
//------------------------------------------------------------------------------
WholeTextResourceGuard::WholeTextResourceGuard( WholeTextResourceProbe probe, const WholeTextResourcePolicy policy)
	: probe_( std::move( probe)), policy_( policy)
{
	// 空のprobeで開始を許可しません
	if( !probe_) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, ERROR_INVALID_FUNCTION) ;
	}
	try {
		baseline_ = probe_() ;
	} catch( const OperationError &error) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, error.code()) ;
	} catch( ...) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, ERROR_GEN_FAILURE) ;
	}
	// 16GB入口だけでOS usableと固定余白を検証し、搭載容量は推定しません
	if( policy_ == WholeTextResourcePolicy::LOW_MEMORY16_GB) {
		constexpr std::uint64_t K_GI_B = 1ULL << 30 ; // byte単位の二進容量
		constexpr auto			K_FLOOR = 2 * K_GI_B ; // physical/commitに残す工学上の床
		constexpr auto			K_HEADROOM = 4 * K_GI_B ; // usableからOS等へ残す余白
		constexpr auto			K_CAP = 10 * K_GI_B ; // process全体の工学上限
		// 欠測・不整合・引算不能・開始床不足を後段I/Oより先に拒否します
		if( baseline_.usableTotalPhysical <= K_HEADROOM || baseline_.availablePhysical > baseline_.usableTotalPhysical ||
		   baseline_.workingSet > baseline_.usableTotalPhysical || baseline_.availablePhysical < K_FLOOR ||
		   baseline_.availableCommit < K_FLOOR) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE, 0x57550001) ;
		}
		const auto physicalGrowth = baseline_.availablePhysical - K_FLOOR ; // 検証済み差分
		const auto commitGrowth = baseline_.availableCommit - K_FLOOR ; // 検証済み差分
		// minで飽和させず、元加算が表現できないsnapshotを拒否します
		if( baseline_.workingSet > std::numeric_limits<std::uint64_t>::max() - physicalGrowth ||
		   baseline_.privateBytes > std::numeric_limits<std::uint64_t>::max() - commitGrowth) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE, 0x57550001) ;
		}
		physicalReserve_ = K_FLOOR ;
		commitReserve_ = K_FLOOR ;
		workingLimit_ =
			std::min( {K_CAP, baseline_.usableTotalPhysical - K_HEADROOM, baseline_.workingSet + physicalGrowth}) ;
		privateLimit_ = std::min( K_CAP, baseline_.privateBytes + commitGrowth) ;
		// 開始時点ですでにcapを超えるprocessは等号だけを除き拒否します
		if( baseline_.workingSet > workingLimit_ || baseline_.privateBytes > privateLimit_) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE, 0x57550001) ;
		}
		return ;
	}
	physicalReserve_ = baseline_.availablePhysical / 2 ;
	commitReserve_ = baseline_.availableCommit / 2 ;
	// 増分0またはbaseline加算overflowならnativeへ進みません
	if( physicalReserve_ == 0 || commitReserve_ == 0 ||
	   baseline_.workingSet > std::numeric_limits<std::uint64_t>::max() - physicalReserve_ ||
	   baseline_.privateBytes > std::numeric_limits<std::uint64_t>::max() - commitReserve_) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, 0x57550001) ;
	}
	workingLimit_ = baseline_.workingSet + physicalReserve_ ;
	privateLimit_ = baseline_.privateBytes + commitReserve_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief callbackから最大100ms間隔で非blocking照会します
 * @param[in] force phase境界で間隔にかかわらず検査
 * @return sticky停止ならtrue
 * @see koefude::WholeTextResourceGuard::poll
 */
//------------------------------------------------------------------------------
bool WholeTextResourceGuard::poll( const bool force) noexcept
{
	// sticky停止は追加のOS呼出しを要しません
	if( failure_.load() != 0) {
		return true ;
	}
	const auto now = GetTickCount64() ; // wrapしない単調ms
	// 同期native callbackの競合では待たず他の担当へ任せます
	if( (!force && now - lastPoll_.load() < 100) || polling_.test_and_set()) {
		return failure_.load() != 0 ;
	}
	std::uint32_t reason = 0 ; // 今回照会で得た停止理由
	std::uint32_t code = 0 ; // probe失敗の元code
	try {
		const auto current = probe_() ; // callback内はOS照会だけ
		// 新policyの欠測やavailable/workingとtotalの不整合もsticky停止です
		if( policy_ == WholeTextResourcePolicy::LOW_MEMORY16_GB &&
		   (current.usableTotalPhysical != baseline_.usableTotalPhysical ||
			current.availablePhysical > current.usableTotalPhysical ||
			current.workingSet > current.usableTotalPhysical)) {
			reason = 1 ;
		}
		// 同値境界は継続し、1byte不足/超過で停止します
		else if( current.availablePhysical < physicalReserve_) {
			reason = 1 ;
			// 開始commit予約を下回った場合もsticky停止します
		} else if( current.availableCommit < commitReserve_) {
			reason = 1 ;
			// 当processのworking増分が予算を超えた場合に停止します
		} else if( current.workingSet > workingLimit_) {
			reason = 1 ;
			// 当processのprivate増分が予算を超えた場合に停止します
		} else if( current.privateBytes > privateLimit_) {
			reason = 1 ;
		}
	} catch( const OperationError &error) {
		reason = 2 ;
		code = error.code() ;
	} catch( ...) {
		reason = 2 ;
		code = ERROR_GEN_FAILURE ;
	}
	std::uint64_t expected = 0 ; // 先行停止理由を上書きしません
	// probe失敗/超過だけをsticky化します
	if( reason != 0) {
		failure_.compare_exchange_strong( expected,
										 (static_cast<std::uint64_t>( reason == 1 ? 0x57550001U : code) << 32) | reason) ;
	}
	lastPoll_.store( now) ;
	polling_.clear() ;
	return failure_.load() != 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief user stopを優先し、資源失敗を例外へ変換します
 * @param[in] stop 利用者停止
 * @param[in] nativeCode 非0なら元engine診断を維持
 * @see koefude::WholeTextResourceGuard::check
 */
//------------------------------------------------------------------------------
void WholeTextResourceGuard::check( const std::stop_token stop, const std::uint32_t nativeCode)
{
	// user取消しは資源停止とnative失敗より優先します
	if( stop.stop_requested()) {
		throw OperationError( ErrorKind::CANCELLED, nativeCode) ;
	}
	// callbackなし区間からreturnした後にも再照会します
	const bool exceeded = poll( true) ; // OS照会中の停止も最後に優先します
	// callback/OS照会と競合した利用者停止を資源失敗へ変えません
	if( stop.stop_requested()) {
		throw OperationError( ErrorKind::CANCELLED, nativeCode) ;
	}
	// 最初のprobe理由/codeをnative元codeがない場合の正本にします
	if( exceeded) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE, nativeCode != 0 ? nativeCode : code()) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 最初の理由/codeを不可分に読みます
 * @return sticky失敗の組
 * @see koefude::WholeTextResourceGuard::failure
 */
//------------------------------------------------------------------------------
WholeTextResourceFailure WholeTextResourceGuard::failure() const noexcept
{
	const auto value = failure_.load() ; // 理由/codeを同時に取得
	return {static_cast<std::uint32_t>( value), static_cast<std::uint32_t>( value >> 32)} ;
}
} // namespace koefude
