//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file alac_decode_checks.h
 * @brief ALACのnative cookieと実PCM総数を照合する契約です
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 ADR 0258の属性と出力停止条件を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "platform/alac_format.h"

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
 */
//------------------------------------------------------------------------------
void validateAlacNativeFormat( const AlacInputInfo &info, std::uint32_t rate, std::uint32_t channels, std::uint32_t bits,
							  std::span<const std::byte> cookie) ;

//------------------------------------------------------------------------------
/**
 * @brief 宣言frame数から16kHz monoの理想ceilを求めます
 * @param[in] info parser成功時のfloor/正のrate/元総frame数
 * @return 16kHz mono sample数の切上げ値
 * @throws OperationError 整数時刻の余裕を含む上限超過はInputTooLarge
 * @pre infoはinspectM4aAlacの成功値です
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t alacOutputCeiling( const AlacInputInfo &info) ;

//------------------------------------------------------------------------------
/**
 * @brief 追記前に実PCM数を宣言の上側へ照合します
 * @param[in] info parser成功時の属性
 * @param[in] actual 追記した場合の16kHz mono総sample数。途中の0を許可します
 * @throws OperationError 超過はInvalidTimeline、整数資源上限はInputTooLarge
 * @note 16kHzは超過0、他rateは理想ceilから超過2sampleまでです
 */
//------------------------------------------------------------------------------
void						validateAlacOutputUpperBound( const AlacInputInfo &info, std::uint64_t actual) ;

//------------------------------------------------------------------------------
/**
 * @brief 終端の実PCM総数を宣言へ照合します
 * @param[in] info parser成功時の属性
 * @param[in] actual 終端の16kHz mono実sample数
 * @throws OperationError 空PCMはDecodeFailed、数差超過はInvalidTimeline
 * @note 16kHzは完全一致、他rateは理想ceilから上下2sampleです
 */
//------------------------------------------------------------------------------
void						validateAlacOutputSamples( const AlacInputInfo &info, std::uint64_t actual) ;

} // namespace koefude
