//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file wave_decode_checks.h
 * @brief WAVの宣言属性と実decoder出力の照合を宣言します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 COMに依存しないnative属性/出力数の照合を追加
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

#include "platform/wave_format.h"

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
 */
//------------------------------------------------------------------------------
void validateWaveNativeFormat( const WaveInputInfo &info, std::uint32_t rate, std::uint32_t channels, std::uint32_t bits,
							  std::uint32_t align, std::uint32_t average) ;

//------------------------------------------------------------------------------
/**
 * @brief WAVの理想16kHz sample数と実変換数をoverflowなしで照合します
 * @param[in] info parser成功時のmetadata。呼出し中のみ借用します
 * @param[in] actual 実出力sample数。最大差1,600 sampleを許可します
 * @throws OperationError 空PCMはDecodeFailed、差超過はInvalidTimeline
 * @note resamplerの厳密な遅延補正ではなく、部分成功を拒否する初回の停止条件です
 */
//------------------------------------------------------------------------------
void validateWaveOutputSamples( const WaveInputInfo &info, std::uint64_t actual) ;

} // namespace koefude
