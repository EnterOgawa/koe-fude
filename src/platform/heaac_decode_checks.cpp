//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file heaac_decode_checks.cpp
 * @brief HE-AACの部分PCM成功と時刻不連続を拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 固定した全packet時間を実PCMと照合
 * 0.1.0 Shunji Ogawa 2026/10/02 長時間で累積する丸めを局所gapと期間の照合へ分離
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
#include "platform/heaac_decode_checks.h"
#include "core/types.h"
#include <cstdint>
#include <limits>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 追記前の16kHz mono総数を宣言ceilへ照合します
 * @param[in] info inspectM4aHeaacの成功値。同期呼出し中だけ借用します
 * @param[in] actual 追記した場合の総sample数。途中の0を許可します
 * @throws OperationError 上限超過はInputTooLarge、宣言超過はInvalidTimeline
 * @see koefude::validateHeaacOutputUpperBound
 */
//------------------------------------------------------------------------------
void validateHeaacOutputUpperBound( const HeAacInputInfo &info, const std::uint64_t actual)
{
	// parserと実数のどちらも整数時刻の表現余裕を維持する
	if( info.outputCeil > K_MAXIMUM_TIMELINE_SAMPLES || actual > K_MAXIMUM_TIMELINE_SAMPLES) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	// 宣言を超えるPCMを所有一時fileへ追記しない
	if( actual > info.outputCeil) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 終端総数を宣言floor～ceilへ照合します
 * @param[in] info parser成功値
 * @param[in] actual 終端の実16kHz mono総sample数
 * @throws OperationError 空はDecodeFailed、範囲外はInvalidTimeline、資源はInputTooLarge
 * @see koefude::validateHeaacOutputSamples
 */
//------------------------------------------------------------------------------
void validateHeaacOutputSamples( const HeAacInputInfo &info, const std::uint64_t actual)
{
	// 空の早い終端を文字起こし可能な音声として扱わない
	if( actual == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	validateHeaacOutputUpperBound( info, actual) ;
	// 端数丸めの幅だけを許可し、欠落を部分成功へまとめない
	if( actual < info.outputFloor) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 初回原点・隣接sample時刻と正確な期間を照合します
 * @param[in,out] timeline 呼出しごとのworker所有状態。成功時だけ更新します
 * @param[in] previous 既に追記した16kHz mono sample数
 * @param[in] count 今回追記するsample数
 * @param[in] timestamp 今回sampleの開始100ns時刻
 * @param[in] duration 今回sampleの100ns期間
 * @throws OperationError 空bufferはDecodeFailed、時刻/状態差はInvalidTimeline、資源はInputTooLarge
 * @note 初回0必須。以後の開始は直前終端から上下1×100ns、期間は今回数×625です
 * @see koefude::validateHeaacSampleTimeline
 */
//------------------------------------------------------------------------------
void validateHeaacSampleTimeline( HeAacTimeline &timeline, const std::uint64_t previous, const std::uint64_t count,
								 const std::int64_t timestamp, const std::int64_t duration)
{
	// 加算・100ns乗算の前に累積数の表現範囲を確認する
	if( previous > K_MAXIMUM_TIMELINE_SAMPLES || count > K_MAXIMUM_TIMELINE_SAMPLES - previous) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	// 空bufferの無進行反復を正常なHE sampleとして許可しない
	if( count == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// 負時刻/期間と終端加算のoverflowを整数加算前に拒否する
	if( timestamp < 0 || duration < 0 || duration > std::numeric_limits<std::int64_t>::max() - timestamp ||
	   timeline.end < 0 || previous != timeline.samples || (timeline.started != (previous != 0))) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	const auto expectedDuration = static_cast<std::int64_t>( count * 625) ; // 実数の正確な期間
	const auto end = timestamp + duration ; // checkedなnative終端100ns
	const auto gap = timestamp - timeline.end ; // 非負値同士から安全に求めた隣接差
	// 累積した丸めを固定幅へ押し込めず、各境界の1刻みと実期間を検査する
	if( (!timeline.started && timestamp != 0) || (timeline.started && (gap < -1 || gap > 1)) ||
	   duration != expectedDuration) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	timeline.samples = previous + count ;
	timeline.end = end ;
	timeline.started = true ;
}
} // namespace koefude
