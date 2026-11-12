//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file flac_decode_checks.cpp
 * @brief FLAC宣言属性とnative属性・実PCM数の不一致を拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 ADR 0247の停止境界をpure検査へ分離
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
#include "platform/flac_decode_checks.h"

#include <cstdint>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief STREAMINFOと必須native属性を照合します
 * @param[in] info parser成功時の所有metadata。呼出し中だけ借用します
 * @param[in] rate MFの元周波数（Hz）。欠落は0
 * @param[in] channels MFの元channel数。欠落は0
 * @param[in] bits MFの元bit深度。欠落は0
 * @param[in] align MFの元PCM frame byte数。欠落は0
 * @throws OperationError 欠落/不一致はDecodeFailed
 * @note major Audio/subtype FLACはCOM境界で別に確認します
 * @see koefude::validateFlacNativeFormat
 */
//------------------------------------------------------------------------------
void validateFlacNativeFormat( const FlacInputInfo &info, const std::uint32_t rate, const std::uint32_t channels,
							  const std::uint32_t bits, const std::uint32_t align)
{
	// 属性欠落と別のnative解釈を音声変換前に拒否する
	if( rate != info.rate || channels != info.channels || bits != info.bits ||
	   align != info.channels * (info.bits / 8)) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 追記前の実数を理想16kHz数の上側へ照合します
 * @param[in] info parser成功時のmetadata
 * @param[in] actual 追記後の16kHz mono sample数。途中の0を許可します
 * @throws OperationError 超過はInvalidTimeline
 * @note 16kHzは超過0、他rateは超過2sampleまで。加算overflowを避けます
 * @see koefude::validateFlacOutputUpperBound
 */
//------------------------------------------------------------------------------
void validateFlacOutputUpperBound( const FlacInputInfo &info, const std::uint64_t actual)
{
	const std::uint64_t tolerance = info.rate == K_SAMPLE_RATE ? 0 : 2 ; // 固定した丸め幅
	// 加算せず、実数が理想値を超える場合だけ安全に減算する
	if( actual > info.idealOutputSamples && actual - info.idealOutputSamples > tolerance) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 終端の実数を既知総数の理想換算へ照合します
 * @param[in] info parser成功時のmetadata
 * @param[in] actual 終端の16kHz mono実sample数
 * @throws OperationError 空PCMはDecodeFailed、数差超過はInvalidTimeline
 * @note 16kHzは完全一致、他rateは上下2sample。CRC/MD5や許容内欠落は保証しません
 * @see koefude::validateFlacOutputSamples
 */
//------------------------------------------------------------------------------
void validateFlacOutputSamples( const FlacInputInfo &info, const std::uint64_t actual)
{
	// 取消しや音声なしを短い成功PCMへ読み替えない
	if( actual == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const std::uint64_t tolerance = info.rate == K_SAMPLE_RATE ? 0 : 2 ; // 終端にも同じ条件
	const auto			ideal = info.idealOutputSamples ; // parserが求めた正の理想数
	const auto			difference = actual >= ideal ? actual - ideal : ideal - actual ; // 安全な絶対差
	// 過大/過小の両側を固定幅へ照合する
	if( difference > tolerance) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

} // namespace koefude
