//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file heaac_decode_checks.h
 * @brief HE-AACの実PCM数とsample時刻を照合する契約です
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 ADR 0271の追記前・終端・時刻条件を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 ADR 0272に従い局所時刻と期間を状態付きで照合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include "platform/heaac_format.h"
#include <cstdint>

namespace koefude {
/** @brief 一つのdecode呼出し内でworkerだけが所有する連続時刻です */
struct HeAacTimeline {
	/// 検証して追記した16kHz mono sample数
	std::uint64_t samples = 0 ;
	/// 直前の検証済みnative終端100ns
	std::int64_t  end = 0 ;
	/// 正のsample bufferを一つ以上検証した場合true
	bool		  started = false ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 追記前の16kHz mono総数を宣言ceilへ照合します
 * @param[in] info inspectM4aHeaacの成功値。同期呼出し中だけ借用します
 * @param[in] actual 追記した場合の総sample数。途中の0を許可します
 * @throws OperationError 上限超過はInputTooLarge、宣言超過はInvalidTimeline
 */
//------------------------------------------------------------------------------
void validateHeaacOutputUpperBound( const HeAacInputInfo &info, std::uint64_t actual) ;
//------------------------------------------------------------------------------
/**
 * @brief 終端総数を宣言floor～ceilへ照合します
 * @param[in] info parser成功値
 * @param[in] actual 終端の実16kHz mono総sample数
 * @throws OperationError 空はDecodeFailed、範囲外はInvalidTimeline、資源はInputTooLarge
 */
//------------------------------------------------------------------------------
void validateHeaacOutputSamples( const HeAacInputInfo &info, std::uint64_t actual) ;
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
 */
//------------------------------------------------------------------------------
void validateHeaacSampleTimeline( HeAacTimeline &timeline, std::uint64_t previous, std::uint64_t count,
								 std::int64_t timestamp, std::int64_t duration) ;
} // namespace koefude
