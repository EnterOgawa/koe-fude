//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_text_alignment.cpp
 * @brief 本文prefix graphでCTC経路和とViterbiの音声時刻を計算します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 全token化の重複しない経路和と有界tracebackを追加
 * 0.1.0 Shunji Ogawa 2026/09/12 明示用語を正規化せず整列する呼出しを追加
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
#include "core/ctc_text_alignment.h"

#include <span>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr double		K_IMPOSSIBLE = -std::numeric_limits<double>::infinity() ; // 到達不能のlog確率
constexpr std::size_t	K_MAXIMUM_STATES = 8192 ; // blank位置と非blank arcの合計上限
constexpr std::size_t	K_MAXIMUM_WORK = 80'000'000 ; // 一本文の比較byte・frame遷移の上限
constexpr std::size_t	K_MAXIMUM_EDGES = 200'000 ; // graphの所有遷移数の上限
constexpr std::size_t	K_MAXIMUM_TRACE_BYTES = 32 * 1024 * 1024 ; // tracebackのbyte上限
constexpr std::uint32_t K_NO_STATE = std::numeric_limits<std::uint32_t>::max() ; // 前状態なし

/** @brief 一つのpieceで本文prefixを進める有向arcです */
struct TextArc {
	/// 本文UTF-8の開始byte
	std::size_t	 begin = 0 ;
	/// 本文UTF-8の終了byte
	std::size_t	 end = 0 ;
	/// 連結する非blank ID
	std::int32_t token = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 安定なlog確率和です
 * @param[in] left 第一確率
 * @param[in] right 第二確率
 * @return 負の無限大を許可したlog(exp(left)+exp(right))
 */
//------------------------------------------------------------------------------
[[nodiscard]] double logAdd( const double left, const double right)
{
	// 片側が到達不能なら他方だけを保持し、無限大同士を減算しない
	if( left == K_IMPOSSIBLE) {
		return right ;
	}
	// 右側だけが到達不能でも同じ定義を維持する
	if( right == K_IMPOSSIBLE) {
		return left ;
	}
	return std::max( left, right) + std::log1p( std::exp( -std::abs( left - right))) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 適用外の空結果を作ります
 * @param[in] status 保持する理由
 * @return 架空の確率や時刻を持たない所有結果
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcTextAlignment unavailable( const CtcTextStatus status)
{
	return {status, K_IMPOSSIBLE, K_IMPOSSIBLE, {}, {}} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 空白除去を含まない一つの正確なCTC出力を採点します
 * @param[in] matrix 検証済みの借用行列
 * @param[in] pieces 検証済みの借用語彙
 * @param[in] text 正確に連結するUTF-8。先頭空白を含む場合もそのまま扱います
 * @param[in] stop 計算境界の取消し
 * @return 全経路の合算と最良一経路、または明示した適用外
 */
//------------------------------------------------------------------------------
[[nodiscard]] CtcTextAlignment alignExact( const CtcMatrixView matrix, const std::span<const std::string> pieces,
										  const std::string_view text, const std::stop_token stop)
{
	const std::size_t						positions = text.size() + 1 ; // 空prefixを含むblank位置数
	std::vector<TextArc>					arcs ; // 本文を構成する到達可能arcだけを所有する
	std::vector<std::vector<std::uint32_t>> incoming( positions) ; // 終点別の非blank状態番号
	std::vector<bool>						reachable( positions, false) ; // token化としての到達可能性
	reachable[0] = true ;
	std::size_t work = 0 ; // 語彙照合のbyte数を事前に上限管理する
	// 到達したprefixから一致する全pieceを追加し、最少token化へ限定しない
	for( std::size_t begin = 0; begin < text.size(); ++begin) { // prefixの開始byte
		checkCancelled( stop) ;
		// UTF-8途中や表現不能prefixから余計なarcを生成しない
		if( !reachable[begin]) {
			continue ;
		}
		// 語彙ID順で追加し、同率時のtraceback順序を固定する
		for( std::size_t token = 0; token < pieces.size(); ++token) { // 語彙の元ID
			const auto &piece = pieces[token] ; // 一致を確認する借用piece
			// blank・予約piece・残り本文より長いpieceは本文arcにならない
			if( static_cast<std::int32_t>( token) == matrix.blank || piece.front() == '<' ||
			   piece.size() > text.size() - begin) {
				continue ;
			}
			work += piece.size() ;
			// 比較量超過は候補の黙示的な切詰めにせず窓の保持理由とする
			if( work > K_MAXIMUM_WORK) {
				return unavailable( CtcTextStatus::RESOURCE_LIMIT) ;
			}
			// 正確に同じ表記へ進めるpieceだけをgraphへ加える
			if( text.substr( begin, piece.size()) == piece) {
				// byte位置とarcの合計をtraceback前にも制限する
				if( positions + arcs.size() == K_MAXIMUM_STATES) {
					return unavailable( CtcTextStatus::RESOURCE_LIMIT) ;
				}
				const std::size_t end = begin + piece.size() ; // 一致した終端byte
				incoming[end].push_back( static_cast<std::uint32_t>( positions + arcs.size())) ;
				arcs.push_back( {begin, end, static_cast<std::int32_t>( token)}) ;
				reachable[end] = true ;
			}
		}
	}
	// 表記自体が構成できない場合はframe不足とは区別する
	if( !reachable.back()) {
		return unavailable( CtcTextStatus::UNREPRESENTABLE) ;
	}
	const std::size_t						states = positions + arcs.size() ; // blankと非blankの全状態数
	std::vector<std::vector<std::uint32_t>> previous( states) ; // 各状態へ入る固定遷移
	std::vector<std::int32_t>				labels( states, matrix.blank) ; // 各状態が出力する語彙ID
	std::size_t								edges = 0 ; // DPの作業量を計算する遷移総数
	// blankは同じ位置のblankまたは非blankから入り、本文を進めない
	for( std::size_t position = 0; position < positions; ++position) { // 本文prefix位置
		previous[position] = incoming[position] ;
		previous[position].push_back( static_cast<std::uint32_t>( position)) ;
		std::sort( previous[position].begin(), previous[position].end()) ;
		edges += previous[position].size() ;
	}
	// 非blankは反復滞在、blankからの発火、異なるIDからの発火を分ける
	for( std::size_t index = 0; index < arcs.size(); ++index) { // 非blank arc番号
		checkCancelled( stop) ;
		const auto		 &arc = arcs[index] ; // 現在の出力piece
		const std::size_t state = positions + index ; // このarcを表す非blank状態
		labels[state] = arc.token ;
		previous[state].push_back( static_cast<std::uint32_t>( arc.begin)) ;
		previous[state].push_back( static_cast<std::uint32_t>( state)) ;
		edges += 2 ;
		// 同じIDの再発火にはblankを要求し、同一tokenの連続を二重計数しない
		for( const auto source : incoming[arc.begin]) { // 直前prefixへ着いた非blank状態
			// 異なるIDならblankを挟まないCTC遷移を許可する
			if( arcs[source - positions].token != arc.token) {
				previous[state].push_back( source) ;
				++edges ;
			}
			// 大きなfan-inで遷移vectorを無制限に増やさない
			if( edges > K_MAXIMUM_EDGES) {
				return unavailable( CtcTextStatus::RESOURCE_LIMIT) ;
			}
		}
		std::sort( previous[state].begin(), previous[state].end()) ;
	}
	// 積の計算前に除算で上限判定し、tracebackと実行時間の双方を制限する
	if( edges > K_MAXIMUM_EDGES || edges > K_MAXIMUM_WORK / std::max( std::size_t{1}, matrix.frames) ||
	   states > K_MAXIMUM_TRACE_BYTES / sizeof(std::uint32_t) / std::max( std::size_t{1}, matrix.frames)) {
		return unavailable( CtcTextStatus::RESOURCE_LIMIT) ;
	}
	std::vector<double>		   sums( states, K_IMPOSSIBLE) ; // 前frameまでの全経路確率
	std::vector<double>		   best( states, K_IMPOSSIBLE) ; // 前frameまでの最良一経路確率
	std::vector<double>		   nextSums( states, K_IMPOSSIBLE) ; // 次frameの経路和
	std::vector<double>		   nextBest( states, K_IMPOSSIBLE) ; // 次frameの最良一経路
	std::vector<std::uint32_t> trace( states * matrix.frames, K_NO_STATE) ; // 最良前状態
	sums[0] = 0 ;
	best[0] = 0 ;
	// 1frameずつ全状態を更新し、同一frame中に複数pieceを発火させない
	for( std::size_t frame = 0; frame < matrix.frames; ++frame) { // 入力音響frame番号
		checkCancelled( stop) ;
		// 前frameだけを読むことで経路の合流とtoken化の分岐を正確に合算する
		for( std::size_t state = 0; state < states; ++state) { // 更新先状態
			double		  total = K_IMPOSSIBLE ; // この状態へ入る全経路和
			double		  maximum = K_IMPOSSIBLE ; // 時刻に使用する最良前経路
			std::uint32_t predecessor = K_NO_STATE ; // 同率は最小状態IDを優先
			// 固定遷移順に到達可能な前状態を集計する
			for( const auto source : previous[state]) { // 前frameの状態番号
				total = logAdd( total, sums[source]) ;
				// 厳密に高い場合だけ置き換え、同率で時刻を揺らさない
				if( best[source] > maximum) {
					maximum = best[source] ;
					predecessor = source ;
				}
			}
			// このframeで状態のIDを出力する音響log確率
			const double emission = matrix.values[frame * matrix.vocabulary + static_cast<std::size_t>( labels[state])] ;
			nextSums[state] = total + emission ;
			nextBest[state] = maximum + emission ;
			trace[frame * states + state] = predecessor ;
		}
		sums.swap( nextSums) ;
		best.swap( nextBest) ;
	}
	std::vector<std::uint32_t> terminals = incoming.back() ; // 完全文の非blank終端
	terminals.push_back( static_cast<std::uint32_t>( text.size())) ;
	std::sort( terminals.begin(), terminals.end()) ;
	CtcTextAlignment result = unavailable( CtcTextStatus::FRAME_INSUFFICIENT) ; // 空の初期結果
	std::uint32_t	 state = K_NO_STATE ; // 最良の終端状態
	// blank終了と非blank終了を互いに重複しない経路として合算する
	for( const auto terminal : terminals) { // 完全文で終了した状態
		result.logProbability = logAdd( result.logProbability, sums[terminal]) ;
		// 最良一経路だけを時刻へ使い、経路和から架空の時刻を作らない
		if( best[terminal] > result.pathLogProbability) {
			result.pathLogProbability = best[terminal] ;
			state = terminal ;
		}
	}
	// frame数の不足で経路がない本文は成功結果にしない
	if( state == K_NO_STATE) {
		return result ;
	}
	result.status = CtcTextStatus::READY ;
	// 終端から戻り、同一非blank状態への滞在を一つのtoken区間へまとめる
	for( std::size_t remaining = matrix.frames; remaining > 0; --remaining) { // 残りframe数
		checkCancelled( stop) ;
		const std::size_t frame = remaining - 1 ; // 現在の逆向きframe
		const auto		  source = trace[frame * states + state] ; // 最良経路の前状態
		// 非blank区間だけを時刻として記録し、blankへ本文を割り当てない
		if( state >= positions) {
			// 区間の終端を初めて逆走したときだけ一つのtokenを追加する
			if( result.spans.empty() || result.spans.back().begin != frame + 1 ||
			   result.tokens.back() != labels[state]) {
				result.tokens.push_back( labels[state]) ;
				result.spans.push_back( {frame, frame + 1}) ;
			} else {
				result.spans.back().begin = frame ;
			}
		}
		state = source ;
	}
	std::reverse( result.tokens.begin(), result.tokens.end()) ;
	std::reverse( result.spans.begin(), result.spans.end()) ;
	return result ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief ADR 0040の比較用表記を作り、元本文の所有権を変更しません
 * @param[in] text 最大4096byteのUTF-8。外側ASCII space/tabだけを除きます
 * @return ASCII大文字だけを小文字へ対応させた所有文字列
 * @throws OperationError 不正UTF-8、内部制御文字、byte上限超過
 * @see koefude::normalizeCrossAsrText
 */
//------------------------------------------------------------------------------
std::string normalizeCrossAsrText( std::string_view text)
{
	// 原本文のbyte数を空白除去より先に制限する
	if( text.size() > 4096) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto wide = toWide( text) ; // ASCII以外の制御文字も不正UTF-8と分けず拒否する表記
	// Unicode制御領域と行区切りを単一の比較本文へ持ち込まない
	for( const wchar_t value : wide) { // 検証中のUTF-16 code unit
		// 非ASCII制御文字を語彙の表現不能という正常状態へ隠さない
		if( (value >= 0x7f && value <= 0x9f) || value == 0x2028 || value == 0x2029) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	// 外側のASCII水平空白だけを除き、内部空白や全角空白を保持する
	while( !text.empty() && (text.front() == ' ' || text.front() == '\t')) {
		text.remove_prefix( 1) ;
	}
	// 後端にも同じ限定した除去規則を適用する
	while( !text.empty() && (text.back() == ' ' || text.back() == '\t')) {
		text.remove_suffix( 1) ;
	}
	std::string result( text) ; // 呼出し元と独立した比較表記
	// ASCII以外のbyteを変更せず、内部制御文字だけは明示拒否する
	for( char &value : result) { // 比較表記中の一byte
		// 改行やNULを単一窓の本文として黙って連結しない
		if( static_cast<unsigned char>( value) < 32 || value == 127) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		// 固定CTC語彙と対応するASCII大文字だけを小文字へ写す
		if( value >= 'A' && value <= 'Z') {
			value = static_cast<char>( value + ('a' - 'A')) ;
		}
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 行列と語彙の整合・容量・UTF-8を一度検証します
 * @param[in] matrix 呼出し後も借用するCTC行列
 * @param[in] pieces matrix語彙順の本文piece。先頭SentencePiece空白は変換済み
 * @param[in] stop 検証中の取消し
 * @throws OperationError 不正入力・上限超過・取消し
 * @see koefude::CtcTextAligner::CtcTextAligner
 */
//------------------------------------------------------------------------------
CtcTextAligner::CtcTextAligner( const CtcMatrixView matrix, const std::span<const std::string> pieces,
							   const std::stop_token stop)
	: matrix_( matrix), pieces_( pieces)
{
	static_cast<void>( decodeCtcGreedy( matrix, stop)) ;
	// 語彙IDと行列の列の対応が曖昧な入力を拒否する
	if( pieces.size() != matrix.vocabulary) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 全pieceのUTF-8とbyte容量を一度検証し、各候補で繰り返さない
	for( const auto &piece : pieces) { // 所有元の固定語彙piece
		checkCancelled( stop) ;
		// 空pieceのepsilon遷移や過大な外部語彙を許可しない
		if( piece.empty() || piece.size() > 512) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		static_cast<void>( toWide( piece)) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 指定表記へ連結する全CTC経路と決定的な最良時刻を求めます
 * @param[in] text 最大4096byte、正規化済みのUTF-8。空本文を許可
 * @param[in] allowLeadingSpace trueなら先頭一ASCII空白付き経路も合算
 * @param[in] stop graph構築・各frame・tracebackの取消し
 * @return 所有結果。適用外は空token/span、負の無限大の確率
 * @throws OperationError 不正本文・取消し。部分結果は返しません
 * @see koefude::CtcTextAligner::align
 */
//------------------------------------------------------------------------------
CtcTextAlignment CtcTextAligner::align( const std::string_view text, const bool allowLeadingSpace,
									   const std::stop_token stop) const
{
	checkCancelled( stop) ;
	// 正規化を呼出し元の明示操作にし、本文を採点中に黙って変更しない
	if( normalizeCrossAsrText( text) != text) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return alignLiteral( text, allowLeadingSpace, stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 大小文字や外側空白を変更しない正確な表記へ整列します
 * @param[in] text 最大4096byteのUTF-8。空本文を許可し、制御文字は拒否します
 * @param[in] allowLeadingSpace 先頭一ASCII空白を加えた別経路も許可する場合true
 * @param[in] stop graph構築・各frame・tracebackの取消し
 * @return 所有結果。適用外の表記は空token/spanと負の無限大の確率
 * @throws OperationError 不正本文、byte上限超過、取消し。部分結果なし
 * @see koefude::CtcTextAligner::alignLiteral
 */
//------------------------------------------------------------------------------
CtcTextAlignment CtcTextAligner::alignLiteral( const std::string_view text, const bool allowLeadingSpace,
											  const std::stop_token stop) const
{
	checkCancelled( stop) ;
	static_cast<void>( normalizeCrossAsrText( text)) ;
	// 外側制御文字も表記として受入れず、空白だけを明示表記へ許可する
	for( const unsigned char character : text) { // UTF-8の検証済み一byte
		// tabや改行を暗黙に消す正規化を行わない
		if( character < 32 || character == 127) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	// 制御tokenの文字列を通常の本文経路へ変換しない
	if( text.find_first_of( "<>") != std::string_view::npos) {
		return unavailable( CtcTextStatus::RESERVED_TEXT) ;
	}
	auto result = alignExact( matrix_, pieces_, text, stop) ; // 空白なしの出力の経路和
	// 厳密表記だけの呼出し、または計算上限では部分的な合算を返さない
	if( !allowLeadingSpace || result.status == CtcTextStatus::RESOURCE_LIMIT) {
		return result ;
	}
	// decoderが除去する先頭一空白を含む、別の正確な出力
	auto spaced = alignExact( matrix_, pieces_, " " + std::string( text), stop) ;
	// 片側の上限超過を部分経路だけの確率で覆い隠さない
	if( spaced.status == CtcTextStatus::RESOURCE_LIMIT) {
		return spaced ;
	}
	// 空白なしが対応不能なら空白付きの結果をそのまま利用する
	if( result.status != CtcTextStatus::READY) {
		// 語彙では構成可能な場合、frame不足というより具体的な状態を残す
		if( spaced.status == CtcTextStatus::READY || spaced.status == CtcTextStatus::FRAME_INSUFFICIENT) {
			return spaced ;
		}
		return result ;
	}
	// 二つとも可能なら確率を合算し、時刻はより高い一経路へ対応させる
	if( spaced.status == CtcTextStatus::READY) {
		const double total = logAdd( result.logProbability, spaced.logProbability) ; // 経路和
		// 同率では空白なしを優先する固定規則
		if( spaced.pathLogProbability > result.pathLogProbability) {
			result = std::move( spaced) ;
		}
		result.logProbability = total ;
	}
	return result ;
}

} // namespace koefude
