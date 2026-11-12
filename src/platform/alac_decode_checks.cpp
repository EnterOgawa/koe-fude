//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file alac_decode_checks.cpp
 * @brief ALACの設定不一致と部分PCMの成功を拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 校正済み出力幅とcookie全fieldを照合
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include "platform/alac_decode_checks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief parserの属性とnativeの必須属性/24byte cookieを照合します
 * @param[in] info parser成功時の所有属性。同期呼出し中だけ借用します
 * @param[in] rate MFの元周波数（Hz）。欠落は0
 * @param[in] channels MFの元channel数。欠落は0
 * @param[in] bits MFの元bit深度。欠落は0
 * @param[in] cookie MF_MT_USER_DATAの借用byte列。空を許可し欠落として拒否します
 * @throws OperationError 属性/cookieの欠落・不一致はDecodeFailed
 * @note Audio major/full ALAC GUIDはCOM境界で別に検査します
 * @see koefude::validateAlacNativeFormat
 */
//------------------------------------------------------------------------------
void validateAlacNativeFormat( const AlacInputInfo &info, const std::uint32_t rate, const std::uint32_t channels,
							  const std::uint32_t bits, const std::span<const std::byte> cookie)
{
	// 圧縮ALACでPCM alignmentを仮定せず、必須属性と正確なcookie長を要求する
	if( rate != info.rate || channels != info.channels || bits != info.bits || cookie.size() != 24) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// 検証済みALAC headerから得た、native decoder属性との照合用の十一項目
	const std::array<std::uint32_t, 11> expected{
		info.frameLength, info.compatibleVersion, info.bits,		   info.pb,	 info.mb, info.kb, info.channels,
		info.maxRun,	  info.maxFrameBytes,	  info.averageBitRate, info.rate} ; // parserが確認した全fieldの所有値
	constexpr std::array<std::size_t, 11> K_WIDTHS{4, 1, 1, 1, 1, 1, 1, 2, 4, 4, 4} ; // cookie幅
	std::size_t							  offset = 0 ; // 固定24byte内の位置
	// 全fieldをbig endianで復元し、hintを含め同じcookieを確認する
	for( std::size_t index = 0; index < expected.size(); ++index) { // 現在のfield番号
		std::uint32_t value = 0 ; // 最大4byteの復元値
		// 現fieldの幅まで読み、借用pointerを呼出し外へ保持しない
		for( std::size_t byte = 0; byte < K_WIDTHS[index]; ++byte) { // field内のbyte番号
			value = (value << 8) | std::to_integer<std::uint32_t>( cookie[offset++]) ;
		}
		// 別設定や欠落を推定で救済せず、変換開始前に拒否する
		if( value != expected[index]) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 宣言frame数から16kHz monoの理想ceilを求めます
 * @param[in] info parser成功時のfloor/正のrate/元総frame数
 * @return 16kHz mono sample数の切上げ値
 * @throws OperationError 整数時刻の余裕を含む上限超過はInputTooLarge
 * @pre infoはinspectM4aAlacの成功値です
 * @see koefude::alacOutputCeiling
 */
//------------------------------------------------------------------------------
std::uint64_t alacOutputCeiling( const AlacInputInfo &info)
{
	const std::uint64_t rounding =
		(info.totalSamples % info.rate) * K_SAMPLE_RATE % info.rate == 0 ? 0 : 1 ; // ceilの余り
	// 時刻の余裕とfloor+1の表現を加算前に確認する
	if( info.idealOutputSamples > K_MAXIMUM_TIMELINE_SAMPLES - rounding) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	return info.idealOutputSamples + rounding ;
}

//------------------------------------------------------------------------------
/**
 * @brief 追記前に実PCM数を宣言の上側へ照合します
 * @param[in] info parser成功時の属性
 * @param[in] actual 追記した場合の16kHz mono総sample数。途中の0を許可します
 * @throws OperationError 超過はInvalidTimeline、整数資源上限はInputTooLarge
 * @note 16kHzは超過0、他rateは理想ceilから超過2sampleまでです
 * @see koefude::validateAlacOutputUpperBound
 */
//------------------------------------------------------------------------------
void validateAlacOutputUpperBound( const AlacInputInfo &info, const std::uint64_t actual)
{
	const auto			ideal = alacOutputCeiling( info) ; // parserのfloorをceilへ補正した数
	const std::uint64_t tolerance = info.rate == K_SAMPLE_RATE ? 0 : 2 ; // ALACの固定校正幅
	// 上側だけを安全に減算し、過大数をPCMへ追記しない
	if( actual > ideal && actual - ideal > tolerance) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 終端の実PCM総数を宣言へ照合します
 * @param[in] info parser成功時の属性
 * @param[in] actual 終端の16kHz mono実sample数
 * @throws OperationError 空PCMはDecodeFailed、数差超過はInvalidTimeline
 * @note 16kHzは完全一致、他rateは理想ceilから上下2sampleです
 * @see koefude::validateAlacOutputSamples
 */
//------------------------------------------------------------------------------
void validateAlacOutputSamples( const AlacInputInfo &info, const std::uint64_t actual)
{
	// 音声なしや早い空終端を有効PCMとして返さない
	if( actual == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto			ideal = alacOutputCeiling( info) ; // 元宣言の切上げ数
	const auto			difference = actual >= ideal ? actual - ideal : ideal - actual ; // 安全な絶対差
	const std::uint64_t tolerance = info.rate == K_SAMPLE_RATE ? 0 : 2 ; // 終端にも同じ固定幅
	// 過小側も判定し、MFの部分成功を結果へ返さない
	if( difference > tolerance) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

} // namespace koefude
