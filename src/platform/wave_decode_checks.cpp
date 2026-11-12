//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file wave_decode_checks.cpp
 * @brief WAVのnative属性と実sample数の不整合を拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 PCM宣言と実出力のpure照合を追加
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
#include "platform/wave_decode_checks.h"

#include <cstdint>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 検証headerとdecoderのnative PCM属性を照合します
 * @param[in] info parser成功時のmetadata。呼出し中のみ借用します
 * @param[in] rate decoderのsample周波数（Hz）
 * @param[in] channels decoderのchannel数
 * @param[in] bits decoderの1sample当たりbit数
 * @param[in] align decoderのframe byte数
 * @param[in] average decoderのbyte/秒
 * @throws OperationError 不一致ではDecodeFailed。成功/失敗とも入力を変更しません
 * @see koefude::validateWaveNativeFormat
 */
//------------------------------------------------------------------------------
void validateWaveNativeFormat( const WaveInputInfo &info, const std::uint32_t rate, const std::uint32_t channels,
							  const std::uint32_t bits, const std::uint32_t align, const std::uint32_t average)
{
	// 検証済みheaderと異なる解釈を変換器へ渡さず、rate/channel/bit/byte属性を照合する
	if( rate != info.rate || channels != info.channels || bits != 16 || align != info.channels * 2 ||
	   average != info.rate * info.channels * 2) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief WAVの理想16kHz sample数と実変換数をoverflowなしで照合します
 * @param[in] info parser成功時のmetadata。呼出し中のみ借用します
 * @param[in] actual 実出力sample数。最大差1,600 sampleを許可します
 * @throws OperationError 空PCMはDecodeFailed、差超過はInvalidTimeline
 * @note resamplerの厳密な遅延補正ではなく、部分成功を拒否する初回の停止条件です
 * @see koefude::validateWaveOutputSamples
 */
//------------------------------------------------------------------------------
void validateWaveOutputSamples( const WaveInputInfo &info, const std::uint64_t actual)
{
	// 空の実PCMを許容差の内側であっても成功にしない
	if( actual == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto ideal = info.idealOutputSamples ; // 検証した原frame数から得た理想sample数
	const auto difference = actual >= ideal ? actual - ideal : ideal - actual ; // 符号なしの絶対差
	// 先に大小を選んで減算し、過大/過小両側とuint64境界を同じ条件で拒否する
	if( difference > 1600) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
}

} // namespace koefude
