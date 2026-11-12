//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_decoder.h
 * @brief 有界なCTC出力の探索と音声由来の時刻合わせを定義します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 greedy対照、prefix beam、全経路確率と整列を追加
 * 0.1.0 Shunji Ogawa 2026/09/12 用語加点を音響確率と分けた探索契約を追加
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
#include <stop_token>
#include <vector>

namespace koefude {

class CtcTermContext ; // 同じ固定語彙で構築した用語の所有context

constexpr std::size_t K_MAXIMUM_CTC_FRAMES = 512 ; // 一度に探索・整列するframe数の上限
constexpr std::size_t K_MAXIMUM_CTC_VOCABULARY = 8192 ; // blankを含む語彙数の上限

/** @brief 呼出し中だけ借用する、正規化済みCTC log probability行列です */
struct CtcMatrixView {
	/// frame優先でframes×vocabulary個。NaN/無限大は不可
	std::span<const float> values ;
	/// paddingを除いた有効frame数。0を許可
	std::size_t			   frames = 0 ;
	/// 各frameの語彙数。2～8192
	std::size_t			   vocabulary = 0 ;
	/// 語彙内のblank ID
	std::int32_t		   blank = 0 ;
} ;

/** @brief greedyの本文を構成するIDと、その最初の発火位置です */
struct CtcGreedyResult {
	/// blankと隣接重複を除いた順序付きID
	std::vector<std::int32_t> tokens ;
	/// 各tokenの相対発火frame。tokensと同数
	std::vector<std::size_t>  frames ;
	/// frameごとの最大値を選んだ一経路のlog確率
	double					  pathLogProbability = 0 ;
} ;

/** @brief 一つのtokenを最良CTC経路で割り当てた半開frame区間です */
struct CtcFrameSpan {
	/// token状態へ初めて入るframe
	std::size_t begin = 0 ;
	/// token状態へ最後に入ったframeの次
	std::size_t end = 0 ;
} ;

/** @brief 指定token列の全経路確率と、最良の音声時刻です */
struct CtcAlignment {
	/// falseなら時刻なし。整列不能を架空の位置で埋めない
	bool					  possible = false ;
	/// 全ての有効CTC経路を合算したlog確率
	double					  logProbability = 0 ;
	/// 時刻に使う最良一経路のlog確率
	double					  pathLogProbability = 0 ;
	/// possibleなら指定token列と同数。空列を許可
	std::vector<CtcFrameSpan> spans ;
} ;

/** @brief 探索の資源と結果数を明示的に制限する設定です */
struct CtcBeamOptions {
	/// 各frameで残すprefix数。1～128
	std::size_t beamSize = 32 ;
	/// 最終候補数。1～8かつbeam_size以下
	std::size_t nbest = 8 ;
	/// 拡張する上位語彙数。1～128。blankは別途保持
	std::size_t symbolsPerFrame = 64 ;
	/// 最高prefixとのlog確率差の上限。0～100
	double		scoreThreshold = 25 ;
} ;

/** @brief 同じCTC行列から得た候補と、その再計算した音響確率です */
struct CtcHypothesis {
	/// 同じ語彙の順序付き非blank ID
	std::vector<std::int32_t> tokens ;
	/// pruningを伴うprefix探索中のlog確率
	double					  searchLogProbability = 0 ;
	/// pruningに依存せず再計算した全経路確率と時刻
	CtcAlignment			  alignment ;
	/// 完成した用語への加点。音響確率ではなく、全体で8以下
	double					  termScore = 0 ;
	/// 再計算音響log確率＋term_scoreによる最終順位
	double					  selectionScore = 0 ;
	/// 一度以上完成した用語ID集合。用語なしは0
	std::uint64_t			  matchedTerms = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 行列のshapeと確率を確認して、各frame最大のgreedy対照を作ります
 * @param[in] matrix 借用log確率。行ごとの確率和は1に近い値が必要です
 * @param[in] stop frame境界で確認する取消し
 * @return 最初の最大IDを優先する決定的な結果。0frameは空です
 * @throws OperationError 不正入力、上限超過、取消し。部分結果は返しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcGreedyResult			 decodeCtcGreedy( CtcMatrixView matrix, std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 指定token列へ音声frameを整列し、全経路確率も計算します
 * @param[in] matrix 借用log確率。greedyと同じ検証条件です
 * @param[in] tokens 非blank ID列。512個以内、呼出し中だけ借用します
 * @param[in] stop frame境界で確認する取消し
 * @return 整列不能はpossible=false、空のspansと負の無限大のスコアです
 * @throws OperationError 不正入力、上限超過、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcAlignment				 alignCtc( CtcMatrixView matrix, std::span<const std::int32_t> tokens,
												  std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief blankと非blankのprefix確率を分けて有界なN-bestを求めます
 * @param[in] matrix 借用log確率。0frameは空本文一候補になります
 * @param[in] options prefix数・語彙pruning・結果数の明示上限
 * @param[in] stop 各frameと候補処理中に確認する取消し
 * @return 発見した候補を全経路確率で再計算した降順列。同率はtoken列の辞書順です
 * @throws OperationError 不正入力、上限超過、取消し。部分結果は返しません
 * @note beamのpruningで探索しなかった本文は結果へ復元しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<CtcHypothesis> decodeCtcBeam( CtcMatrixView matrix, const CtcBeamOptions &options,
													   std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 音響確率を保持し、明示用語の補助点を加えて同じbeamを探索します
 * @param[in] matrix 借用log確率。用語contextと同じ語彙数・blankが必要です
 * @param[in] options 用語なしと共通の探索幅・語彙pruning・候補数
 * @param[in] terms 呼出し中だけ借用するimmutable用語context
 * @param[in] stop 各frame・候補処理中に確認する取消し
 * @return 同じCTCで再計算した音響確率＋完成用語加点の降順列。未完成加点は残しません
 * @throws OperationError 不正入力、語彙不一致、照合量上限、取消し
 * @note 空用語またはboost 0では既存beamの本文・音響確率・時刻・順位を維持します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<CtcHypothesis> decodeCtcTerms( CtcMatrixView matrix, const CtcBeamOptions &options,
														const CtcTermContext &terms, std::stop_token stop = {}) ;

} // namespace koefude
