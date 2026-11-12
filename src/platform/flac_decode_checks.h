//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file flac_decode_checks.h
 * @brief FLACのnative属性と実PCM数の照合契約を定義します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 既知総数に基づく有界な停止条件を追加
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

#include "platform/flac_format.h"

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
 */
//------------------------------------------------------------------------------
void validateFlacNativeFormat( const FlacInputInfo &info, std::uint32_t rate, std::uint32_t channels, std::uint32_t bits,
							  std::uint32_t align) ;

//------------------------------------------------------------------------------
/**
 * @brief 追記前の実数を理想16kHz数の上側へ照合します
 * @param[in] info parser成功時のmetadata
 * @param[in] actual 追記後の16kHz mono sample数。途中の0を許可します
 * @throws OperationError 超過はInvalidTimeline
 * @note 16kHzは超過0、他rateは超過2sampleまで。加算overflowを避けます
 */
//------------------------------------------------------------------------------
void validateFlacOutputUpperBound( const FlacInputInfo &info, std::uint64_t actual) ;

//------------------------------------------------------------------------------
/**
 * @brief 終端の実数を既知総数の理想換算へ照合します
 * @param[in] info parser成功時のmetadata
 * @param[in] actual 終端の16kHz mono実sample数
 * @throws OperationError 空PCMはDecodeFailed、数差超過はInvalidTimeline
 * @note 16kHzは完全一致、他rateは上下2sample。CRC/MD5や許容内欠落は保証しません
 */
//------------------------------------------------------------------------------
void validateFlacOutputSamples( const FlacInputInfo &info, std::uint64_t actual) ;

} // namespace koefude
