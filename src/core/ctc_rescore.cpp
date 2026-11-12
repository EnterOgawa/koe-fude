//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_rescore.cpp
 * @brief LMの確率と長さ補正から固定CTC候補の順位を計算します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 全候補の適用可否と無効化時の厳密な原順位を検証
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
#include "core/ctc_rescore.h"

#include <span>
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/types.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 有限で有界な自然対数確率だけを受け付けます
 * @param[in] value 外部計算から受け取る合計確率
 * @return 数値誤差を含む固定範囲内ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool validLogProbability( const double value) noexcept
{
	return std::isfinite( value) && value >= -1e9 && value <= 1e-6 ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 音響候補を増減せず、LMの結合スコアから元indexの順位を求めます
 * @param[in] candidates CTC確率の降順に並ぶ1～8候補。呼出し中だけ借用します
 * @param[in] options λとκ。λ=0ならLM値を参照せず元順位を保持します
 * @param[in] stop 呼出しの前後で確認する取消し
 * @return 所有する順位と元index順スコア。一候補でもLM適用外なら全体の元順位を保持
 * @throws OperationError 不正件数、非有限値、範囲外、元順位の不整合、取消し
 * @note 本文・時刻は引数に取らず、生成・置換・補完を行いません。失敗時は部分結果なし
 * @see koefude::rescoreCtcCandidates
 */
//------------------------------------------------------------------------------
CtcRescoreResult rescoreCtcCandidates( const std::span<const CtcRescoreInput> candidates,
									  const CtcRescoreOptions &options, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 無制限の候補やNaNの条件で順位を決めず、呼出し側の契約違反として拒否する
	if( candidates.empty() || candidates.size() > 8 || !std::isfinite( options.lmWeight) || options.lmWeight < 0 ||
	   options.lmWeight > 1 || !std::isfinite( options.tokenReward) || options.tokenReward < 0 ||
	   options.tokenReward > 6) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	CtcRescoreResult result ; // 失敗時に呼出し側へ渡さない所有結果
	result.order.reserve( candidates.size()) ;
	result.scores.reserve( candidates.size()) ;
	double previous = std::numeric_limits<double>::infinity() ; // 原候補の降順を検証する前値
	bool   allAvailable = true ; // 一候補だけを除いて選択を偏らせない全体判定
	// 元indexと音響確率を保存し、候補の欠落・順序違いを先に検出する
	for( std::size_t index = 0; index < candidates.size(); ++index) { // 原候補の順位
		const auto &candidate = candidates[index] ; // 同期呼出し中だけ借用する一候補
		// 既存beamとは異なる不正確率や順序を、LMで覆い隠さない
		if( !validLogProbability( candidate.ctcLogProbability) || candidate.ctcLogProbability > previous) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		previous = candidate.ctcLogProbability ;
		result.order.push_back( index) ;
		result.scores.push_back( candidate.ctcLogProbability) ;
		// 無効化したLMの値を検証や順位へ使わず、元候補だけで成立させる
		if( options.lmWeight == 0) {
			continue ;
		}
		// token化の適用外を全体の不適用として保持する
		if( !candidate.languageModel) {
			allAvailable = false ;
			continue ;
		}
		const auto &lm = *candidate.languageModel ; // 検証対象の言語確率と予測数
		// NaN・過大な確率・EOSを数えない入力を適用外という正常状態にしない
		if( !validLogProbability( lm.logProbability) || lm.predictedTokens == 0 || lm.predictedTokens > 513) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	// 一つでも対応不能なら全候補の原順位を保持し、本文を増減させない
	if( options.lmWeight == 0 || !allAvailable) {
		checkCancelled( stop) ;
		return result ;
	}
	// 検証済みの全候補だけに同じλとκを適用し、元index順へスコアを保存する
	for( std::size_t index = 0; index < candidates.size(); ++index) { // 採点する元候補番号
		const auto &lm = *candidates[index].languageModel ; // 全体検証済みの言語確率
		result.scores[index] +=
			options.lmWeight * (lm.logProbability + options.tokenReward * static_cast<double>( lm.predictedTokens)) ;
	}
	std::stable_sort( result.order.begin(), result.order.end(),
					 //------------------------------------------------------------------------------
					 /**
					  * @brief 同率の元順序を維持して高いスコアを先へ置きます
					  * @param[in] left 左候補の元index
					  * @param[in] right 右候補の元index
					  * @return 左候補の方が高ければtrue
					  */
					 //------------------------------------------------------------------------------
					 [&]( const std::size_t left, const std::size_t right)
					 {
						 return result.scores[left] > result.scores[right] ;
					 }) ;
	result.applied = true ;
	checkCancelled( stop) ;
	return result ;
}

} // namespace koefude
