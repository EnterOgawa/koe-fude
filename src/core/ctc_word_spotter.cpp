//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_word_spotter.cpp
 * @brief 全token化の局所CTC差と元token境界から用語候補を比較します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 有界graph・任意開始探索・保守的な局所置換を追加
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
#include "core/ctc_word_spotter.h"

#include <span>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>

#include "core/ctc_terms.h"
#include "core/ctc_text_alignment.h"
#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr double	  K_IMPOSSIBLE = -std::numeric_limits<double>::infinity() ; // 到達不能な経路
constexpr std::size_t K_MAXIMUM_STATES = 32768 ; // 全用語graphの状態数上限
constexpr std::size_t K_MAXIMUM_EDGES = 200000 ; // 全用語graphの遷移数上限
constexpr std::size_t K_MAXIMUM_WORK = 80000000 ; // graph構築と一窓探索それぞれの作業上限
constexpr std::size_t K_MAXIMUM_RAW_SPOTS = 65536 ; // 重複除去前の所有候補上限
constexpr std::size_t K_MAXIMUM_SPOTS = 256 ; // 比較出力の候補上限
constexpr std::size_t K_MAXIMUM_APPLIED = 16 ; // 一窓で変更する区間数の上限

/** @brief 同じ表記prefixを一piece進めるarcです */
struct Arc {
	/// 元UTF-8の開始byte
	std::size_t	 begin = 0 ;
	/// 元UTF-8の終了byte
	std::size_t	 end = 0 ;
	/// 出力する非blank ID
	std::int32_t token = 0 ;
} ;

/** @brief 一つの正確な表記を構成するCTC状態graphです */
struct Graph {
	/// 利用者の元用語ID
	std::size_t							  term = 0 ;
	/// 各状態が出力する語彙ID
	std::vector<std::int32_t>			  labels ;
	/// 入力元状態。0は毎frameの新規開始
	std::vector<std::vector<std::size_t>> previous ;
	/// 完成した最後の非blank状態
	std::vector<std::size_t>			  terminals ;
} ;

