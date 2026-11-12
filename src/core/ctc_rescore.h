//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_rescore.h
 * @brief 既存CTC候補の本文と時刻を変更せず、LMによる選択順位を返します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 有界な候補再採点と適用外時の原順位保持を追加
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
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace koefude {

/** @brief BOSの後に本文とEOSを予測した一候補の言語確率です */
struct LanguageModelScore {
	/// 自然対数確率の合計。有限で[-1e9, 1e-6]
	double		logProbability = 0 ;
	/// 本文token数とEOSの1個。1～513
	std::size_t predictedTokens = 0 ;
} ;

/** @brief 元候補を複写せず、再採点に必要なスコアだけを借用する値です */
struct CtcRescoreInput {
	/// 元CTC全経路の自然対数確率。[-1e9, 1e-6]
	double							  ctcLogProbability = 0 ;
	/// 空ならLMの適用外。失敗とは区別
	std::optional<LanguageModelScore> languageModel ;
} ;

/** @brief ADR 0038のスコア結合に用いる明示的な比較条件です */
struct CtcRescoreOptions {
	/// LM確率と長さ補正に掛けるλ。有限な0～1
	double lmWeight = 0 ;
	/// 予測token一つに加えるκ。有限な0～6
	double tokenReward = 0 ;
} ;

/** @brief 元indexとスコアを所有し、元本文・時刻の所有権を移さない選択結果です */
struct CtcRescoreResult {
	/// 最上位からの元候補index。同率は元順序
	std::vector<std::size_t> order ;
	/// 元候補index順の選択スコア。orderとは別に対応する
	std::vector<double>		 scores ;
	/// λ>0かつ全候補へLMを適用できた場合true。順位変化とは別
	bool					 applied = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 音響候補を増減せず、LMの結合スコアから元indexの順位を求めます
 * @param[in] candidates CTC確率の降順に並ぶ1～8候補。呼出し中だけ借用します
 * @param[in] options λとκ。λ=0ならLM値を参照せず元順位を保持します
 * @param[in] stop 呼出しの前後で確認する取消し
 * @return 所有する順位と元index順スコア。一候補でもLM適用外なら全体の元順位を保持
 * @throws OperationError 不正件数、非有限値、範囲外、元順位の不整合、取消し
 * @note 本文・時刻は引数に取らず、生成・置換・補完を行いません。失敗時は部分結果なし
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcRescoreResult rescoreCtcCandidates( std::span<const CtcRescoreInput> candidates,
													const CtcRescoreOptions &options, std::stop_token stop = {}) ;

} // namespace koefude
