//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_decoder.cpp
 * @brief CTCのprefix探索とforward/Viterbi整列を有界に実行します
 * @version 1.0.0
 * @date 2026/09/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/11 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/11 blank経路と実反復を区別するnative decoderを追加
 * 0.1.0 Shunji Ogawa 2026/09/12 同じ音響確率へ有界な用語加点を分けて追加
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
#include "core/ctc_decoder.h"

#include <span>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

#include "core/ctc_terms.h"
#include "core/types.h"

namespace koefude {
namespace {

constexpr double	 K_IMPOSSIBLE = -std::numeric_limits<double>::infinity() ; // 到達不能の内部状態

//------------------------------------------------------------------------------
/**
 * @brief 小さい確率もunderflowで失わない二項log加算を行います
 * @param[in] first 第一のlog確率。内部の負の無限大を許可
 * @param[in] second 第二のlog確率。内部の負の無限大を許可
 * @return 二つの経路確率を加えたlog値
 */
//------------------------------------------------------------------------------
[[nodiscard]] double logAdd( const double first, const double second)
{
	// 到達不能からは確率を加えず、両方不能の場合もNaNを発生させない
	if( first == K_IMPOSSIBLE) {
		return second ;
	}
	// 第二項が不能なら第一項をそのまま保つ
	if( second == K_IMPOSSIBLE) {
		return first ;
	}
	const double greater = std::max( first, second) ; // 安定化する指数の基準
	return greater + std::log1p( std::exp( std::min( first, second) - greater)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief allocation前に行列の上限とlog確率の意味を確認します
 * @param[in] matrix 呼出し中の借用行列
 * @param[in] stop 検証中も応答する取消し
 * @throws OperationError shape/確率不正、上限超過、取消し
 */
//------------------------------------------------------------------------------
void validateMatrix( const CtcMatrixView matrix, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 個別の次元を先に制限し、後続の積がoverflowしないようにする
	if( matrix.frames > K_MAXIMUM_CTC_FRAMES || matrix.vocabulary > K_MAXIMUM_CTC_VOCABULARY) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	// 語彙内のblankと、全要素を過不足なく指すspanだけを受け付ける
	if( matrix.vocabulary < 2 || matrix.blank < 0 || static_cast<std::size_t>( matrix.blank) >= matrix.vocabulary ||
	   matrix.values.size() != matrix.frames * matrix.vocabulary) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 各frameの有限性と正規化を確認し、logitsをlog確率として扱わない
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 現在の音声frame
		checkCancelled( stop) ;
		double probability = 0 ; // 正規化済み行の確率和
		// 全語彙を一度ずつ確認し、不正値をpruningで隠さない
		for( std::size_t symbol = 0; symbol < matrix.vocabulary; ++symbol) { // 語彙内の位置
			const float score = matrix.values[frame * matrix.vocabulary + symbol] ; // 借用値
			// 正の大きい値・無限大・NaNは外部出力の異常とする
			if( !std::isfinite( score) || score > 0.001F) {
				throw OperationError( ErrorKind::INVALID_INPUT) ;
			}
			probability += std::exp( static_cast<double>( score)) ;
		}
		// float32の丸めを許容し、未正規化値や欠落した語彙を拒否する
		if( std::abs( probability - 1.0) > 0.001) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 検証済み行列へforwardとViterbiを同じ状態遷移で適用します
 * @param[in] matrix 検証済みの借用log確率
 * @param[in] tokens 元本文の非blank ID。上限と範囲は本関数で検証します
 * @param[in] stop 各frameで確認する取消し
 * @return 全経路確率と一経路の時刻。不能時はpossible=falseです
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcAlignment alignValidated( const CtcMatrixView matrix, const std::span<const std::int32_t> tokens,
										  const std::stop_token stop)
{
	// 2倍の状態列を確保する前に、token数をframe上限で制限する
	if( tokens.size() > K_MAXIMUM_CTC_FRAMES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::size_t requiredFrames = tokens.size() ; // 同じtokenの間のblankも後で加算する
	// 指定本文を検証し、連続同tokenに必要なblank frameを数える
	for( std::size_t index = 0; index < tokens.size(); ++index) { // 本文内のtoken番号
		// 存在しないIDやblank自身を本文へ含めない
		if( tokens[index] < 0 || static_cast<std::size_t>( tokens[index]) >= matrix.vocabulary ||
		   tokens[index] == matrix.blank) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// 同じラベルを別の発言として保持するには中間blankが必要になる
		if( index != 0 && tokens[index] == tokens[index - 1]) {
			++requiredFrames ;
		}
	}
	CtcAlignment result{false, K_IMPOSSIBLE, K_IMPOSSIBLE, {}} ; // 不能時もスコアを明確にする
	// 音声frameが足りない本文に架空の時刻を割り当てない
	if( requiredFrames > matrix.frames) {
		return result ;
	}
	// 空音声には空本文だけを確率1で対応付ける
	if( matrix.frames == 0) {
		result.possible = true ;
		result.logProbability = 0 ;
		result.pathLogProbability = 0 ;
		return result ;
	}
	const std::size_t		  states = tokens.size() * 2 + 1 ; // blankとtokenを交互に置く状態数
	std::vector<double>		  previousSum( states, K_IMPOSSIBLE) ; // 一つ前の全経路log確率
	std::vector<double>		  currentSum( states, K_IMPOSSIBLE) ; // 更新中の全経路log確率
	std::vector<double>		  previousBest( states, K_IMPOSSIBLE) ; // 一つ前のViterbi確率
	std::vector<double>		  currentBest( states, K_IMPOSSIBLE) ; // 更新中のViterbi確率
	std::vector<std::uint8_t> backtrace( matrix.frames * states, 0) ; // 前状態との差0/1/2
	previousSum[0] = 0 ;
	previousBest[0] = 0 ;
	// 全frameを一度ずつ処理し、状態の確率だけを二列で保持する
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 現在の音声frame
		checkCancelled( stop) ;
		// 自状態・直前・異なるtoken間のskipだけを音声順に集計する
		for( std::size_t state = 0; state < states; ++state) { // blank/token状態番号
			const auto	 symbol = state % 2 == 0 ? matrix.blank : tokens[state / 2] ; // この状態のID
			double		 total = previousSum[state] ; // 自状態への継続
			double		 best = previousBest[state] ; // 最良経路の継続
			std::uint8_t step = 0 ; // 同率なら継続、次に1遷移を優先して決定的にする
			// 直前状態から一つ進む経路を加える
			if( state > 0) {
				total = logAdd( total, previousSum[state - 1]) ;
				// より高い確率の経路だけでbacktraceを置き換える
				if( previousBest[state - 1] > best) {
					best = previousBest[state - 1] ;
					step = 1 ;
				}
			}
			// 異なるtokenの間だけblankを飛ばせる。同tokenの実反復は飛ばさない
			if( state > 1 && state % 2 == 1 && tokens[state / 2] != tokens[state / 2 - 1]) {
				total = logAdd( total, previousSum[state - 2]) ;
				// skipが最良の場合だけ二つ前の状態を記録する
				if( previousBest[state - 2] > best) {
					best = previousBest[state - 2] ;
					step = 2 ;
				}
			}
			const double emission =
				matrix.values[frame * matrix.vocabulary + static_cast<std::size_t>( symbol)] ; // 音響確率
			currentSum[state] = total + emission ;
			currentBest[state] = best + emission ;
			backtrace[frame * states + state] = step ;
		}
		previousSum.swap( currentSum) ;
		previousBest.swap( currentBest) ;
	}
	std::size_t state = states - 1 ; // 最終blankまたは最後のtokenが終端になる
	result.logProbability = previousSum[state] ;
	// 非空本文は最後のtoken上で終わる経路も正しい終端として数える
	if( !tokens.empty()) {
		result.logProbability = logAdd( result.logProbability, previousSum[state - 1]) ;
		// 最終token側が高ければ、そこから音声時刻を復元する
		if( previousBest[state - 1] > previousBest[state]) {
			--state ;
		}
	}
	result.pathLogProbability = previousBest[state] ;
	result.possible = std::isfinite( result.logProbability) ;
	// 内部でも不能な経路のbacktraceを始めない
	if( !result.possible) {
		return result ;
	}
	result.spans.assign( tokens.size(), {matrix.frames, 0}) ;
	// 最良経路を後ろからたどり、各tokenが占める音声frameを保持する
	for( std::size_t remaining = matrix.frames; remaining != 0; --remaining) { // 未復元frame数
		const std::size_t frame = remaining - 1 ; // 今回復元するframe
		// blankには発言時刻を割り当てず、token状態だけを集める
		if( state % 2 == 1) {
			auto &span = result.spans[state / 2] ; // 当該tokenの所有時刻
			span.begin = frame ;
			span.end = std::max( span.end, frame + 1) ;
		}
		state -= backtrace[frame * states + state] ;
	}
	return result ;
}

/** @brief 同じprefixのblank終端と非blank終端のlog確率です */
struct PrefixProbability {
	/// 最後のframeがblankの経路
	double		 blank = K_IMPOSSIBLE ;
	/// 最後のframeが本文末尾tokenの経路
	double		 nonblank = K_IMPOSSIBLE ;
	/// 本文が同じ全経路で共通の用語状態
	CtcTermState terms ;
} ;

/** @brief pruningの前後で所有する、一つの順序付きprefixです */
struct Prefix {
	/// blankを除いた部分本文
	std::vector<std::int32_t> tokens ;
	/// 終端別に集計した経路確率
	PrefixProbability		  probability ;
	/// 探索中の音響log確率と用語補助点の合計
	double					  rankingScore = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief prefixの確率順を同率でも決定的に比較します
 * @param[in] first 左辺の所有prefix
 * @param[in] second 右辺の所有prefix
 * @return firstを先に残す場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool prefixBefore( const Prefix &first, const Prefix &second)
{
	const double firstScore = first.rankingScore ; // 左の探索順位。確率とは区別する
	const double secondScore = second.rankingScore ; // 右の探索順位
	return firstScore != secondScore ? firstScore > secondScore : first.tokens < second.tokens ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 行列のshapeと確率を確認して、各frame最大のgreedy対照を作ります
 * @param[in] matrix 借用log確率。行ごとの確率和は1に近い値が必要です
 * @param[in] stop frame境界で確認する取消し
 * @return 最初の最大IDを優先する決定的な結果。0frameは空です
 * @throws OperationError 不正入力、上限超過、取消し。部分結果は返しません
 * @see koefude::decodeCtcGreedy
 */
//------------------------------------------------------------------------------
CtcGreedyResult decodeCtcGreedy( const CtcMatrixView matrix, const std::stop_token stop)
{
	validateMatrix( matrix, stop) ;
	CtcGreedyResult result ; // 途中で失敗した場合は呼出し元へ返さない所有結果
	std::int32_t	previous = -1 ; // 初回を必ず発火として扱う語彙外sentinel
	// 各frameの最初の最大IDを選び、従来のCTC発火位置を再現する
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 現在の音声frame
		checkCancelled( stop) ;
		// 当該frameだけの同期借用確率
		const auto values = matrix.values.subspan( frame * matrix.vocabulary, matrix.vocabulary) ;
		const auto best = std::max_element( values.begin(), values.end()) ; // 最初の最大値
		const auto symbol = static_cast<std::int32_t>( best - values.begin()) ; // 有界な語彙ID
		result.pathLogProbability += *best ;
		// blankを挟まない同IDだけをまとめ、実際の繰り返しを残す
		if( symbol != matrix.blank && symbol != previous) {
			result.tokens.push_back( symbol) ;
			result.frames.push_back( frame) ;
		}
		previous = symbol ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 指定token列へ音声frameを整列し、全経路確率も計算します
 * @param[in] matrix 借用log確率。greedyと同じ検証条件です
 * @param[in] tokens 非blank ID列。512個以内、呼出し中だけ借用します
 * @param[in] stop frame境界で確認する取消し
 * @return 整列不能はpossible=false、空のspansと負の無限大のスコアです
 * @throws OperationError 不正入力、上限超過、取消し
 * @see koefude::alignCtc
 */
//------------------------------------------------------------------------------
CtcAlignment alignCtc( const CtcMatrixView matrix, const std::span<const std::int32_t> tokens,
					  const std::stop_token stop)
{
	validateMatrix( matrix, stop) ;
	return alignValidated( matrix, tokens, stop) ;
}

namespace {

//------------------------------------------------------------------------------
/**
 * @brief 既存beamと明示用語beamで共通の音響経路を処理します
 * @param[in] matrix 借用CTC行列
 * @param[in] options 共通の探索上限
 * @param[in] terms 同じ語彙の用語context。NULLは用語なし、呼出し中だけ借用します
 * @param[in] stop 各frameと候補の取消し
 * @return 音響確率と用語加点を分けた有界候補列
 * @throws OperationError 入力・用語語彙不正、照合量上限、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<CtcHypothesis> decodeBeamWithContext( const CtcMatrixView		   matrix,
															   const CtcBeamOptions		  &options,
															   const CtcTermContext *const terms,
															   const std::stop_token	   stop)
{
	validateMatrix( matrix, stop) ;
	// 別の語彙やblankで構築した辞書を音響tokenへ適用しない
	if( terms != nullptr && (terms->vocabularySize() != matrix.vocabulary || terms->blank() != matrix.blank)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 探索幅と候補数の不正値で無制限のprefix集合を生成しない
	if( options.beamSize < 1 || options.beamSize > 128 || options.nbest < 1 || options.nbest > 8 ||
	   options.nbest > options.beamSize || options.symbolsPerFrame < 1 || options.symbolsPerFrame > 128 ||
	   !std::isfinite( options.scoreThreshold) || options.scoreThreshold < 0 || options.scoreThreshold > 100) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	std::vector<Prefix>		  beam{{{}, {0, K_IMPOSSIBLE, {}}, 0}} ; // 空prefixから確率1で開始する
	std::vector<std::int32_t> symbols( matrix.vocabulary) ; // 各frameの上位語彙を選ぶ作業列
	std::size_t				  termWork = 0 ; // 一窓で新しい本文へ追加した用語照合byte数
	// 各音声frameで拡張・統合・pruningし、過去の探索木全体は保持しない
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 現在の音声frame
		checkCancelled( stop) ;
		// 一つのframeの借用log確率
		const auto row = matrix.values.subspan( frame * matrix.vocabulary, matrix.vocabulary) ;
		std::iota( symbols.begin(), symbols.end(), 0) ;
		// 各frameで探索へ残す上位語彙数。行の語彙数を超えない
		const std::size_t retained = std::min( matrix.vocabulary, options.symbolsPerFrame) ;
		// prefixとは独立して上位語彙だけを拡張し、語彙IDで同率を固定する
		std::partial_sort( symbols.begin(), symbols.begin() + static_cast<std::ptrdiff_t>( retained), symbols.end(),
						  //------------------------------------------------------------------------------
						  /**
						   * @brief frameのスコア順です
						   * @param[in] first 左ID
						   * @param[in] second 右ID
						   * @return 左を先に残す場合true
						   */
						  //------------------------------------------------------------------------------
						  [&row]( const std::int32_t first, const std::int32_t second)
						  {
							  return row[first] != row[second] ? row[first] > row[second] : first < second ;
						  }) ;
		std::map<std::vector<std::int32_t>, PrefixProbability> next ; // 同本文へ全経路を加算
		// pruning済みprefixだけを拡張し、blankと同token継続は語彙pruningと独立に保持する
		for( const auto &prefix : beam) { // 今回拡張する所有prefix
			checkCancelled( stop) ;
			// 現在のprefixへ至る全経路の合計
			const double total = logAdd( prefix.probability.blank, prefix.probability.nonblank) ;
			auto		&same = next[prefix.tokens] ; // 本文を変えず継続する状態
			same.terms = prefix.probability.terms ;
			same.blank = logAdd( same.blank, total + row[matrix.blank]) ;
			// 同IDの非blank継続は新しい文字を増やさない
			if( !prefix.tokens.empty()) {
				same.nonblank = logAdd( same.nonblank, prefix.probability.nonblank + row[prefix.tokens.back()]) ;
			}
			// 上位語彙から非blankだけを一つ追加し、実反復にはblank終端の確率を使う
			for( std::size_t index = 0; index < retained; ++index) { // 上位語彙の順位
				const auto symbol = symbols[index] ; // 今回追加を検討する非blank ID
				// blankは既に本文を変えない経路で集計した
				if( symbol == matrix.blank) {
					continue ;
				}
				// 同じtokenを別の発火として追加する場合の区別
				const bool	 repeated = !prefix.tokens.empty() && symbol == prefix.tokens.back() ;
				const double probability = repeated ? prefix.probability.blank : total ;
				// 不能な追加経路から候補objectを作らない
				if( probability == K_IMPOSSIBLE) {
					continue ;
				}
				auto extended = prefix.tokens ; // 次frameまで所有する拡張本文
				extended.push_back( symbol) ;
				auto [entry, inserted] = next.try_emplace( std::move( extended)) ;
				// 同じ本文への全経路を集め、新規の本文にだけ用語遷移を計算する
				auto &target = entry->second ; // 所有node内の音響確率と用語状態
				// 同本文へ複数経路が合流しても加点を経路数で増やさない
				if( inserted && terms != nullptr) {
					const auto bytes = terms->pieceBytes( symbol) ; // 新たに読む本文piece長
					// token語彙数の上限と独立に、文字列照合の仕事量も制限する
					if( bytes > K_MAXIMUM_CTC_TERM_WORK - termWork) {
						throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
					}
					termWork += bytes ;
					target.terms = terms->advance( prefix.probability.terms, symbol) ;
				}
				target.nonblank = logAdd( target.nonblank, probability + row[symbol]) ;
			}
		}
		std::vector<Prefix> ranked ; // 移動して上位prefixだけを残す一時列
		ranked.reserve( next.size()) ;
		// 各本文の終端確率を一度ずつ取り出し、node handleでtoken列の再copyを避ける
		while( !next.empty()) {
			auto	   entry = next.extract( next.begin()) ; // mapから所有権を外したnode
			const auto probability = entry.mapped() ; // 同本文に集まった音響確率
			double	   ranking = logAdd( probability.blank, probability.nonblank) ; // 音響順位
			// 用語状態から計算した補助点だけを順位へ加え、音響確率は変更しない
			if( terms != nullptr) {
				const auto score = terms->score( probability.terms) ; // 完成と先読みの別加点
				ranking += score.completed + score.partial ;
			}
			ranked.push_back( {std::move( entry.key()), probability, ranking}) ;
		}
		const std::size_t count = std::min( options.beamSize, ranked.size()) ; // 次frameの上限
		std::partial_sort( ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>( count), ranked.end(),
						  prefixBefore) ;
		ranked.resize( count) ;
		const double best = ranked.front().rankingScore ; // pruningの基準となる探索順位
		// 最良候補を必ず残し、指定差より小さいprefixだけを後方から捨てる
		while( ranked.size() > 1 && ranked.back().rankingScore < best - options.scoreThreshold) {
			ranked.pop_back() ;
		}
		beam = std::move( ranked) ;
	}
	// 終端では未完成語の先読みを返し、架空の用語完成を最終候補へ残さない
	for( auto &prefix : beam) { // 完了frame後の所有prefix
		checkCancelled( stop) ;
		prefix.rankingScore = logAdd( prefix.probability.blank, prefix.probability.nonblank) ;
		// 最終候補を絞る前にも未完成分を除去する
		if( terms != nullptr) {
			prefix.rankingScore += terms->score( prefix.probability.terms).completed ;
		}
	}
	std::sort( beam.begin(), beam.end(), prefixBefore) ;
	std::vector<CtcHypothesis> result ; // 発見した本文を同じCTCで再計算する候補列
	const std::size_t		   count = std::min( options.nbest, beam.size()) ; // 保持候補は最大8
	result.reserve( count) ;
	// 最終上位候補を全経路で再採点し、時刻を持つ確率と探索近似値を分ける
	for( std::size_t index = 0; index < count; ++index) { // 最終探索候補の順位
		checkCancelled( stop) ;
		auto		&prefix = beam[index] ; // token列を移動する候補
		// pruningを含む探索時の音響スコア
		const double score = logAdd( prefix.probability.blank, prefix.probability.nonblank) ;
		auto		 alignment = alignValidated( matrix, prefix.tokens, stop) ; // 同じ本文への全経路再計算
		// 音響から独立した、完全一致した語だけの最終補助点
		const double termScore = terms == nullptr ? 0 : terms->score( prefix.probability.terms).completed ;
		const double selection = alignment.logProbability + termScore ; // 最終順位スコア
		result.push_back( {std::move( prefix.tokens), score, std::move( alignment), termScore, selection,
						  prefix.probability.terms.completed}) ;
	}
	std::sort( result.begin(), result.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 再採点順です
			   * @param[in] first 左候補
			   * @param[in] second 右候補
			   * @return 左を先に表示する場合true
			   */
			  //------------------------------------------------------------------------------
			  []( const CtcHypothesis &first, const CtcHypothesis &second)
			  {
				  return first.selectionScore != second.selectionScore ? first.selectionScore > second.selectionScore
																	   : first.tokens < second.tokens ;
			  }) ;
	return result ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief blankと非blankのprefix確率を分けて有界なN-bestを求めます
 * @param[in] matrix 借用log確率。0frameは空本文一候補になります
 * @param[in] options prefix数・語彙pruning・結果数の明示上限
 * @param[in] stop 各frameと候補処理中に確認する取消し
 * @return 発見した候補を全経路確率で再計算した降順列。同率はtoken列の辞書順です
 * @throws OperationError 不正入力、上限超過、取消し。部分結果は返しません
 * @note beamのpruningで探索しなかった本文は結果へ復元しません
 * @see koefude::decodeCtcBeam
 */
//------------------------------------------------------------------------------
std::vector<CtcHypothesis> decodeCtcBeam( const CtcMatrixView matrix, const CtcBeamOptions &options,
										 const std::stop_token stop)
{
	return decodeBeamWithContext( matrix, options, nullptr, stop) ;
}

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
 * @see koefude::decodeCtcTerms
 */
//------------------------------------------------------------------------------
std::vector<CtcHypothesis> decodeCtcTerms( const CtcMatrixView matrix, const CtcBeamOptions &options,
										  const CtcTermContext &terms, const std::stop_token stop)
{
	// 無効化時も語彙の取り違えを黙って許可しない
	if( terms.vocabularySize() != matrix.vocabulary || terms.blank() != matrix.blank) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 用語なしでは照合量の上限も追加せず、既存beamと同じ受入範囲を保つ
	if( terms.spellings().empty() || terms.boost() == 0) {
		auto result = decodeBeamWithContext( matrix, options, nullptr, stop) ; // 音響だけの候補
		// 0加点でも、完成候補の一致語IDは音響探索と独立に診断できるようにする
		for( auto &candidate : result) { // 最大8候補の所有本文
			CtcTermState state ; // 候補ごとに初期化する用語状態
			// 完成本文の最大512 tokenだけを走査し、探索中の照合量を増やさない
			for( const auto token : candidate.tokens) { // 順序付きの非blank ID
				checkCancelled( stop) ;
				state = terms.advance( state, token) ;
			}
			candidate.matchedTerms = state.completed ;
		}
		return result ;
	}
	return decodeBeamWithContext( matrix, options, &terms, stop) ;
}

} // namespace koefude
