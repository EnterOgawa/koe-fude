//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_word_spotter.h
 * @brief 明示用語の局所CTC探索と原tokenを保持する比較契約を定義します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 N-bestへ依存しない用語探索と局所置換を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/ctc_decoder.h"

namespace koefude {

/** @brief 音響上の候補と本文へ採用しない理由を区別します */
enum class CtcSpotReason {
	/// 条件を満たした局所置換
	APPLIED,
	/// 用語加点込みの差がしきい値以下
	WITHIN_MARGIN,
	/// 同じ本文を元greedyが既に持つ
	ALREADY_PRESENT,
	/// 置換する元の非blank tokenがない
	EMPTY_INTERVAL,
	/// 用語の区間が元tokenの途中を切る
	CUTS_TOKEN,
	/// 数文字または固定否定表現を含む変更
	CRITICAL_TEXT,
	/// 先に採用した候補と重なる
	OVERLAP,
} ;

/** @brief 一つの完成語の候補を所有します。時刻は窓内のframeです */
struct CtcWordSpot {
	/// 明示入力の用語ID
	std::size_t	  term = 0 ;
	/// 初回非blankから完成した最終非blankまでの半開区間
	CtcFrameSpan  span ;
	/// 用語の最良経路から同区間のgreedy経路を引いた0以下の差
	double		  acousticDifference = 0 ;
	/// apply後の採否。detectでは未判定
	CtcSpotReason reason = CtcSpotReason::WITHIN_MARGIN ;
} ;

/** @brief 選定集合で固定する局所判定値です */
struct CtcSpotOptions {
	/// 一つの完成語への加点。有限な0～8
	double boost = 0 ;
	/// 必要な厳密なスコア差。有限な0～8
	double margin = 0 ;
} ;

/** @brief 元tokenを残しながら作った比較用の本文列と全候補です */
struct CtcSpotResult {
	/// 最大512個の非blank ID。製品結果へ自動適用しない
	std::vector<std::int32_t> tokens ;
	/// tokensと同数の発火frame。変更範囲外は元値
	std::vector<std::size_t>  frames ;
	/// 重複除去後の256候補以下。元用語IDと判断を保持
	std::vector<CtcWordSpot>  spots ;
} ;

/**
 * @brief 同じworkerで使うimmutableな用語graphと固定語彙を所有します
 * @note 行列は同期呼出し中だけ借用します。contextへ音声や過去窓の状態を残しません
 */
class CtcWordSpotter final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 全表記token化のgraphを構築し、未知語・状態容量を先に検証します
	 * @param[in] pieces 2～8192個、各256byte以内のUTF-8本文piece。内部へcopyします
	 * @param[in] blank 固定語彙のblank ID
	 * @param[in] terms 最大64語、各192byte、合計4096byte。空集合は無効化です
	 * @param[in] stop graph構築の取消し
	 * @throws OperationError 不正入力、表現不能、上限超過、取消し。部分contextなし
	 */
	//------------------------------------------------------------------------------
	CtcWordSpotter( std::span<const std::string> pieces, std::int32_t blank, std::span<const std::string> terms,
				   std::stop_token stop = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有語彙とgraphを解放します
	 */
	//------------------------------------------------------------------------------
	~CtcWordSpotter() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 大きなgraphの複製を禁止します
	 * @param[in] other 複製元
	 */
	//------------------------------------------------------------------------------
	CtcWordSpotter( const CtcWordSpotter &other) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有状態の代入を禁止します
	 * @param[in] other 代入元
	 * @return 利用不可
	 */
	//------------------------------------------------------------------------------
	CtcWordSpotter						  &operator=( const CtcWordSpotter &other) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 任意frameで始まる完全な用語を、全語彙出力から検出します
	 * @param[in] matrix 同じ語彙・blankの有限で正規化済みlog確率。最大512frame
	 * @param[in] stop frame境界の取消し
	 * @return 同じ用語の重なる検出を除いた最大256件。本文採否は未判定です
	 * @throws OperationError 入力・作業量・候補数の上限超過、取消し。部分結果なし
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::vector<CtcWordSpot> detect( CtcMatrixView matrix, std::stop_token stop = {}) const ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 音響差・元token区間・重要表記から比較用の局所置換を作ります
	 * @param[in] matrix detectと同じ契約の借用行列
	 * @param[in] options 有限な加点とmargin。加点0は元本文と時刻を完全保持します
	 * @param[in] stop 探索・再整列・置換の取消し
	 * @return 元時刻を保持した所有token列と候補の採否。採用は最大16件
	 * @throws OperationError 不正設定・容量超過・再整列矛盾・取消し。部分結果なし
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] CtcSpotResult apply( CtcMatrixView matrix, CtcSpotOptions options, std::stop_token stop = {}) const ;

	private:
	class Impl ; // graph状態と借用しない語彙の実装
	/// worker専有のimmutable context
	std::unique_ptr<Impl> implementation_ ;
} ;

} // namespace koefude