/** @brief 状態と計算量をallocation前に制限する集計です */
struct Budget {
	/// 構築済み状態数
	std::size_t states = 0 ;
	/// 構築済み遷移数
	std::size_t edges = 0 ;
	/// 表記照合のbyte量
	std::size_t work = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief graph容量の超過を明示失敗にします
 * @param[in] budget 現在の集計
 */
//------------------------------------------------------------------------------
void checkBudget( const Budget &budget)
{
	// 切詰めによる黙示的な語の脱落を禁止する
	if( budget.states > K_MAXIMUM_STATES || budget.edges > K_MAXIMUM_EDGES || budget.work > K_MAXIMUM_WORK) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 全ての正確なpiece連結をCTC遷移へ展開します
 * @param[in] pieces 検証済みの語彙
 * @param[in] blank 非本文のblank ID
 * @param[in] text 正確に連結する表記。先頭空白付きも別graphとして扱います
 * @param[in] term 元用語ID
 * @param[in,out] budget 全graphの有界集計
 * @param[in] stop 構築中の取消し
 * @return 所有graph。表現不能の先頭空白variantはterminalが空です
 */
//------------------------------------------------------------------------------
[[nodiscard]] Graph makeGraph( const std::span<const std::string> pieces, const std::int32_t blank,
							  const std::string_view text, const std::size_t term, Budget &budget,
							  const std::stop_token stop)
{
	const std::size_t					  positions = text.size() + 1 ; // blank位置を含むprefix数
	std::vector<Arc>					  arcs ; // 本文へ一致した非blank arc
	std::vector<std::vector<std::size_t>> incoming( positions) ; // prefixへ入るarc状態
	std::vector<bool>					  reachable( positions, false) ; // token連結の到達可否
	reachable[0] = true ;
	budget.states += positions ;
	checkBudget( budget) ;
	// 到達できるprefixだけを拡張し、不正なUTF-8途中位置を作らない
	for( std::size_t begin = 0; begin < text.size(); ++begin) { // 現在の本文byte位置
		checkCancelled( stop) ;
		// 先行pieceで到達できない位置から候補を生成しない
		if( !reachable[begin]) {
			continue ;
		}
		// 全語彙を調べ、上位N-bestの有無を探索条件にしない
		for( std::size_t token = 0; token < pieces.size(); ++token) { // 元の語彙ID
			const auto &piece = pieces[token] ; // 同期構築中の借用表記
			// blank、予約、残りより長いpieceは本文へ使用しない
			if( static_cast<std::int32_t>( token) == blank || piece.front() == '<' ||
			   piece.size() > text.size() - begin) {
				continue ;
			}
			budget.work += piece.size() ;
			checkBudget( budget) ;
			// 正確に同じ表記へ進む全てのtoken化を保持する
			if( text.substr( begin, piece.size()) == piece) {
				++budget.states ;
				checkBudget( budget) ;
				const std::size_t end = begin + piece.size() ; // 一致pieceの終端
				incoming[end].push_back( positions + arcs.size()) ;
				arcs.push_back( {begin, end, static_cast<std::int32_t>( token)}) ;
				reachable[end] = true ;
			}
		}
	}
	Graph graph ; // 入力spanから独立した所有graph
	graph.term = term ;
	graph.labels.assign( positions + arcs.size(), blank) ;
	graph.previous.resize( graph.labels.size()) ;
	graph.terminals = incoming.back() ;
	// root blankを作らず、新規経路は最初の非blank frameから始める
	for( std::size_t position = 1; position < positions; ++position) { // 本文prefix位置
		graph.previous[position] = incoming[position] ;
		graph.previous[position].push_back( position) ;
		budget.edges += graph.previous[position].size() ;
		checkBudget( budget) ;
	}
	// 継続、blank後の同ID再発火、異なるIDへの直行を区別する
	for( std::size_t index = 0; index < arcs.size(); ++index) { // 非blank arc番号
		checkCancelled( stop) ;
		const auto		 &arc = arcs[index] ; // 現在の出力piece
		const std::size_t state = positions + index ; // 出力先のCTC状態
		graph.labels[state] = arc.token ;
		graph.previous[state] = {arc.begin, state} ;
		budget.edges += 2 ;
		checkBudget( budget) ;
		// 直前prefixを完成した異なるIDからだけblankなしで移動する
		for( const std::size_t source : incoming[arc.begin]) { // 直前非blankの状態
			// 同じIDの二度目の発火にはblankを要求する
			if( graph.labels[source] != arc.token) {
				++budget.edges ;
				checkBudget( budget) ;
				graph.previous[state].push_back( source) ;
			}
		}
		std::sort( graph.previous[state].begin(), graph.previous[state].end()) ;
	}
	return graph ;
}

//------------------------------------------------------------------------------
/**
 * @brief 競合候補の決定的な優先順です
 * @param[in] left 第一候補
 * @param[in] right 第二候補
 * @return 音響差、短い区間、用語ID、開始順で第一が優先ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool preferred( const CtcWordSpot &left, const CtcWordSpot &right)
{
	return std::tuple{-left.acousticDifference, left.span.end - left.span.begin, left.term, left.span.begin} <
		   std::tuple{-right.acousticDifference, right.span.end - right.span.begin, right.term, right.span.begin} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 半開区間の交差を調べます
 * @param[in] left 第一区間
 * @param[in] right 第二区間
 * @return 一frame以上交差すればtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool overlaps( const CtcFrameSpan left, const CtcFrameSpan right)
{
	return left.begin < right.end && right.begin < left.end ;
}

//------------------------------------------------------------------------------
/**
 * @brief token表記を連結します
 * @param[in] tokens 非blank列
 * @param[in] pieces 検証済み語彙
 * @return 正確な本文。先頭空白も保持します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string surface( const std::span<const std::int32_t> tokens, const std::span<const std::string> pieces)
{
	std::string result ; // 所有する局所本文
	// 元ID順で表記を結合し、読みや正規化による置換を加えない
	for( const std::int32_t token : tokens) { // 現在の非blank ID
		result += pieces[static_cast<std::size_t>( token)] ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 変更範囲の重要表記を保護します
 * @param[in] text 局所UTF-8本文
 * @return 数文字・固定否定substring・予約表記を含めばtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool critical( const std::string_view text)
{
	// ADR 0040と同じ表記集合を、今回変更する局所範囲へ適用する
	constexpr std::wstring_view K_NUMBERS = L"0123456789０１２３４５６７８９〇零一二三四五六七八九十百千万億兆壱弐参" ;
	constexpr std::array<std::string_view, 9> K_NEGATIONS{"ない",	"なく", "なかっ", "ません", "ずに",
														  "いいえ", "否定", "禁止",	  "できぬ"} ;
	// 意味を推測せず予約表記と数文字を保護する
	if( toWide( text).find_first_of( K_NUMBERS) != std::wstring::npos ||
	   text.find_first_of( "<>") != std::string_view::npos) {
		return true ;
	}
	// 表記集合を全て確認し、評価本文に応じた例外を作らない
	for( const auto value : K_NEGATIONS) { // 固定した否定substring
		// 否定に一致する範囲の変更を保留する
		if( text.find( value) != std::string_view::npos) {
			return true ;
		}
	}
	return false ;
}

} // namespace

/** @brief 外部の語彙・用語の寿命へ依存しない探索状態です */
class CtcWordSpotter::Impl {
	public:
	/// 固定語彙の所有copy
	std::vector<std::string> pieces ;
	/// 元順序の明示用語
	std::vector<std::string> terms ;
	/// 語彙の固定blank ID
	std::int32_t			 blank = 0 ;
	/// 完全表記と先頭空白variant
	std::vector<Graph>		 graphs ;
	/// frame数と掛け合わせる構築済み容量
	Budget					 budget ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 全表記token化のgraphを構築し、未知語・状態容量を先に検証します
 * @param[in] pieces 2～8192個、各256byte以内のUTF-8本文piece。内部へcopyします
 * @param[in] blank 固定語彙のblank ID
 * @param[in] terms 最大64語、各192byte、合計4096byte。空集合は無効化です
 * @param[in] stop graph構築の取消し
 * @throws OperationError 不正入力、表現不能、上限超過、取消し。部分contextなし
 * @see koefude::CtcWordSpotter::CtcWordSpotter(std::span< const std::string >, std::int32_t, std::span< const std::string >, std::stop_token)
 */
//------------------------------------------------------------------------------
CtcWordSpotter::CtcWordSpotter( const std::span<const std::string> pieces, const std::int32_t blank,
							   const std::span<const std::string> terms, const std::stop_token stop)
	: implementation_( std::make_unique<Impl>())
{
	const CtcTermContext validation( pieces, blank, terms, 0, stop) ; // 共通の外部入力境界
	auto				&value = *implementation_ ; // 構築中だけ変更する所有状態
	value.pieces.assign( pieces.begin(), pieces.end()) ;
	value.terms.assign( terms.begin(), terms.end()) ;
	value.blank = blank ;
	// 全ての明示語を同じ固定語彙で構成する
	for( std::size_t term = 0; term < terms.size(); ++term) { // 元用語ID
		value.graphs.push_back( makeGraph( pieces, blank, terms[term], term, value.budget, stop)) ;
		value.graphs.push_back( makeGraph( pieces, blank, " " + terms[term], term, value.budget, stop)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 所有語彙とgraphを解放します
 * @see koefude::CtcWordSpotter::~CtcWordSpotter
 */
//------------------------------------------------------------------------------
CtcWordSpotter::~CtcWordSpotter() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 任意frameで始まる完全な用語を、全語彙出力から検出します
 * @param[in] matrix 同じ語彙・blankの有限で正規化済みlog確率。最大512frame
 * @param[in] stop frame境界の取消し
 * @return 同じ用語の重なる検出を除いた最大256件。本文採否は未判定です
 * @throws OperationError 入力・作業量・候補数の上限超過、取消し。部分結果なし
 * @see koefude::CtcWordSpotter::detect
 */
//------------------------------------------------------------------------------
std::vector<CtcWordSpot> CtcWordSpotter::detect( const CtcMatrixView matrix, const std::stop_token stop) const
{
	const auto &value = *implementation_ ; // 同期探索中のimmutable状態
	// 同じ件数でもblankが異なる行列を混同しない
	if( matrix.vocabulary != value.pieces.size() || matrix.blank != value.blank) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	static_cast<void>( decodeCtcGreedy( matrix, stop)) ;
	// 掛算前に除算して有界な作業量を確認する
	if( value.budget.edges > K_MAXIMUM_WORK / std::max( std::size_t{1}, matrix.frames)) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::vector<double> maximum( matrix.frames) ; // 各frameのgreedy log確率
	// 同じ区間の最大経路との差を使い、長い負値の累積で開始位置を偏らせない
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 元frame番号
		// 検証済み一行の借用確率
		const auto row = matrix.values.subspan( frame * matrix.vocabulary, matrix.vocabulary) ;
		maximum[frame] = *std::max_element( row.begin(), row.end()) ;
	}
	std::vector<CtcWordSpot> raw ; // 有界な完成候補だけを保存する
	// 別graphの状態を共有せず、同音異表記を暗黙に同一視しない
	for( const auto &graph : value.graphs) { // 一つの正確な表記graph
		checkCancelled( stop) ;
		// 表現不能な先頭空白variantだけは探索する終端を持たない
		if( graph.terminals.empty()) {
			continue ;
		}
		std::vector<double>		 best( graph.labels.size(), K_IMPOSSIBLE) ; // 前frameの最良差
		std::vector<double>		 next( best) ; // 次frameへの出力領域
		std::vector<std::size_t> starts( best.size()) ; // 最良経路の開始frame
		std::vector<std::size_t> nextStarts( starts) ; // 次frameの開始位置
		// 全frameを順に進め、各frameで新しい用語開始を許可する
		for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 現在のframe番号
			checkCancelled( stop) ;
			// 同じframeの全語彙出力
			const auto row = matrix.values.subspan( frame * matrix.vocabulary, matrix.vocabulary) ;
			best[0] = 0 ;
			starts[0] = frame ;
			// 直前frameだけから状態を更新し、同frameの二重発火を防ぐ
			for( std::size_t state = 1; state < best.size(); ++state) { // 更新先状態
				double			  score = K_IMPOSSIBLE ; // まだ到達する経路なし
				std::size_t		  begin = 0 ; // 同率時は遅い開始を優先する
				// この状態の出力語彙ID
				const std::size_t token = static_cast<std::size_t>( graph.labels[state]) ;
				// 到達可能な前状態を全て調べる
				for( const std::size_t source : graph.previous[state]) { // 前状態ID
					// 新規開始だけはblank優勢frameと極小tokenを飛ばす
					if( source == 0 &&
					   (row[static_cast<std::size_t>( matrix.blank)] > std::log( 0.8) || row[token] < std::log( 0.001))) {
						continue ;
					}
					// 差が同じなら短い区間、さらに同率なら状態ID順を保持する
					if( best[source] > score || (best[source] == score && starts[source] > begin)) {
						score = best[source] ;
						begin = starts[source] ;
					}
				}
				next[state] = score + static_cast<double>( row[token]) - maximum[frame] ;
				nextStarts[state] = begin ;
			}
			best.swap( next) ;
			starts.swap( nextStarts) ;
			CtcWordSpot spot{graph.term, {0, frame + 1}, K_IMPOSSIBLE} ; // この終端の最良候補
			// 完全な表記で終わる非blankだけを検出する
			for( const std::size_t terminal : graph.terminals) { // 完成状態の番号
				const CtcWordSpot candidate{
					graph.term, {starts[terminal], frame + 1}, best[terminal]} ; // 同じ終端時刻の一候補
				// 表記の別token化を重複した検出として保存しない
				if( preferred( candidate, spot)) {
					spot = candidate ;
				}
			}
			// 最大加点8でもmarginを超えない候補はどの条件でも採用しない
			if( spot.acousticDifference > -8) {
				// 候補を黙って落とさず、所有量超過を明示する
				if( raw.size() == K_MAXIMUM_RAW_SPOTS) {
					throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
				}
				raw.push_back( spot) ;
			}
		}
	}
	std::sort( raw.begin(), raw.end(), preferred) ;
	std::vector<CtcWordSpot> result ; // 同語の重複を除いた有限候補
	// 高い音響差から採用し、同じ語の競合区間だけを重複として落とす
	for( const auto &spot : raw) { // 現在の音響候補
		checkCancelled( stop) ;
		bool duplicate = false ; // 同じ語・交差区間の有無
		// 残した候補との交差だけを調べ、離れた再登場を削除しない
		for( const auto &previous : result) { // 先に保持した候補
			duplicate = duplicate || (previous.term == spot.term && overlaps( previous.span, spot.span)) ;
		}
		// 重複候補を別の用語出現として数えない
		if( duplicate) {
			continue ;
		}
		// 候補数超過では全文保持に見せかけず比較を失敗させる
		if( result.size() == K_MAXIMUM_SPOTS) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		result.push_back( spot) ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 音響差・元token区間・重要表記から比較用の局所置換を作ります
 * @param[in] matrix detectと同じ契約の借用行列
 * @param[in] options 有限な加点とmargin。加点0は元本文と時刻を完全保持します
 * @param[in] stop 探索・再整列・置換の取消し
 * @return 元時刻を保持した所有token列と候補の採否。採用は最大16件
 * @throws OperationError 不正設定・容量超過・再整列矛盾・取消し。部分結果なし
 * @see koefude::CtcWordSpotter::apply
 */
//------------------------------------------------------------------------------
CtcSpotResult CtcWordSpotter::apply( const CtcMatrixView matrix, const CtcSpotOptions options,
									const std::stop_token stop) const
{
	// 不正な値を無効化と解釈しない
	if( !std::isfinite( options.boost) || !std::isfinite( options.margin) || options.boost < 0 || options.boost > 8 ||
	   options.margin < 0 || options.margin > 8) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto				 &value = *implementation_ ; // 検証済みの所有語彙と用語
	const auto				  greedy = decodeCtcGreedy( matrix, stop) ; // 元対照のtokenと発火frame
	CtcSpotResult			  result{greedy.tokens, greedy.frames, detect( matrix, stop)} ; // 最初は元本文
	std::vector<CtcFrameSpan> spans ; // greedy最大経路での非blank滞在区間
	// 各tokenの連続滞在末尾を、別のforced alignmentを使わず元経路から取得する
	for( std::size_t index = 0; index < greedy.tokens.size(); ++index) { // 元token番号
		std::size_t end = greedy.frames[index] + 1 ; // 発火frameの次から継続を調べる
		// 同じ最大IDが連続する間だけ区間を伸ばし、blank後の反復を別tokenにする
		while( end < matrix.frames) {
			// 現在の末尾frameの借用確率
			const auto row = matrix.values.subspan( end * matrix.vocabulary, matrix.vocabulary) ;
			const auto token = std::distance( row.begin(), std::max_element( row.begin(), row.end())) ;
			// 元greedyと同じ最初の最大ID
			// blankまたは異なるIDに移ればこの発火の区間を確定する
			if( token != greedy.tokens[index]) {
				break ;
			}
			++end ;
		}
		spans.push_back( {greedy.frames[index], end}) ;
	}
	/** @brief 確定した非交差の置換tokenと元の添字範囲です */
	struct Replacement {
		std::size_t				  begin = 0 ; // 元tokenの置換先頭index
		std::size_t				  end = 0 ; // 元tokenの置換終端index
		std::vector<std::int32_t> tokens ; // 同じ局所CTCへ整列した新しいtoken
		std::vector<std::size_t>  frames ; // 窓基準へ戻した発火frame
	} ;
	std::vector<Replacement> replacements ; // 最大16個の所有置換
	// 音響差の高い順で判定し、原候補も保持する
	for( auto &spot : result.spots) { // 採否を記録する候補
		checkCancelled( stop) ;
		// 加点0と僅差では元の本文と時刻を保持する
		if( options.boost == 0 || spot.acousticDifference + options.boost <= options.margin) {
			continue ;
		}
		std::size_t begin = spans.size() ; // 交差した最初の元token
		std::size_t end = 0 ; // 交差した最後の元tokenの次
		// 全元tokenを確認し、時刻だけから存在しない文字位置を作らない
		for( std::size_t index = 0; index < spans.size(); ++index) { // 元token番号
			// 一frame以上交差するtokenだけを置換対象に含める
			if( overlaps( spans[index], spot.span)) {
				begin = std::min( begin, index) ;
				end = index + 1 ;
			}
		}
		// 元の非blankがない場所への新規本文挿入を禁止する
		if( begin == spans.size()) {
			spot.reason = CtcSpotReason::EMPTY_INTERVAL ;
			continue ;
		}
		// 元tokenの一部だけを削る局所置換を禁止する
		if( spans[begin].begin < spot.span.begin || spans[end - 1].end > spot.span.end) {
			spot.reason = CtcSpotReason::CUTS_TOKEN ;
			continue ;
		}
		const std::string	   original = surface( std::span( greedy.tokens).subspan( begin, end - begin),
												  value.pieces) ; // 正確な元局所本文
		// 最初のASCII空白だけを表記の対応から外す
		const std::string_view comparable = original.starts_with( ' ') ? std::string_view( original).substr( 1) : original ;
		// 既に同じ語なら時刻やtoken化を作り直さない
		if( comparable == value.terms[spot.term]) {
			spot.reason = CtcSpotReason::ALREADY_PRESENT ;
			continue ;
		}
		// 重要表記を含む変更範囲は推定で上書きしない
		if( critical( original) || critical( value.terms[spot.term])) {
			spot.reason = CtcSpotReason::CRITICAL_TEXT ;
			continue ;
		}
		bool conflict = false ; // 先に採用した元token範囲との交差
		// 採用順と時刻順を分け、同じtokenを二度書換えない
		for( const auto &replacement : replacements) { // 既に確定した置換
			conflict = conflict || (begin < replacement.end && replacement.begin < end) ;
		}
		// 競合は音響差が高い既採用候補を保持する
		if( conflict) {
			spot.reason = CtcSpotReason::OVERLAP ;
			continue ;
		}
		// 元と同じ行列の有限な局所借用範囲
		const CtcMatrixView	 local{matrix.values.subspan( spot.span.begin * matrix.vocabulary,
														 (spot.span.end - spot.span.begin) * matrix.vocabulary),
								   spot.span.end - spot.span.begin, matrix.vocabulary, matrix.blank} ;
		const CtcTextAligner aligner( local, value.pieces, stop) ; // 全token化の独立した再整列
		// 採用する用語の最良tokenと局所時刻
		const auto			 alignment = aligner.alignLiteral( value.terms[spot.term], true, stop) ;
		// 再整列できない候補へ架空の時刻を割当てない
		if( alignment.status != CtcTextStatus::READY) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		double greedyProbability = 0 ; // 局所区間の元greedy経路確率
		// 採用経路の差が検出値と一致することを独立に再計算する
		for( std::size_t frame = 0; frame < local.frames; ++frame) { // 局所frame番号
			// 同じframeの借用確率
			const auto row = local.values.subspan( frame * local.vocabulary, local.vocabulary) ;
			greedyProbability += *std::max_element( row.begin(), row.end()) ;
		}
		// 再整列で空白端が選ばれた候補は元tokenの境界を証明できないため保留する
		if( alignment.spans.empty() || alignment.spans.front().begin != 0 ||
		   alignment.spans.back().end != local.frames ||
		   std::abs( alignment.pathLogProbability - greedyProbability - spot.acousticDifference) > 1e-5) {
			spot.reason = CtcSpotReason::CUTS_TOKEN ;
			continue ;
		}
		// 採用量を黙って切詰めず、比較の容量違反を明示する
		if( replacements.size() == K_MAXIMUM_APPLIED) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		Replacement replacement{begin, end, alignment.tokens, {}} ; // 所有する局所置換
		// 同じ局所音声から得た時刻を元窓基準へ戻す
		for( const auto span : alignment.spans) { // 用語内tokenの非blank区間
			replacement.frames.push_back( spot.span.begin + span.begin) ;
		}
		replacements.push_back( std::move( replacement)) ;
		spot.reason = CtcSpotReason::APPLIED ;
	}
	std::sort( replacements.begin(), replacements.end(),
			  //------------------------------------------------------------------------------
			  /**
			   * @brief 置換を元token順へ戻します
			   * @param[in] left 第一置換
			   * @param[in] right 第二置換
			   * @return 第一が前ならtrue
			   */
			  //------------------------------------------------------------------------------
			  []( const Replacement &left, const Replacement &right)
			  {
				  return left.begin < right.begin ;
			  }) ;
	result.tokens.clear() ;
	result.frames.clear() ;
	std::size_t cursor = 0 ; // 元greedy列の未出力先頭
	// 非交差の置換だけを反映し、それ以外の元token/時刻をcopyする
	for( const auto &replacement : replacements) { // 時刻順の局所置換
		// この置換の前まで元の本文をそのまま保持する
		while( cursor < replacement.begin) {
			result.tokens.push_back( greedy.tokens[cursor]) ;
			result.frames.push_back( greedy.frames[cursor++]) ;
		}
		result.tokens.insert( result.tokens.end(), replacement.tokens.begin(), replacement.tokens.end()) ;
		result.frames.insert( result.frames.end(), replacement.frames.begin(), replacement.frames.end()) ;
		cursor = replacement.end ;
	}
	// 最後の置換以降も元の本文と時刻を完全に保持する
	while( cursor < greedy.tokens.size()) {
		result.tokens.push_back( greedy.tokens[cursor]) ;
		result.frames.push_back( greedy.frames[cursor++]) ;
	}
	// 全体のtoken容量を確定前に検証する
	if( result.tokens.size() > K_MAXIMUM_CTC_FRAMES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	checkCancelled( stop) ;
	return result ;
}

} // namespace koefude
