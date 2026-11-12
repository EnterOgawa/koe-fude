//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file cross_asr_selection.cpp
 * @brief 元indexを維持する候補選択と事前固定した本文保護を実装します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 共通スコア、原候補保持、数文字と否定substringを固定
 * 0.1.0 Shunji Ogawa 2026/09/13 同じ重要表記定義で固定LM提案を保護
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/cross_asr_selection.h"

#include <span>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>

#include "core/ctc_text_alignment.h"
#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

// 数字と日本語の数文字。一般語に含まれる場合も保守的に窓全体を保持する
constexpr std::wstring_view K_NUMBER_CHARACTERS =
	L"0123456789０１２３４５６７８９〇零一二三四五六七八九十百千万億兆壱弐参" ;
constexpr std::array<std::string_view, 9> K_NEGATIONS{"ない",	"なく", "なかっ", "ません", "ずに",
													  "いいえ", "否定", "禁止",	  "できぬ"} ;
// 調整結果を見る前に固定する否定substring。意味の網羅を保証しない

//------------------------------------------------------------------------------
/**
 * @brief 確率の値域を検証します
 * @param[in] value 自然対数確率
 * @return 有限で固定範囲内ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool						  validProbability( const double value)
{
	return std::isfinite( value) && value >= -1e9 && value <= 1e-6 ;
}

//------------------------------------------------------------------------------
/**
 * @brief 事前固定した重要表記の有無を検査します
 * @param[in] text 検証済み原本文
 * @return 数文字または固定否定substringを含む場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool containsCritical( const std::string_view text)
{
	const auto wide = toWide( text) ; // UTF-8の途中byteを数文字として誤検出しない表記
	// 一つでも数文字があれば他の単語の意味を推測せず保護する
	if( wide.find_first_of( K_NUMBER_CHARACTERS) != std::wstring::npos) {
		return true ;
	}
	// 固定した否定表現を全部調べ、正解依存の例外を加えない
	for( const auto negation : K_NEGATIONS) { // 固定substringの一つ
		// 否定表記を含む窓では元本文を保守的に保持する
		if( text.find( negation) != std::string_view::npos) {
			return true ;
		}
	}
	return false ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 固定LM提案へ重要表記保護だけを適用します
 * @param[in] original 元greedyのUTF-8本文。4096byte以下、呼出し中だけ借用
 * @param[in] proposed 元LM提案のUTF-8本文。同じ容量・寿命。空本文も許可
 * @param[in] stop 検証・選択前後の協調取消し
 * @return 元greedyまたは提案の選択と理由。正解・token・時刻を参照しません
 * @throws OperationError 不正UTF-8・容量・制御文字・取消し。部分結果なし
 * @note 正規化・短縮保護・第2候補探索は行いません。状態を持たず失敗後も再使用できます
 * @see koefude::guardLmCandidate
 */
