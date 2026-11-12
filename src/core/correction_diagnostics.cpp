//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file correction_diagnostics.cpp
 * @brief 既存処理を変更せず固定scalarを受動集約します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1301の補助CPU接続と限定検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1304の最初の時刻拒否を固定容量へ追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "core/correction_diagnostics.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 最初の拒否scalarだけを保持し、診断失敗をsticky不完全へ変換します
 * @param[in] failure 同期borrowの固定値。本文や所有pointerを持ちません
 * @note mutex内はscalar copyだけです。後続cleanup/sealは記録を消しません
 * @see koefude::CorrectionDiagnosticCapture::whisperTimelineFailed
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::whisperTimelineFailed( const WhisperTimelineFailure &failure) noexcept
{
	// 最初の記録が不能でも、後の拒否を最初の拒否として代用しません
	if( timelineFailureAttempted_.exchange( true, std::memory_order_relaxed)) {
		return ;
	}
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarの検査とcopyだけを排他します
		// 不正な診断値を既定0や正常な観測へ変換しません
		if( static_cast<std::uint32_t>( failure.guard.site) > 2 ||
		   failure.guard.token.has_value() !=
			   (failure.guard.site == WhisperTimelineFailureSite::TOKEN_SCHEMA_OR_RANGE)) {
			value_.valid = false ;
			return ;
		}
		value_.whisperTimelineFailure = failure ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 実選択を固定容量へcopyします
 * @param[in] variant 実表
 * @param[in] reason 選択理由
 * @param[in] digest fast完成SHA又は空
 * @see koefude::CorrectionDiagnosticCapture::whisperSelected
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::whisperSelected( WhisperCpuVariant variant, WhisperCpuReason reason,
												  std::string_view digest) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定値のcopyだけを保護
		// 二重選択や過大な値を診断成功へ補正しません
		if( value_.whisperCpu.variant != WhisperCpuVariant::UNKNOWN || variant == WhisperCpuVariant::UNKNOWN ||
		   static_cast<unsigned>( variant) > 2 || static_cast<unsigned>( reason) > 3 ||
		   (variant == WhisperCpuVariant::AVX2
				? reason != WhisperCpuReason::ELIGIBLE || digest.size() != 64 ||
					  digest.find_first_not_of( "0123456789abcdef") != std::string_view::npos
				: !digest.empty() ||
					  (reason != WhisperCpuReason::CPU_UNSUPPORTED && reason != WhisperCpuReason::SLOT_ABSENT))) {
			value_.valid = false ;
			return ;
		}
		value_.whisperCpu.variant = variant ;
		value_.whisperCpu.reason = reason ;
		std::copy( digest.begin(), digest.end(), value_.whisperCpu.buildSha.begin()) ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同期scopeの一回cleanup結果を記録します
 * @param[in] freeStatus context解放status、未作成はnull
 * @param[in] moduleClosed load済moduleの成否、未loadはnull
 * @see koefude::CorrectionDiagnosticCapture::whisperClosed
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::whisperClosed( std::optional<std::uint32_t> freeStatus,
												std::optional<bool>			 moduleClosed) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定値のsetだけを保護
		value_.whisperCpu.closeAttempted = true ;
		value_.whisperCpu.freeStatus = freeStatus ;
		value_.whisperCpu.moduleClosed = moduleClosed ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 実worker開始を固定します
 * @see koefude::CorrectionDiagnosticCapture::workerStarted
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::workerStarted() noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		value_.workerStarted = true ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 予約後のdispatch入口で時刻を固定します
 * @see koefude::CorrectionDiagnosticCapture::dispatchStarted
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::dispatchStarted() noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		// 実入口の最初の時刻だけを保持します
		if( !value_.dispatchStarted) {
			value_.dispatchStarted = true ;
			dispatchTime_ = std::chrono::steady_clock::now() ;
		}
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 集約前の実進捗を採ります
 * @param[in] value 実通知
 * @see koefude::CorrectionDiagnosticCapture::progress
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::progress( Progress value) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		const auto			  index = static_cast<std::size_t>( value.wholeTextPhase) ; // 固定phase番号
		// 未定義値や実測範囲外を成功へ補正しません
		if( index >= value_.phases.size() || value.percent < -1 || value.percent > 100 ||
		   static_cast<unsigned>( value.stage) > 7) {
			value_.valid = false ;
			return ;
		}
		auto &phase = value_.phases[index] ; // 最大6slot
		phase.observed = true ;
		phase.stage = value.stage ;
		phase.percent =
			value.percent < 0 ? std::nullopt : std::optional<std::uint32_t>( static_cast<std::uint32_t>( value.percent)) ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief native call直前を記録します
 * @see koefude::CorrectionDiagnosticCapture::nativeCall
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::nativeCall() noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		// 上限超過はsticky不完全にし製品を止めません
		if( value_.nativeCalls == 16384) {
			value_.valid = false ;
			return ;
		}
		++value_.nativeCalls ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 成功した生成receipt直後を記録します
 * @see koefude::CorrectionDiagnosticCapture::nativeCompleted
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::nativeCompleted() noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		// callのないreceiptをCPU実行成功へ数えません
		if( value_.nativeCompletions >= value_.nativeCalls) {
			value_.valid = false ;
			return ;
		}
		++value_.nativeCompletions ;
		value_.backend = 0 ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief receiveへ移す前に一度だけ固定します
 * @param[in] result 元終端、同期borrow
 * @see koefude::CorrectionDiagnosticCapture::seal
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::seal( const WholeTextBoundedResult &result) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		// 終端を保存例外や二重通知から変更しません
		if( value_.sealed) {
			return ;
		}
		value_.sealed = true ;
		// 実dispatchがない結果を生成候補へ昇格しません
		if( !value_.dispatchStarted) {
			return ;
		}
		value_.generationWallMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - dispatchTime_)
				.count()) ;
		value_.outcome = result.outcome == WholeTextBoundedOutcome::READY_FOR_PERSISTENCE
							 ? CorrectionDiagnosticOutcome::READY_FOR_PERSISTENCE
						 : result.outcome == WholeTextBoundedOutcome::CANCELLED ? CorrectionDiagnosticOutcome::CANCELLED
																				: CorrectionDiagnosticOutcome::FAILURE ;
		value_.code = result.errorCode ;
		// 成功の既定ErrorKindは未観測です
		if( result.outcome != WholeTextBoundedOutcome::READY_FOR_PERSISTENCE) {
			value_.errorKind = result.errorKind ;
		}
		value_.planObserved = result.binding.recordCount > 0 ;
		// 未封印計画に既定0のcursorを作りません
		if( value_.planObserved) {
			value_.plannedRecords = result.binding.recordCount ;
			value_.plannedGenerations = result.binding.generationCount ;
			value_.completedRecords = result.committed.count ;
			value_.originalBytes = result.binding.originalBytes ;
			value_.committedCoreEnd = result.committed.coreEnd ;
			value_.committedBodyBytes = result.committed.bodyBytes ;
			value_.failedIndex = result.failedIndex ;
		}
		// 実callまたは元非0失敗だけをnative観測とします
		if( value_.nativeCompletions || result.nativeStatus || result.nativeDiagnostic || result.nativeEngineCode) {
			value_.nativeStatus = result.nativeStatus ;
			value_.nativeDiagnostic = result.nativeDiagnostic ;
			value_.nativeEngineCode = result.nativeEngineCode ;
		}
		// guardの実観測または元失敗がある場合だけ理由0も既知です
		if( value_.resources.samples || result.resourceFailure.reason) {
			value_.resourceReason = result.resourceFailure.reason ;
			value_.resourceCode = result.resourceFailure.code ;
		}
		value_.closeAttempted = result.close.attempted ;
		// 未closeを解放成功へ読み替えません
		if( result.close.attempted) {
			value_.closeStatus = result.close.rawStatus ;
			value_.closeDiagnostic = result.close.rawDiagnostic ;
			value_.closeEngineCode = result.close.rawEngineCode ;
			value_.releaseConfirmed = result.close.releaseConfirmed ;
			value_.restartRequired = result.restartRequired ;
		}
		// 全量候補の検証不一致は診断だけを不完全にします
		if( value_.planObserved &&
		   (result.binding.recordCount > 16384 || result.committed.count > result.binding.recordCount ||
			value_.nativeCalls > result.binding.generationCount || result.binding.originalBytes > 8388608 ||
			result.committed.bodyBytes > 8388608)) {
			value_.valid = false ;
		}
		// Readyは全量・解放を必要とします
		if( value_.outcome == CorrectionDiagnosticOutcome::READY_FOR_PERSISTENCE &&
		   (!value_.planObserved || result.failedIndex || result.committed.count != result.binding.recordCount ||
			result.committed.coreEnd != result.binding.originalBytes || !result.close.releaseConfirmed ||
			result.restartRequired || result.resourceFailure.reason)) {
			value_.valid = false ;
		}
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 最新scalarをcopyします
 * @return 本文なしの固定値
 * @see koefude::CorrectionDiagnosticCapture::snapshot
 */
