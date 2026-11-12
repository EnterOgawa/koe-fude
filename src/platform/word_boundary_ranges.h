//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file word_boundary_ranges.h
 * @brief 単語範囲の取得元と独立に、製品と同じUTF-16/UTF-8対応を検証します
 * @version 1.0.0
 * @date 2026/11/11
 * @author Shunji Ogawa
 *
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/11 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1416の同期借用range入口を追加
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 製品の公開APIではない内部seamです。OS言語判定・辞書・保存・通信を代行しません
 * @see LICENSE
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>

#include "platform/word_boundaries.h"

namespace koefude::word_boundary_detail {

/** @brief 一語が占める元本文のUTF-16符号単位範囲を保持します */
struct WordRange {
	/// 元UTF-16本文先頭からの開始符号単位数。surrogate途中は共有処理が拒否します
	std::uint32_t start ;
	/// 開始位置からの符号単位数。0も許し、本文終端を超える値は拒否します
	std::uint32_t length ;
} ;

/**
 * @brief 指定indexの一語だけを同期取得する借用callbackの型です
 * @param[in,out] context 呼出し全体で有効な非null借用状態。所有を移さず、呼出し後に保持しません
 * @param[in] index 0から指定語数未満の語番号。共有処理が昇順で一度ずつ要求します
 * @param[out] range 成功時の元UTF-16開始/長さ。失敗時は使用されず、事前値の保持を要求しません
 * @param[in,out] diagnostic 言語/語数を保持し、取得段階/HRESULTは更新可能です。失敗時は失敗段階/HRESULTを書きます
 * @return 一range取得成功ならtrue。false時は共有処理が停止し、全tokenの分割許可をfalseに保ちます
 * @note callbackは本文/tokenを変更せず、借用外の共有状態を使いません。range列の所有・copyは不要です
 */
using WordRangeReader = bool (*)( void *context, std::uint32_t index, WordRange &range,
								 WordBoundaryDiagnostic &diagnostic) ;

//------------------------------------------------------------------------------
/**
 * @brief 固定の語数と同期readerから、nativeと同じ処理でtokenの分割許可を設定します
 * @param[in,out] segment 呼出し中に排他所有するUTF-8本文とtoken。本文・時刻は変更しません
 * 容量検証後に全許可を解除し、全rangeの検証成功後だけ許可します。容量超過時はtokenを変更しません
 * @param[in] wordCount 元UTF-16符号単位数以下の語数。超過はWORD_COUNTでfalse、0語は全許可falseで成功です
 * @param[in] reader indexを一rangeへ変換する借用callback。読取りが必要な時は非nullとします
 * @param[in,out] context reader専用の借用状態。読取り時は非nullで、同期呼出し全体の寿命を要します
 * @param[out] diagnostic 開始時に初期化する固定診断。OS言語はUNOBSERVEDのままです
 * 空本文/tokenはCOMPLETE/語数0、範囲不正はWORD_RANGE、reader失敗はreaderの段階/HRESULTを保持します
 * @return 全range対応成功または空本文/tokenならtrue。位置・語数・reader失敗は全許可falseでfalseです
 * @throws OperationError 容量上限超過または不正UTF-8。容量超過前はtoken非変更、不正UTF-8時は全許可falseです
 * @note 内部試験入口です。同期中だけ借用し、呼出し後に保持しません。COM・OS辞書・I/O・通信を使いません
 * 追加記憶量は本文長に有界で、語数に比例するrange copyを作りません。readerの例外はそのまま伝播します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool markWordBoundariesFromRanges( TextSegment &segment, std::uint32_t wordCount, WordRangeReader reader,
												void *context, WordBoundaryDiagnostic &diagnostic) ;

} // namespace koefude::word_boundary_detail
