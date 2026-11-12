//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file ctc_terms.cpp
 * @brief 明示用語を本文byteの接尾prefix状態で照合します
 * @version 1.0.0
 * @date 2026/09/12
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/12 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/12 固定語彙の表現確認、失敗遷移、加点上限を追加
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
#include "core/ctc_terms.h"

#include <span>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

#include "core/ctc_decoder.h"
#include "core/types.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

constexpr double	  K_MAXIMUM_BONUS = 8 ; // 完成語と先読みを合計した補助点の上限
constexpr std::size_t K_MAXIMUM_SPELLING_WORK = 80'000'000 ; // 表現可能性を比較するbyte数の上限

/** @brief 失敗遷移を含む一つのUTF-8 byte状態です */
struct TermNode {
	/// 全byteの遷移。構築中の0は子なし
	std::array<std::uint32_t, 256>		   next{} ;
	/// 各用語の未完成prefixの最大進行度
	std::array<float, K_MAXIMUM_CTC_TERMS> progress{} ;
	/// 一致しない場合に使う最長の接尾prefix
	std::uint32_t						   failure = 0 ;
	/// この状態で完了する用語ID集合
	std::uint64_t						   output = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief モデル制御用のpieceを本文用語から区別します
 * @param[in] piece 非空の語彙piece
 * @return 山括弧で囲まれた予約pieceならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool reservedPiece( const std::string_view piece)
{
	return piece.size() >= 2 && piece.front() == '<' && piece.back() == '>' ;
}

//------------------------------------------------------------------------------
/**
 * @brief 暗黙の正規化を行わず、明示用語の形式を検証します
 * @param[in] term 非空でbyte上限内のUTF-8用語
 * @throws OperationError 不正UTF-8、前後空白、制御文字、予約表記
 */
//------------------------------------------------------------------------------
void validateTerm( const std::string &term)
{
	const auto		  wide = toWide( term) ; // Unicode空白と制御文字を検証する所有文字列
	std::vector<WORD> classes( wide.size()) ; // Windowsの文字分類。UTF-16単位と同数
	// 文字分類に失敗した入力を検証済みとして扱わない
	if( wide.empty() || !GetStringTypeW( CT_CTYPE1, wide.data(), static_cast<int>( wide.size()), classes.data())) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 外側の空白を黙って削らず、利用者に意図する用語の指定を求める
	if( (classes.front() & C1_SPACE) != 0 || (classes.back() & C1_SPACE) != 0) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 全文字を確認し、行境界やモデル制御表記を用語へ含めない
	for( std::size_t index = 0; index < wide.size(); ++index) { // UTF-16内の検証位置
		// 改行・制御文字・Unicode行区切り・山括弧を拒否する
		if( (classes[index] & C1_CNTRL) != 0 || wide[index] == 0x2028 || wide[index] == 0x2029 || wide[index] == L'<' ||
		   wide[index] == L'>') {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 固定pieceで表現できる最少token列を、byte位置のDPで確認します
 * @param[in] term UTF-8と上限を検証済みの用語
 * @param[in] pieces 固定語彙。先頭ASCII空白は最初のtokenだけ除去可能です
 * @param[in] blank 非本文のblank ID
 * @param[in,out] work 全用語を通した比較byte数。上限超過で失敗します
 * @param[in] stop byte位置ごとの取消し
 * @return 最大64 tokenの決定的な表現例。学習時tokenizerの再現ではありません
 * @throws OperationError 表現不能、上限超過、取消し
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::int32_t> spellTerm( const std::string &term, const std::span<const std::string> pieces,
												  const std::int32_t blank, std::size_t &work,
												  const std::stop_token stop)
{
	std::vector<std::vector<std::int32_t>> best( term.size() + 1) ; // byte位置までの最少ID列
	std::vector<bool>					   reachable( term.size() + 1, false) ; // 空列と到達不能を区別する状態
	reachable[0] = true ;
	// 先頭から到達可能なbyte位置だけを拡張し、逆向きや循環の遷移を作らない
	for( std::size_t position = 0; position < term.size(); ++position) { // 現在の用語byte位置
		checkCancelled( stop) ;
		// まだ表現できない位置から候補を生成しない
		if( !reachable[position]) {
			continue ;
		}
		// IDの小さい順に全語彙を照合し、同率の診断列を決定的にする
		for( std::size_t index = 0; index < pieces.size(); ++index) { // 借用語彙内のID
			auto piece = std::string_view( pieces[index]) ; // 先頭だけ調整可能な借用view
			// blankと制御用語彙から通常用語を構成しない
			if( static_cast<std::int32_t>( index) == blank || reservedPiece( piece)) {
				continue ;
			}
			// 製品decoderが除く先頭一個の空白だけを同じ条件で許可する
			if( position == 0 && piece.starts_with( ' ')) {
				piece.remove_prefix( 1) ;
			}
			// 空白だけのtokenや残りを超えるpieceで停止しない遷移を作らない
			if( piece.empty() || piece.size() > term.size() - position) {
				continue ;
			}
			// 全用語の表現確認にも計算量上限を適用する
			if( piece.size() > K_MAXIMUM_SPELLING_WORK - work) {
				throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
			}
			work += piece.size() ;
			// 本文表記が一致する語彙だけで到達可能性を更新する
			if( std::string_view( term).substr( position, piece.size()) != piece) {
				continue ;
			}
			auto candidate = best[position] ; // 現在のprefixへ一tokenを追加する所有列
			candidate.push_back( static_cast<std::int32_t>( index)) ;
			const std::size_t end = position + piece.size() ; // 今回到達する用語byte位置
			// 少ないtoken数を優先し、同数ならID列の辞書順で一意にする
			if( !reachable[end] || candidate.size() < best[end].size() ||
			   (candidate.size() == best[end].size() && candidate < best[end])) {
				best[end] = std::move( candidate) ;
				reachable[end] = true ;
			}
		}
	}
	// 表現不能な用語を無効な辞書として黙って受け付けない
	if( !reachable.back()) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 長過ぎる診断列を後の探索へ無制限に渡さない
	if( best.back().size() > 64) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	return std::move( best.back()) ;
}

} // namespace

/** @brief 同期worker内の固定語彙とbyte状態を所有する実装です */
class CtcTermContext::Impl {
	public:
	/// 構築時の語彙を所有copyした列
	std::vector<std::string>	 pieces ;
	/// 明示用語と診断用token列
	std::vector<CtcTermSpelling> spellings ;
	/// rootを含むbyte状態。最大4097状態
	std::vector<TermNode>		 nodes ;
	/// 非本文の語彙ID
	std::int32_t				 blank = 0 ;
	/// 完全一致一語の加点。0～4
	double						 boost = 0 ;
	/// 存在する用語IDだけのbit集合
	std::uint64_t				 allowedMask = 0 ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 公開状態を使用する前に、所属contextの容量とIDを確認します
	 * @param[in] state 同じcontextで生成したprefix状態
	 * @throws OperationError 状態番号または用語bitの不正
	 */
	//------------------------------------------------------------------------------
	void						 validate( const CtcTermState state) const
	{
		// 架空のnodeや用語IDで所有配列の外へ進まない
		if( state.node >= nodes.size() || (state.completed & ~allowedMask) != 0) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 語彙での表現可能性と容量を検証し、用語状態を構築します
 * @param[in] pieces ID順のUTF-8本文piece。語頭記号は既存decoderと同じ空白へ変換済み
 * 2～8192語、各256byte以下。構築中だけ借用し内部へcopyします
 * @param[in] blank pieces内のblank ID。blankと予約tokenは用語の表現に使いません
 * @param[in] terms 最大64語、1語192byte、合計4096byte以下。空集合は無効化です
 * @param[in] boost 一語一回の加点。有限な0～4。0は音響順位を変更しません
 * @param[in] stop 構築中の取消し。失敗時はcontextを返しません
 * @throws OperationError 不正語彙/用語、表現不能、上限超過、取消し
 * @see koefude::CtcTermContext::CtcTermContext(std::span< const std::string >, std::int32_t, std::span< const std::string >, double, std::stop_token)
 */
//------------------------------------------------------------------------------
CtcTermContext::CtcTermContext( const std::span<const std::string> pieces, const std::int32_t blank,
							   const std::span<const std::string> terms, const double boost, const std::stop_token stop)
	: implementation_( std::make_unique<Impl>())
{
	checkCancelled( stop) ;
	// 個別上限を先に確認し、語彙copyや状態allocationを有界にする
	if( pieces.size() > K_MAXIMUM_CTC_VOCABULARY || terms.size() > K_MAXIMUM_CTC_TERMS) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	// 語彙のblankと補助点の定義が不正なら構築を開始しない
	if( pieces.size() < 2 || blank < 0 || static_cast<std::size_t>( blank) >= pieces.size() || !std::isfinite( boost) ||
	   boost < 0 || boost > 4) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 予約tokenも含む全語彙のbyte数とUTF-8を検証し、後の遷移の意味を固定する
	for( const auto &piece : pieces) { // 現在の本文piece
		checkCancelled( stop) ;
		// 空または過大なpieceを暗黙に飛ばして語彙を変えない
		if( piece.empty() || piece.size() > 256) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		static_cast<void>( toWide( piece)) ; // 不正UTF-8をbyte状態へ持ち込まない
	}
	std::size_t				   totalBytes = 0 ; // 状態数のallocation前に求める全用語長
	std::set<std::string_view> unique ; // 入力の寿命中だけ借用する重複検出集合
	// 用語をそのまま検証し、重複や空白を正規化して隠さない
	for( const auto &term : terms) { // 現在の明示用語
		checkCancelled( stop) ;
		// 先にbyte長を確認し、UTF-16変換と総量加算も制限する
		if( term.size() > K_MAXIMUM_CTC_TERM_BYTES || term.size() > K_MAXIMUM_CTC_TERM_TOTAL_BYTES - totalBytes) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		// 空や同じ語の多重登録による加点を禁止する
		if( term.empty() || !unique.insert( term).second) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
		validateTerm( term) ;
		totalBytes += term.size() ;
	}
	auto &data = *implementation_ ; // 構築中だけ変更する所有実装
	data.pieces.assign( pieces.begin(), pieces.end()) ;
	data.blank = blank ;
	data.boost = boost ;
	data.allowedMask =
		terms.size() == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << terms.size()) - 1 ;
	data.nodes.reserve( totalBytes + 1) ;
	data.nodes.emplace_back() ;
	std::size_t spellingWork = 0 ; // 全用語に共通の表現確認budget
	// 登録順をIDとし、各用語のbyte prefixを所有trieへ追加する
	for( std::size_t index = 0; index < terms.size(); ++index) { // 明示用語のID
		checkCancelled( stop) ;
		data.spellings.push_back( {terms[index], spellTerm( terms[index], pieces, blank, spellingWork, stop)}) ;
		std::uint32_t node = 0 ; // この用語の構築中prefix
		// 有界な全byteを順に追加し、UTF-8を別の表記へ変換しない
		for( std::size_t position = 0; position < terms[index].size(); ++position) {
			// 今回追加する用語byte位置
			const auto byte = static_cast<unsigned char>( terms[index][position]) ; // 0～255
			// まだ存在しないprefixだけ新しい状態を追加する
			if( data.nodes[node].next[byte] == 0) {
				data.nodes[node].next[byte] = static_cast<std::uint32_t>( data.nodes.size()) ;
				data.nodes.emplace_back() ;
			}
			node = data.nodes[node].next[byte] ;
			// 完成語には先読みを二重加点せず、真の途中prefixだけを記録する
			if( position + 1 < terms[index].size()) {
				data.nodes[node].progress[index] =
					static_cast<float>( position + 1) / static_cast<float>( terms[index].size()) ;
			}
		}
		data.nodes[node].output |= std::uint64_t{1} << index ;
	}
	std::vector<std::uint32_t> queue ; // 失敗遷移を親から確定する有界な幅優先列
	queue.reserve( data.nodes.size()) ;
	// rootの実子だけを最初の階層にし、存在しないbyteはrootへ戻る
	for( const auto child : data.nodes.front().next) { // 最初のbyteの子状態
		// 実在するprefixを一回ずつ処理する
		if( child != 0) {
			queue.push_back( child) ;
		}
	}
	// 親と失敗先が確定した状態から順に全byteの遷移を埋める
	for( std::size_t index = 0; index < queue.size(); ++index) { // 幅優先で確定する状態
		checkCancelled( stop) ;
		const auto node = queue[index] ; // 今回の所有node番号
		const auto fallback = data.nodes[node].failure ; // 確定済みの最長接尾prefix
		data.nodes[node].output |= data.nodes[fallback].output ;
		// 接尾prefixでも未完成語が続く場合は最大進行度を保持する
		for( std::size_t term = 0; term < terms.size(); ++term) { // 候補用語のID
			data.nodes[node].progress[term] =
				std::max( data.nodes[node].progress[term], data.nodes[fallback].progress[term]) ;
		}
		// 全byteを一度ずつ確定し、推論中の文字照合を一遷移にする
		for( std::size_t byte = 0; byte < 256; ++byte) { // 可能な次byte
			const auto child = data.nodes[node].next[byte] ; // 元trieの子、0なら欠落
			// 実子へは失敗先を設定し、次の階層で処理する
			if( child != 0) {
				data.nodes[child].failure = data.nodes[fallback].next[byte] ;
				queue.push_back( child) ;
			} else {
				data.nodes[node].next[byte] = data.nodes[fallback].next[byte] ;
			}
		}
	}
	checkCancelled( stop) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有語彙と用語状態を解放します
 * @see koefude::CtcTermContext::~CtcTermContext
 */
//------------------------------------------------------------------------------
CtcTermContext::~CtcTermContext() = default ;

//------------------------------------------------------------------------------
/**
 * @brief 新しく発火した一tokenの本文pieceだけ用語状態を進めます
 * @param[in] state 同じcontextで得たprefix状態
 * @param[in] token 語彙内の非blank ID。同token継続やblank frameには呼びません
 * @return 新しい状態。元の本文・tokenは変更しません
 * @throws OperationError 状態/ID不正。予約tokenは進行をrootへ戻します
 * @see koefude::CtcTermContext::advance
 */
//------------------------------------------------------------------------------
CtcTermState CtcTermContext::advance( CtcTermState state, const std::int32_t token) const
{
	const auto &data = *implementation_ ; // 呼出し中だけ借用するimmutable状態
	data.validate( state) ;
	static_cast<void>( pieceBytes( token)) ; // 外部IDを配列参照の前に検証する
	const auto &piece = data.pieces[token] ; // 元語彙と同じ本文byte列
	// 予約tokenに含まれる文字を語の根拠として継続させない
	if( reservedPiece( piece)) {
		state.node = 0 ;
		return state ;
	}
	// 新しいtokenの全byteを一度ずつ進め、完成語を集合として記録する
	for( const unsigned char byte : piece) { // 固定語彙の一byte
		state.node = data.nodes[state.node].next[byte] ;
		state.completed |= data.nodes[state.node].output ;
	}
	return state ;
}

//------------------------------------------------------------------------------
/**
 * @brief 完成済みの加点と、一時的な先読み点を分けて返します
 * @param[in] state 同じcontextで得たprefix状態
 * @return 合計8以下の補助点。最終順位にはcompletedだけを使用します
 * @throws OperationError 状態番号または用語IDの不正
 * @see koefude::CtcTermContext::score
 */
//------------------------------------------------------------------------------
CtcTermScore CtcTermContext::score( const CtcTermState state) const
{
	const auto &data = *implementation_ ; // 読み取り専用の用語情報
	data.validate( state) ;
	CtcTermScore result ; // 音響確率に含めない二種類の補助点
	result.completed = std::min( K_MAXIMUM_BONUS, std::popcount( state.completed) * data.boost) ;
	// 未加点の用語だけ先読みし、既に完成した語の繰返しを誘導しない
	for( std::size_t index = 0; index < data.spellings.size(); ++index) { // 明示用語のID
		// 完成済みの語には部分一致点を加えない
		if( (state.completed & (std::uint64_t{1} << index)) == 0) {
			result.partial = std::max( result.partial, 0.5 * data.boost * data.nodes[state.node].progress[index]) ;
		}
	}
	result.partial = std::min( result.partial, K_MAXIMUM_BONUS - result.completed) ;
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief 語彙同一性の件数境界を返します
 * @return blankを含む固定語彙数
 * @see koefude::CtcTermContext::vocabularySize
 */
//------------------------------------------------------------------------------
std::size_t CtcTermContext::vocabularySize() const noexcept
{
	return implementation_->pieces.size() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 語彙同一性のblank境界を返します
 * @return 固定blank ID
 * @see koefude::CtcTermContext::blank
 */
//------------------------------------------------------------------------------
std::int32_t CtcTermContext::blank() const noexcept
{
	return implementation_->blank ;
}

//------------------------------------------------------------------------------
/**
 * @brief 無効化と測定条件を照合します
 * @return 一語の明示加点。0～4
 * @see koefude::CtcTermContext::boost
 */
//------------------------------------------------------------------------------
double CtcTermContext::boost() const noexcept
{
	return implementation_->boost ;
}

//------------------------------------------------------------------------------
/**
 * @brief 照合量の計算に使うpiece長を返します
 * @param[in] token 非blank ID
 * @return 所有本文pieceのbyte数
 * @throws OperationError ID不正
 * @see koefude::CtcTermContext::pieceBytes
 */
//------------------------------------------------------------------------------
std::size_t CtcTermContext::pieceBytes( const std::int32_t token) const
{
	// 本文の追加に使えないblankや範囲外IDを拒否する
	if( token < 0 || static_cast<std::size_t>( token) >= implementation_->pieces.size() ||
	   token == implementation_->blank) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	return implementation_->pieces[token].size() ;
}

//------------------------------------------------------------------------------
/**
 * @brief 診断用の表現例を借用します
 * @return 本contextの寿命内で有効なimmutable列
 * @see koefude::CtcTermContext::spellings
 */
//------------------------------------------------------------------------------
std::span<const CtcTermSpelling> CtcTermContext::spellings() const noexcept
{
	return implementation_->spellings ;
}

} // namespace koefude
