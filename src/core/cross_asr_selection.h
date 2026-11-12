//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cross_asr_selection.h
 * @brief 共通CTC採点とLMから元ASR候補のindexを保守的に選びます
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 適用外・僅差・重要表記の保持規則を追加
 * 0.1.0 Shunji Ogawa 2026/09/13 固定LM提案の二者択一と重要表記保護を追加
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
#include <string_view>
#include <vector>

#include "core/ctc_rescore.h"

namespace koefude {

/** @brief 本文を所有せず、同じ窓の原候補と共通採点を借用します */
struct CrossAsrCandidate {
	/// 4096byte以内の元UTF-8。呼出し中だけ有効
	std::string_view				  text ;
	/// 全token化の共通CTC log確率。適用外は空
	std::optional<double>			  acoustic ;
	/// 同じ固定LMによる本文+EOSの確率
	std::optional<LanguageModelScore> languageModel ;
} ;

/** @brief 調整集合で固定し、評価集合へそのまま適用する規則です */
struct CrossAsrOptions {
	/// 共通CTC確率へ加えるLM確率のλ。0～1
	double lmWeight = 0 ;
	/// greedyを上回る必要のあるスコア差。0～100
	double margin = 0 ;
	/// 数文字または固定否定表現の変更窓を保持する
	bool   protectCritical = true ;
} ;

/** @brief 選択と元本文保持の理由を区別します */
enum class CrossAsrReason {
	/// marginを超えて元候補を選択した
	SELECTED,
	/// 元greedyが同率を含め最高だった
	GREEDY_BEST,
	/// 一候補でも共通音響採点の適用外
	ACOUSTIC_UNAVAILABLE,
	/// λ>0で一候補でもLMの適用外
	LM_UNAVAILABLE,
	/// スコア差がmargin以下
	WITHIN_MARGIN,
	/// 数値または否定の表記を含む本文変更を保留した
	CRITICAL_TEXT,
} ;

/** @brief 元候補のindexと選択理由を所有し、本文や時刻を生成しません */
struct CrossAsrSelection {
	/// 戻り値の元index。保持時はgreedyの0
	std::size_t			selected = 0 ;
	/// 保護とmargin適用前の元index
	std::size_t			proposed = 0 ;
	/// 決定の理由
	CrossAsrReason		reason = CrossAsrReason::GREEDY_BEST ;
	/// 元index順のスコア。適用外がある場合は空
	std::vector<double> scores ;
} ;

/** @brief ADR 0050のCで元greedyかLM提案を選ぶ理由です */
enum class LmGuardReason {
	/// 本文がbyte単位で同じなので元greedyを保持する
	UNCHANGED,
	/// どちらかに固定重要表記がある変更窓を保持する
	CRITICAL_TEXT,
	/// 固定重要表記のない窓で元LM提案を選ぶ
	PROPOSED,
} ;

/** @brief 元segmentを選ぶ値だけを所有し、本文や時刻を複製しません */
struct LmGuardSelection {
	/// trueはLM提案、falseは元greedy全体を選ぶ
	bool		  useProposed = false ;
	/// 二者択一の理由
	LmGuardReason reason = LmGuardReason::UNCHANGED ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定LM提案へ重要表記保護だけを適用します
 * @param[in] original 元greedyのUTF-8本文。4096byte以下、呼出し中だけ借用
 * @param[in] proposed 元LM提案のUTF-8本文。同じ容量・寿命。空本文も許可
 * @param[in] stop 検証・選択前後の協調取消し
 * @return 元greedyまたは提案の選択と理由。正解・token・時刻を参照しません
 * @throws OperationError 不正UTF-8・容量・制御文字・取消し。部分結果なし
 * @note 正規化・短縮保護・第2候補探索は行いません。状態を持たず失敗後も再使用できます
 */
//------------------------------------------------------------------------------
[[nodiscard]] LmGuardSelection	guardLmCandidate( std::string_view original, std::string_view proposed,
												 std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 原候補を変更せず、固定規則でgreedy保持または元indexを返します
 * @param[in] candidates 1～10個。index 0はgreedy、その後はbeam順、別ASR候補順
 * @param[in] options 同率は元index順。λ=0ではLM値を参照しません
 * @param[in] stop 選択処理前後の取消し
 * @return 元候補index、全候補の共通スコアと保持理由
 * @throws OperationError 不正件数・本文・確率・設定・取消し。部分結果なし
 * @note 重要表記は固定文字/substringによる検査であり意味理解ではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] CrossAsrSelection selectCrossAsr( std::span<const CrossAsrCandidate> candidates,
											   const CrossAsrOptions &options, std::stop_token stop = {}) ;

} // namespace koefude
