//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file observed_asr_timeline.h
 * @brief 共有Whisper窓の時刻拒否と研究全文のmetadata境界を定義します
 * @version 1.0.0
 * @date 2026/10/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/09 厳密segmentと原値を残す全文収集の境界を分離
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1304の拒否scalar outを既存guardへ追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1306のnative表現由来の全文metadata上限を適用
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

#include "core/engines.h"
#include "core/shared_asr_windows.h"
#include "core/types.h"
#include "core/whisper_timeline_diagnostic.h"

namespace koefude {

constexpr std::uint64_t K_WHISPER_TIMESTAMP_QUANTUM_SAMPLES =
	2 * 160 ; // 固定whisper.cppのtimestamp tokenは2 hop、16kHzで20msです
constexpr std::uint64_t K_WHISPER_NATIVE_TIMESTAMP_SPAN_CENTISECONDS =
	30 * 100 ; // 固定nativeのtimestamp語彙が表す30秒幅、単位csです

//------------------------------------------------------------------------------
/**
 * @brief 実窓からnative内部seek・token処理を覆う全文metadata上限を求めます
 * @param[in] actualWindowSamples 検査対象の16kHz実窓長、単位sample
 * @return 3000+2*ceil(sample数/160)の有限な入場上限、単位cs
 * @throws OperationError 実窓長が0又は416000sample超過
 * @note 実音声時刻の上限や精度保証ではなく、原値を変更しないmetadata入場契約です
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t whisperObservedTimelineMaximumCs( const std::uint64_t actualWindowSamples)
{
	// 加算前に既存の26秒実窓範囲を要求し、巨大値のoverflowも防ぎます
	if( actualWindowSamples == 0 || actualWindowSamples > 416000) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	const auto hops =
		actualWindowSamples / 160 + (actualWindowSamples % 160 != 0 ? 1U : 0U) ; // 実窓ceil10ms長、単位cs
	return K_WHISPER_NATIVE_TIMESTAMP_SPAN_CENTISECONDS + 2 * hops ;
}

//------------------------------------------------------------------------------
/**
 * @brief 共有窓用途が既定またはobserver付きの明示全文用途であることを確認します
 * @param[in] use 通常segmentまたは研究全文用途。未知enum値を拒否します
 * @param[in] observed 同期observerが存在する場合true
 * @throws OperationError 未知用途、observerなしの全文用途
 */
//------------------------------------------------------------------------------
inline void checkAsrTimelineUse( const AsrTimelineUse use, const bool observed)
{
	// 本文収集の緩和を観測なしの通常入口へ適用しません
	if( (use != AsrTimelineUse::STRICT_SEGMENTS && use != AsrTimelineUse::OBSERVED_WHOLE_TEXT) ||
	   (use == AsrTimelineUse::OBSERVED_WHOLE_TEXT && !observed)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 原行時刻を修正せず、通常拒否または有界な全文metadata入場を検査します
 * @param[in] row 実16kHz窓へ束縛された元行と元token。所有値を変更しません
 * @param[in] use 通常segmentまたはobserver付き全文用途
 * @param[out] failure null可、同期borrow。拒否したguardの元scalarだけを設定し成功時は不変
 * @throws OperationError 不正窓、時刻、非有限、envelope逸脱、未知単位
 * @note 全文用途の通過は正確な時刻・話者の証明ではありません
 * token逆転・零長・順序不成立とcentisecondsの-1は原値のまま保持します
 */
//------------------------------------------------------------------------------
inline void checkWhisperRowTimeline( const RawAsrRow &row, const AsrTimelineUse use,
									std::optional<WhisperTimelineGuardFailure> *failure = nullptr)
{
	checkAsrTimelineUse( use, true) ;
	checkSharedAsrWindows( std::span( &row.window, 1), row.window.end) ;
	const auto samples = row.window.end - row.window.begin ; // 実窓の検査済みsample数
	// 原行の非有限・負値とnative csから外れたschemaを修復しません
	if( !std::isfinite( row.startMs) || !std::isfinite( row.endMs) || row.startMs < 0 || row.endMs < 0 ||
	   row.startMs / 10.0 != std::trunc( row.startMs / 10.0) || row.endMs / 10.0 != std::trunc( row.endMs / 10.0)) {
		// 明示outがある拒否だけを記録し、元guardと例外を変更しません
		if( failure) {
			*failure = {WhisperTimelineFailureSite::ROW_SCHEMA, std::bit_cast<std::uint64_t>( row.startMs),
						std::bit_cast<std::uint64_t>( row.endMs), std::nullopt} ;
		}
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	// 通常用途は従来のraw csとfloor(sample/160)の拒否条件を維持します
	if( use == AsrTimelineUse::STRICT_SEGMENTS) {
		// 逆転や実窓終端を越えた行を通常segmentへ渡しません
		if( row.endMs < row.startMs || row.endMs / 10.0 > static_cast<double>( samples / 160)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		return ;
	}
	const auto maximumCs =
		static_cast<double>( whisperObservedTimelineMaximumCs( samples)) ; // native表現を覆うmetadata上限、単位cs
	// 有限なmetadata上限を越す行は時刻Unknownとしても入場させません
	if( row.startMs / 10.0 > maximumCs || row.endMs / 10.0 > maximumCs) {
		// 元doubleの全bitをcopyし、clipや原値の補正を行いません
		if( failure) {
			*failure = {WhisperTimelineFailureSite::ROW_ENVELOPE, std::bit_cast<std::uint64_t>( row.startMs),
						std::bit_cast<std::uint64_t>( row.endMs), std::nullopt} ;
		}
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	std::uint64_t tokenIndex = 0 ; // 検査順そのままの行内0始まりindex
	// 特別tokenも含め全原tokenの有限値・単位・表現範囲だけを検査します
	for( const auto &token : row.tokens) { // 当該行の原token metadata
		// -1だけをnative未成立sentinelとして残し、他negativeや巨大値を拒否します
		if( token.unit != "centiseconds" || !std::isfinite( token.start) || !std::isfinite( token.end) ||
		   token.start < -1 || token.end < -1 || (token.start < 0 && token.start != -1) ||
		   (token.end < 0 && token.end != -1) || token.start > maximumCs || token.end > maximumCs ||
		   token.start != std::trunc( token.start) || token.end != std::trunc( token.end)) {
			// token本文や任意単位を保持せず、当該判定scalarだけをcopyします
			if( failure) {
				*failure = {WhisperTimelineFailureSite::TOKEN_SCHEMA_OR_RANGE,
							std::bit_cast<std::uint64_t>( row.startMs), std::bit_cast<std::uint64_t>( row.endMs),
							WhisperTimelineTokenFailure{tokenIndex, std::bit_cast<std::uint64_t>( token.start),
														std::bit_cast<std::uint64_t>( token.end),
														token.unit == "centiseconds", token.special}} ;
			}
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		++tokenIndex ;
	}
}

} // namespace koefude