//------------------------------------------------------------------------------
CorrectionDiagnosticSnapshot CorrectionDiagnosticCapture::snapshot() const noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		auto				  result = value_ ; /* 固定copy */
		result.valid = result.valid && !failed_.load( std::memory_order_relaxed) ;
		return result ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
		CorrectionDiagnosticSnapshot failed ; /* 診断だけ失敗 */
		failed.valid = false ;
		return failed ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じprobe標本を集約します
 * @param[in] working 当process byte
 * @param[in] privateBytes private byte
 * @param[in] physical available byte
 * @param[in] commit available byte
 * @see koefude::CorrectionDiagnosticCapture::sample
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::sample( std::uint64_t working, std::uint64_t privateBytes, std::uint64_t physical,
										 std::uint64_t commit) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		auto				 &resource = value_.resources ; /* 同じ標本 */
		const auto			  now = std::chrono::steady_clock::now() ; // 単調時刻
		// 前標本がある場合だけgapを観測します
		if( lastSample_) {
			resource.maxGapMs =
				std::max( resource.maxGapMs.value_or( 0),
						 static_cast<std::uint64_t>(
							 std::chrono::duration_cast<std::chrono::milliseconds>( now - *lastSample_).count())) ;
		}
		lastSample_ = now ;
		resource.peakWorking = std::max( resource.peakWorking.value_or( working), working) ;
		resource.peakPrivate = std::max( resource.peakPrivate.value_or( privateBytes), privateBytes) ;
		resource.minPhysical = std::min( resource.minPhysical.value_or( physical), physical) ;
		resource.minCommit = std::min( resource.minCommit.value_or( commit), commit) ;
		// u64のwrapを成功標本数0へ戻しません
		if( resource.samples == std::numeric_limits<std::uint64_t>::max()) {
			value_.valid = false ;
			resource.status = 2 ;
			return ;
		}
		++resource.samples ;
		// 部分失敗は後の成功標本で消しません
		if( resource.status == 0) {
			resource.status = 1 ;
		}
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 元probe失敗だけを保持します
 * @see koefude::CorrectionDiagnosticCapture::probeFailed
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::probeFailed() noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		value_.resources.status = 2 ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 既存guardの同じ開始snapshotを追加照会なしでcopyします
 * @param[in] working 開始working set byte
 * @param[in] privateBytes 開始private byte
 * @param[in] workingLimit 既存working set上限byte
 * @param[in] privateLimit 既存private上限byte
 * @param[in] usableTotal 同じ開始snapshotのOS usable total、未提供はnull
 * @param[in] physical 同じ開始snapshotのavailable physical、未提供はnull
 * @param[in] commit 同じ開始snapshotのavailable commit、未提供はnull
 * @see koefude::CorrectionDiagnosticCapture::guardLimits
 */
//------------------------------------------------------------------------------
void CorrectionDiagnosticCapture::guardLimits( std::uint64_t working, std::uint64_t privateBytes,
											  std::uint64_t workingLimit, std::uint64_t privateLimit,
											  std::optional<std::uint64_t> usableTotal,
											  std::optional<std::uint64_t> physical,
											  std::optional<std::uint64_t> commit) noexcept
{
	try {
		const std::lock_guard lock( mutex_) ; // 固定scalarのcopy/setだけを保護する短い排他
		auto				 &resource = value_.resources ; /* 既存値 */
		resource.baselineWorking = working ;
		resource.baselinePrivate = privateBytes ;
		resource.workingLimit = workingLimit ;
		resource.privateLimit = privateLimit ;
		resource.baselineUsableTotal = usableTotal ;
		resource.baselineAvailablePhysical = physical ;
		resource.baselineAvailableCommit = commit ;
	} catch( ...) {
		failed_.store( true, std::memory_order_relaxed) ;
	}
}
} // namespace koefude