//------------------------------------------------------------------------------
LmGuardSelection guardLmCandidate( const std::string_view original, const std::string_view proposed,
								  const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 両本文を同じ上限とUTF-8契約で検査し、返された比較表記は選択へ使わない
	for( const auto text : {original, proposed}) { // 呼出し中だけ借用する元本文
		static_cast<void>( normalizeCrossAsrText( text)) ;
		// 外側のtabも通常本文として許可せず、Python Cと同じ契約へ揃える
		for( const unsigned char byte : text) { // ASCII制御文字を検出するbyte
			// C1とUnicode行区切りは上のUTF-8検証で拒否済み
			if( byte < 32) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
		}
	}
	LmGuardSelection result ; // 同文なら元greedyを保持する初期値
	// byte単位で変わる窓だけを固定表記の有無で判定する
	if( original != proposed) {
		// 片側だけに現れる重要表記も窓全体の保持理由にする
		if( containsCritical( original) || containsCritical( proposed)) {
			result.reason = LmGuardReason::CRITICAL_TEXT ;
		} else {
			result.useProposed = true ;
			result.reason = LmGuardReason::PROPOSED ;
		}
	}
	checkCancelled( stop) ;
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 原候補を変更せず、固定規則でgreedy保持または元indexを返します
 * @param[in] candidates 1～10個。index 0はgreedy、その後はbeam順、別ASR候補順
 * @param[in] options 同率は元index順。λ=0ではLM値を参照しません
 * @param[in] stop 選択処理前後の取消し
 * @return 元候補index、全候補の共通スコアと保持理由
 * @throws OperationError 不正件数・本文・確率・設定・取消し。部分結果なし
 * @note 重要表記は固定文字/substringによる検査であり意味理解ではありません
 * @see koefude::selectCrossAsr
 */
//------------------------------------------------------------------------------
CrossAsrSelection selectCrossAsr( const std::span<const CrossAsrCandidate> candidates, const CrossAsrOptions &options,
								 const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 件数・重み・僅差条件を結果の偶然に依存せず拒否する
	if( candidates.empty() || candidates.size() > 10 || !std::isfinite( options.lmWeight) || options.lmWeight < 0 ||
	   options.lmWeight > 1 || !std::isfinite( options.margin) || options.margin < 0 || options.margin > 100) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	bool acousticAvailable = true ; // 全候補への音響適用可否
	bool lmAvailable = true ; // 有効なLMの全候補への適用可否
	// 全体保持の場合も他候補の不正数値を正常な適用外に隠さない
	for( const auto &candidate : candidates) { // 原本文と採点値を借用する一候補
		static_cast<void>( normalizeCrossAsrText( candidate.text)) ;
		// 音響適用外は窓全体で保持するが、有限でない値は入力エラーとする
		if( !candidate.acoustic) {
			acousticAvailable = false ;
			// 提供された音響値が不正なら適用外とは区別して拒否する
		} else if( !validProbability( *candidate.acoustic)) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// 無効にしたLMの値を検査にも選択にも用いない
		if( options.lmWeight == 0) {
			continue ;
		}
		// token化不能と不正な推論値を異なる状態で処理する
		if( !candidate.languageModel) {
			lmAvailable = false ;
			// 提供されたLM値の不正確率や予測数を適用外として隠さない
		} else if( !validProbability( candidate.languageModel->logProbability) ||
				  candidate.languageModel->predictedTokens == 0 || candidate.languageModel->predictedTokens > 513) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	CrossAsrSelection result ; // 元index 0を保持する初期結果
	// 一候補の音響適用外でも候補を除外せず、窓全体を保持する
	if( !acousticAvailable) {
		result.reason = CrossAsrReason::ACOUSTIC_UNAVAILABLE ;
		checkCancelled( stop) ;
		return result ;
	}
	// λ>0ではLMを全候補に同じ条件で使える場合だけ比較する
	if( !lmAvailable) {
		result.reason = CrossAsrReason::LM_UNAVAILABLE ;
		checkCancelled( stop) ;
		return result ;
	}
	// 原index順にスコアを計算し、厳密に高い場合だけ最良候補を更新する
	for( std::size_t index = 0; index < candidates.size(); ++index) { // 原候補番号
		double score = *candidates[index].acoustic ; // 同じ表記CTCの音響確率
		// 長さ補正を加えず、明示したλだけでLM確率を合成する
		if( options.lmWeight > 0) {
			score += options.lmWeight * candidates[index].languageModel->logProbability ;
		}
		result.scores.push_back( score) ;
		// 同率はgreedyと元beamの順序を維持する
		if( score > result.scores[result.proposed]) {
			result.proposed = index ;
		}
	}
	// greedyが最高なら保持理由をそのまま返す
	if( result.proposed == 0) {
		result.reason = CrossAsrReason::GREEDY_BEST ;
		// 元greedyとの差が僅かなら変更の根拠不足として保持する
	} else if( result.scores[result.proposed] - result.scores[0] <= options.margin) {
		result.reason = CrossAsrReason::WITHIN_MARGIN ;
		// 原文か変更候補に重要表記がある窓を丸ごと保護する
	} else if( options.protectCritical && candidates[0].text != candidates[result.proposed].text &&
			  (containsCritical( candidates[0].text) || containsCritical( candidates[result.proposed].text))) {
		result.reason = CrossAsrReason::CRITICAL_TEXT ;
		// 音響・LM・margin・保護の全条件を満たす原候補だけを選択する
	} else {
		result.selected = result.proposed ;
		result.reason = CrossAsrReason::SELECTED ;
	}
	checkCancelled( stop) ;
	return result ;
}

} // namespace koefude
