//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_plan.cpp
 * @brief 全量の一次被覆を保持する有限な計画を作ります
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の計画と計数receiptを追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1310のFinal1536・派生予算と限定検証へ整合
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1318でEOF候補の不要な行境界短縮を抑止
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/whole_text_plan.h"
#include "../../native/whole_text/generator.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <charconv>
#include <limits>
#include <utility>
#include <windows.h>

#include <bcrypt.h>

namespace koefude {
namespace {
using Diag = WholeTextPlanDiagnostic ; // 本文なし診断
//------------------------------------------------------------------------------
/**
 * @brief 上限を超える加算を拒否します
 * @param[in] a 使用済み
 * @param[in] b 追加
 * @param[in] cap 上限
 * @param[in] d 診断
 * @return 検証済み和
 */
//------------------------------------------------------------------------------
std::uint64_t add( std::uint64_t a, std::uint64_t b, std::uint64_t cap, Diag d)
{
	requireWholeTextPlan( a <= cap && b <= cap - a, d) ;
	return a + b ;
}
//------------------------------------------------------------------------------
/**
 * @brief byte位置がUTF-8境界か確認します
 * @param[in] s 元byte
 * @param[in] p 位置
 * @return 境界ならtrue
 */
//------------------------------------------------------------------------------
bool boundary( std::string_view s, std::uint64_t p)
{
	return p <= s.size() &&
		   (p == s.size() || (static_cast<unsigned char>( s[static_cast<std::size_t>( p)]) & 0xc0) != 0x80) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 既存stripの空白集合を使います
 * @param[in] c scalar
 * @return 空白ならtrue
 */
//------------------------------------------------------------------------------
bool whitespace( std::uint32_t c)
{
	return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
		   (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 ;
}
//------------------------------------------------------------------------------
/**
 * @brief strictな一scalarを復号します
 * @param[in] s 全byte
 * @param[in,out] p 元位置
 * @return scalar
 */
//------------------------------------------------------------------------------
std::uint32_t scalar( std::string_view s, std::uint64_t &p)
{
	requireWholeTextPlan( p < s.size(), Diag::COVERAGE) ;
	const auto	  lead = static_cast<unsigned char>( s[static_cast<std::size_t>( p++)]) ; // 先頭
	std::uint32_t c = lead, n = 0, minimum = 0 ; // 復号値と後続数
	// 有効な先頭byteごとに必要な後続数を固定します
	if( lead >= 0xc2 && lead <= 0xdf) {
		c &= 31 ;
		n = 1 ;
		minimum = 0x80 ;
		// 成立する条件だけを処理し、未成立の値は元の状態へ残します
	} else if( lead >= 0xe0 && lead <= 0xef) {
		c &= 15 ;
		n = 2 ;
		minimum = 0x800 ;
		// 成立する条件だけを処理し、未成立の値は元の状態へ残します
	} else if( lead >= 0xf0 && lead <= 0xf4) {
		c &= 7 ;
		n = 3 ;
		minimum = 0x10000 ;
	} else {
		requireWholeTextPlan( lead < 0x80, Diag::COVERAGE) ;
	}
	requireWholeTextPlan( n <= s.size() - p, Diag::COVERAGE) ;
	// 宣言された後続byteだけを消費します
	for( std::uint32_t i = 0; i < n; ++i) {
		const auto b = static_cast<unsigned char>( s[static_cast<std::size_t>( p++)]) ; // 後続
		requireWholeTextPlan( (b & 0xc0) == 0x80, Diag::COVERAGE) ;
		c = (c << 6) | (b & 63) ;
	}
	requireWholeTextPlan( c >= minimum && c > 0 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff), Diag::COVERAGE) ;
	return c ;
}
/** @brief 256byte以上間隔のrank checkpointです */
struct Checkpoint {
	/// 元byte
	std::uint32_t byte = 0 ;
	/// 全scalar rank
	std::uint32_t count = 0 ;
	/// 非空白rank
	std::uint32_t content = 0 ;
} ;
/** @brief 一originのflat indexです */
struct OriginIndex {
	/// Oのtape位置またはAのgroup rank
	std::uint64_t tape = 0 ;
	/// 全scalar
	std::uint32_t count = 0 ;
	/// 非空白scalar
	std::uint32_t content = 0 ;
	/// checkpoint開始
	std::uint32_t cpBegin = 0 ;
	/// checkpoint終端
	std::uint32_t cpEnd = 0 ;
} ;
/** @brief 全scalarのoffset列を作らない作業indexです */
struct Index {
	/// 全O
	std::vector<OriginIndex>			original ;
	/// 全A
	std::vector<OriginIndex>			raw ;
	/// checkpointだけ
	std::vector<Checkpoint>				points ;
	/// raw元順
	std::vector<WholeTextRawAssignment> assignments ;
	/// anchor別stable index
	std::vector<std::uint32_t>			grouped ;
	/// O別group原点
	std::vector<std::uint64_t>			groupBegin ;
	/// O別M
	std::vector<std::uint64_t>			groupTotal ;
	/// 直接連結原文
	std::string							tape ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 一originのrankをindex化します
 * @param[in,out] ix index
 * @param[in] text 元byte
 * @param[in] stop 停止
 * @return origin情報
 */
//------------------------------------------------------------------------------
OriginIndex indexText( Index &ix, std::string_view text, std::stop_token stop)
{
	OriginIndex out ; // 元rank
	out.cpBegin = static_cast<std::uint32_t>( ix.points.size()) ;
	std::uint64_t last = 0 ; // 最後のcheckpoint
	// 全scalarを検査し、256byte経過後だけcheckpointを追加します
	for( std::uint64_t p = 0; p < text.size();) {
		checkCancelled( stop) ;
		// 短い文字列ではcheckpoint確保を増やしません
		if( p - last >= 253) {
			ix.points.push_back( {static_cast<std::uint32_t>( p), out.count, out.content}) ;
			last = p ;
		}
		const auto c = scalar( text, p) ; // strict scalar
		++out.count ;
		// 原byteを残して非空白rankだけを別に数えます
		if( !whitespace( c)) {
			++out.content ;
		}
	}
	out.cpEnd = static_cast<std::uint32_t>( ix.points.size()) ;
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief byte位置のrankを求めます
 * @param[in] s 元byte
 * @param[in] ix origin index
 * @param[in] points 全checkpoint
 * @param[in] byte 境界
 * @param[in] content 非空白rank指定
 * @return rank
 */
//------------------------------------------------------------------------------
std::uint64_t rank( std::string_view s, const OriginIndex &ix, const std::vector<Checkpoint> &points,
				   // この同期境界だけで使用する検証値
				   std::uint64_t byte, bool content)
{
	requireWholeTextPlan( boundary( s, byte), Diag::COVERAGE) ;
	const auto	  first = points.begin() + ix.cpBegin, last = points.begin() + ix.cpEnd ; // 局所表
	// この同期境界だけで使用する検証値
	auto		  found = std::upper_bound( first, last, byte,
		//------------------------------------------------------------------------------
		/**
		 * @brief 元行内のbyte位置より後にあるcheckpointをupper_boundで探すため、位置を比較します
		 * @param[in] value 探索する元行先頭からのUTF-8 byte offsetです
		 * @param[in] cp 同期探索中だけ参照するcheckpointで、byteも同じ元行を原点とします
		 * @return valueがcp.byteより小さい場合だけtrue。同じ位置または後ならfalseです
		 */
		//------------------------------------------------------------------------------
		[]( auto value, const Checkpoint &cp)
		{
			return value < cp.byte ;
		}) ; // 直前
	std::uint64_t p = 0, n = 0 ; // checkpoint以前の原点
	// 最も近いcheckpointを使います
	if( found != first) {
		--found ;
		p = found->byte ;
		n = content ? found->content : found->count ;
	}
	// 指定byteへ達するまで約256byte以内を復号します
	while( p < byte) {
		const auto c = scalar( s, p) ; // 現scalar
		// 指定されたrank集合だけを数えます
		if( !content || !whitespace( c)) {
			++n ;
		}
	}
	return n ;
}
//------------------------------------------------------------------------------
/**
 * @brief scalar ordinalを元byteへ戻します
 * @param[in] s 元byte
 * @param[in] ix origin
 * @param[in] points 全checkpoint
 * @param[in] ordinal rank
 * @return byte境界
 */
//------------------------------------------------------------------------------
std::uint64_t offset( std::string_view s, const OriginIndex &ix, const std::vector<Checkpoint> &points,
					 std::uint64_t ordinal)
{
	requireWholeTextPlan( ordinal <= ix.count, Diag::COVERAGE) ;
	const auto	  first = points.begin() + ix.cpBegin, last = points.begin() + ix.cpEnd ; // 局所
	// この同期境界だけで使用する検証値
	auto		  found = std::upper_bound( first, last, ordinal,
		//------------------------------------------------------------------------------
		/**
		 * @brief Unicode scalar ordinalより後にあるcheckpointをupper_boundで探すため、件数を比較します
		 * @param[in] value 元行先頭から数えたUnicode scalarのordinalです。byte数ではありません
		 * @param[in] cp 同期探索中だけ参照するcheckpointで、countは同じ元行のscalar累積数です
		 * @return valueがcp.countより小さい場合だけtrue。同じ件数または後ならfalseです
		 */
		//------------------------------------------------------------------------------
		[]( auto value, const Checkpoint &cp)
		{
			return value < cp.count ;
		}) ; // 直前rank
	std::uint64_t p = 0, n = 0 ; // 原点
	// 直前checkpointから復号します
	if( found != first) {
		--found ;
		p = found->byte ;
		n = found->count ;
	}
	// 指定ordinalの先頭で停止します
	while( n < ordinal) {
		static_cast<void>( scalar( s, p)) ;
		++n ;
	}
	return p ;
}
//------------------------------------------------------------------------------
/**
 * @brief checked ceil(a*b/c)です
 * @param[in] a rank
 * @param[in] b M
 * @param[in] c S
 * @return 逆射影rank
 */
//------------------------------------------------------------------------------
std::uint64_t ceiling( std::uint64_t a, std::uint64_t b, std::uint64_t c)
{
	requireWholeTextPlan( c > 0 && (a == 0 || b <= UINT64_MAX / a), Diag::PLAN_BYTES) ;
	const auto product = a * b ; // 検査済み積
	return product / c + (product % c != 0 ? 1 : 0) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有界flat indexを確保します
 * @param[in] in 材料
 * @param[in] limits 上限
 * @param[in] stop 停止
 * @return 全index
 */
//------------------------------------------------------------------------------
Index buildIndex( const WholeTextPlanInputView &in, const WholeTextPlanLimits &limits, std::stop_token stop)
{
	requireWholeTextPlan( !in.original.empty() && in.original.size() <= 50000 && in.raw.size() <= 50000, Diag::COVERAGE) ;
	requireWholeTextPlan( in.auxiliary.size() <= limits.auxiliaryBytes &&
							 in.canonicalSpans.size() <= limits.physicalSpans,
						 Diag::PLAN_BYTES) ;
	requireWholeTextPlan( wholeTextSha256( in.auxiliary) == in.auxiliarySha256, Diag::COVERAGE) ;
	std::uint64_t textBytes = 0, cpCapacity = 0 ; // 確保前の計数
	// 全O/Aを数え、scalar配列を確保しません
	for( const auto &o : in.original) {
		textBytes = add( textBytes, o.text.size(), limits.inputBytes, Diag::PLAN_BYTES) ;
		cpCapacity += o.text.size() / 253 ;
	}
	// raw本文も同じbyte上限へ加算します
	for( const auto &a : in.raw) {
		textBytes = add( textBytes, a.text.size(), limits.inputBytes, Diag::PLAN_BYTES) ;
		cpCapacity += a.text.size() / 253 ;
	}
	const auto cpBytes = cpCapacity * sizeof(Checkpoint) ; // 最大checkpoint
	requireWholeTextPlan( cpBytes <= limits.checkpointBytes, Diag::PLAN_BYTES) ;
	// この同期境界だけで使用する検証値
	const auto owned = (in.original.size() + in.raw.size()) * sizeof(OriginIndex) +
					   in.raw.size() * (sizeof(WholeTextRawAssignment) + sizeof(std::uint32_t)) +
					   in.original.size() * sizeof(std::uint64_t) * 3 + cpBytes ; // 挿入位置copyも含む
	// この同期境界だけで使用する検証値
	const auto materialIndex = in.original.size() * sizeof(WholeTextOriginalOrigin) +
							   in.raw.size() * sizeof(WholeTextRawOrigin) +
							   (in.canonicalSpans.size() + 8) * sizeof(WholeTextCanonicalSpan) ;
	requireWholeTextPlan( owned + textBytes + materialIndex <= limits.indexBytes, Diag::PLAN_BYTES) ;
	Index out ; // 全確保の前に上限検査済み
	out.original.reserve( in.original.size()) ;
	out.raw.reserve( in.raw.size()) ;
	out.points.reserve( static_cast<std::size_t>( cpCapacity)) ;
	out.assignments.reserve( in.raw.size()) ;
	out.grouped.resize( in.raw.size()) ;
	out.groupBegin.resize( in.original.size()) ;
	out.groupTotal.resize( in.original.size()) ;
	out.tape.reserve( static_cast<std::size_t>( textBytes)) ;
	std::int64_t  previous = 0 ; // 非減少source開始
	std::uint64_t content = 0 ; // 非空白総数
	// 全原文をseparatorなしで連結します
	for( const auto &o : in.original) {
		requireWholeTextPlan( o.startMs >= previous && o.endMs >= o.startMs, Diag::COVERAGE) ;
		previous = o.startMs ;
		auto item = indexText( out, o.text, stop) ; // 行index
		item.tape = out.tape.size() ;
		content += item.content ;
		out.original.push_back( item) ;
		out.tape.append( o.text) ;
	}
	requireWholeTextPlan( content > 0, Diag::NO_ORIGINAL_CONTENT) ;
	// 元raw順でanchorを固定し、本文による候補探索をしません
	for( const auto &a : in.raw) {
		auto		item = indexText( out, a.text, stop) ; // 全raw rank
		// この同期境界だけで使用する検証値
		const auto	anchor = a.startMs.value_or( static_cast<std::int64_t>( a.windowBegin / 16)) ;
		// この同期境界だけで使用する検証値
		auto		found = std::upper_bound( in.original.begin(), in.original.end(), anchor,
			//------------------------------------------------------------------------------
			/**
			 * @brief raw区間の開始時刻に対応する原文行を選ぶため、開始時刻を比較します
			 * @param[in] value rawのstartMs、未通知ならwindowBeginを16sample/msで換算した元録音基準のanchor（ms）です
			 * @param[in] o 同期探索中だけ参照する原文行で、startMsも元録音基準です
			 * @return valueがo.startMsより前ならtrue。同時刻または後ならfalseです
			 */
			//------------------------------------------------------------------------------
			[]( auto value, const auto &o)
			{
				return value < o.startMs ;
			}) ; // 最後の開始値
		// この同期境界だけで使用する検証値
		std::size_t target =
			found == in.original.begin() ? 0 : static_cast<std::size_t>( found - in.original.begin() - 1) ; // 候補O
		const auto selected = in.original[target].startMs ; // 同値なら先頭
		// 同開始値の最初の元行へ戻します
		while( target > 0 && in.original[target - 1].startMs == selected) {
			--target ;
		}
		// 全空白のanchorは次の非空白行、なければ直前へ寄せます
		if( out.original[target].content == 0) {
			auto next = target ; // 次の候補
			// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
			while( next < in.original.size() && out.original[next].content == 0) {
				++next ;
			}
			// 次の候補があれば優先します
			if( next < in.original.size()) {
				target = next ;
			} else {
				// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
				while( out.original[target].content == 0) {
					--target ;
				}
			}
		}
		const auto &o = in.original[target] ; // 最終anchor
		out.assignments.push_back( {static_cast<std::uint32_t>( target), a.startMs.has_value(),
								   a.startMs && a.endMs && *a.startMs < o.endMs && *a.endMs > o.startMs}) ;
		out.raw.push_back( item) ;
		++out.groupBegin[target] ;
	}
	std::uint64_t cursor = 0 ; // stable groupの開始
	// 件数表をprefix原点へ変換します
	for( auto &count : out.groupBegin) {
		// 現在のscalar数または検証済み件数
		const auto n = count ;
		count = cursor ;
		cursor += n ;
	}
	auto positions = out.groupBegin ; // 上限に計数済みの挿入位置
	// 元raw順を保ってanchor別へ詰めます
	for( std::size_t a = 0; a < in.raw.size(); ++a) {
		const auto o = out.assignments[a].original ; // anchor O
		out.grouped[static_cast<std::size_t>( positions[o]++)] = static_cast<std::uint32_t>( a) ;
		out.raw[a].tape = out.groupTotal[o] ;
		out.groupTotal[o] += out.raw[a].count ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief tape範囲を元Oへ分割します
 * @param[in] in 材料
 * @param[in] ix index
 * @param[in] r 範囲
 * @return 元順slice
 */
//------------------------------------------------------------------------------
std::vector<WholeTextPlanSlice> originalSlices( const WholeTextPlanInputView &in, const Index &ix, WholeTextByteRange r)
{
	std::vector<WholeTextPlanSlice> out ; // 局所範囲だけ
	// この同期境界だけで使用する検証値
	auto							found = std::upper_bound( ix.original.begin(), ix.original.end(), r.begin,
		//------------------------------------------------------------------------------
		/**
		 * @brief 原文tapeのbyte境界に交差する最初の行を探すため、行の先頭位置を比較します
		 * @param[in] value 連結した原文tapeの先頭からのUTF-8 byte offsetです
		 * @param[in] o 同期探索中だけ参照する原文indexで、tapeは同じ連結原文中の行開始byteです
		 * @return valueがo.tapeより小さい場合だけtrue。同じ位置または後ならfalseです
		 */
		//------------------------------------------------------------------------------
		[]( auto value, const auto &o)
		{
			return value < o.tape ;
		}) ; // 最初の候補
	// 有限列内の現在のordinal
	std::size_t						i =
		found == ix.original.begin() ? 0 : static_cast<std::size_t>( found - ix.original.begin() - 1) ; // 元index
	// core終端まで正の交差だけを保持します
	for(; i < in.original.size() && ix.original[i].tape < r.end; ++i) {
		const auto base = ix.original[i].tape, end = base + in.original[i].text.size() ; // 行範囲
		// 元rangeと交差する行だけを扱います
		if( end > r.begin) {
			// この同期境界だけで使用する検証値
			const auto b = std::max( base, r.begin) - base, e = std::min( end, r.end) - base ;
			// 零長Oには新byteを発明しません
			if( b < e) {
				out.push_back( {static_cast<std::uint32_t>( i), {b, e}}) ;
			}
		}
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief scalar rankの逆像でraw一次sliceを作ります
 * @param[in] in 材料
 * @param[in] ix index
 * @param[in] slices O範囲
 * @return raw範囲
 */
//------------------------------------------------------------------------------
std::vector<WholeTextPlanSlice> rawSlices( const WholeTextPlanInputView &in, const Index &ix,
										  const std::vector<WholeTextPlanSlice> &slices)
{
	std::vector<WholeTextPlanSlice> out ; // 一候補だけ
	// 同anchorのrawだけをflat groupから探します
	for( const auto &s : slices) {
		// 現在の元原文origin
		const auto o = s.origin ;
		const auto total = ix.groupTotal[o] ; // M
		// 空白originにはrawを捨て込みません
		if( ix.original[o].content == 0) {
			continue ;
		}
		// 現在のUTF-8 byteまたはrank位置
		const auto p = rank( in.original[o].text, ix.original[o], ix.points, s.bytes.begin, true) ;
		// この同期境界だけで使用する検証値
		const auto q = rank( in.original[o].text, ix.original[o], ix.points, s.bytes.end, true) ;
		// この同期境界だけで使用する検証値
		const auto low = ceiling( p, total, ix.original[o].content) ;
		const auto high = ceiling( q, total, ix.original[o].content) ; // 逆像終端
		// この同期境界だけで使用する検証値
		const auto begin = ix.groupBegin[o],
				   end = o + 1 < ix.groupBegin.size() ? ix.groupBegin[o + 1] : ix.grouped.size() ; // 同anchor範囲
		// この同期境界だけで使用する検証値
		auto first = ix.grouped.begin() + static_cast<std::ptrdiff_t>( begin) ;
		auto last = ix.grouped.begin() + static_cast<std::ptrdiff_t>( end) ; // borrow終端
		// 零長rawはOの最初の非空白scalarを含むcoreに一度記録します
		if( p == 0 && q > 0) {
			// 全要素を元の順序で処理し、範囲の終端で停止します
			for( auto empty = first; empty != last; ++empty) {
				// 空本文だけを零長originとして記録します
				if( in.raw[*empty].text.empty()) {
					out.push_back( {*empty, {0, 0}}) ;
				}
			}
		}
		// この同期境界だけで使用する検証値
		auto found = std::lower_bound( first, last, low,
			//------------------------------------------------------------------------------
			/**
			 * @brief rawのscalar区間の終端と下端を比較し、正の交差がない区間を探索から除きます
			 * @param[in] a 捕捉したix.rawとin.rawに共通する検証済みraw origin indexです
			 * @param[in] value 同じ原文originへ割り当てたraw群の中で求めた交差下端のscalar ordinalです。byte数ではありません
			 * @return ix.raw[a]のscalar終端がvalue以下ならtrue。正の交差を持ち得る区間ならfalseです
			 */
			//------------------------------------------------------------------------------
			[&]( auto a, auto value)
			{
				return ix.raw[a].tape + ix.raw[a].count <= value ;
			}) ; // 下端と交差する最初のraw
		// 上端へ達するまで各rawとの交差を一回だけ保持します
		for(; found != last; ++found) {
			// 現在の元raw origin
			const auto a = *found ;
			// この同期境界だけで使用する検証値
			const auto base = ix.raw[a].tape ;
			const auto tail = base + ix.raw[a].count ; // raw rank終端
			// 上端以後は全て範囲外です
			if( base >= high) {
				break ;
			}
			// 正の交差だけを元byte境界へ戻します
			if( tail > low) {
				// この同期境界だけで使用する検証値
				const auto b = offset( in.raw[a].text, ix.raw[a], ix.points, std::max( low, base) - base) ;
				// この同期境界だけで使用する検証値
				const auto e = offset( in.raw[a].text, ix.raw[a], ix.points, std::min( high, tail) - base) ;
				// 空scalar交差はpromptへ増やしません
				if( b < e) {
					out.push_back( {a, {b, e}}) ;
				}
			}
		}
	}
	// 零長originも含め、元raw順をpromptの局所範囲へ維持します
	std::stable_sort( out.begin(), out.end(),
					 //------------------------------------------------------------------------------
					 /**
					  * @brief 抽出したraw sliceを元のorigin番号順に安定整列します
					  * @param[in] a 同期比較中だけ参照する左のraw sliceです
					  * @param[in] b 同期比較中だけ参照する右のraw sliceです
					  * @return a.originがb.originより小さい場合だけtrue。同じoriginはfalseとし、stable_sortが元の順序を保ちます
					  */
					 //------------------------------------------------------------------------------
					 []( const auto &a, const auto &b)
					 {
						 return a.origin < b.origin ;
					 }) ;
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一scalarを残して半減します
 * @param[in] tape 元byte
 * @param[in] r 範囲
 * @param[in] right 内側が右端
 * @return 縮小または最小範囲
 */
//------------------------------------------------------------------------------
WholeTextByteRange halve( std::string_view tape, WholeTextByteRange r, bool right)
{
	// file端の空文脈だけは空を保ちます
	if( r.begin == r.end) {
		return r ;
	}
	auto p = r.begin + (r.end - r.begin) / 2 ; // byte中点
	// 中点以前の最後のUTF-8境界へ戻します
	while( p > r.begin && !boundary( tape, p)) {
		--p ;
	}
	// 右端側は内側の最後のscalarを残します
	if( right) {
		// 中点が先頭なら最初のscalarを除ける時だけ除きます
		if( p == r.begin) {
			// この同期境界だけで使用する検証値
			auto next = p ;
			static_cast<void>( scalar( tape, next)) ;
			p = next < r.end ? next : p ;
		}
		return {p, r.end} ;
	}
	// 左端側は最低一scalarを残します
	if( p == r.begin) {
		static_cast<void>( scalar( tape, p)) ;
	}
	return {r.begin, p} ;
}
/** @brief SHA用の有界固定順writerです */
class Encoding {
	public:
	/// 封印byte
	std::string	  bytes ;
	/// 上限
	std::uint64_t cap = 8388608 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief byteを追加します
	 * @param[in] s 全byte
	 */
	//------------------------------------------------------------------------------
	void		  raw( std::string_view s)
	{
		requireWholeTextPlan( bytes.size() <= cap && s.size() <= cap - bytes.size(), Diag::PLAN_BYTES) ;
		bytes.append( s) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 符号なし64bit整数の全8byteをlittle endian順で有界encoding先へ追加します
	 * @param[in] n 固定8byteとして記録する符号なし整数です。可変長整数へは変換しません
	 */
	//------------------------------------------------------------------------------
	void number( std::uint64_t n)
	{
		// 元幅の全byteを固定順へ書きます
		for( unsigned i = 0; i < 8; ++i) {
			const char c = static_cast<char>( n >> (i * 8)) ; // 下位byte
			raw( std::string_view( &c, 1)) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 長さ付きbyteを書きます
	 * @param[in] s 元byte
	 */
	//------------------------------------------------------------------------------
	void text( std::string_view s)
	{
		number( s.size()) ;
		raw( s) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief SHAを書きます
	 * @param[in] d 全32byte
	 */
	//------------------------------------------------------------------------------
	void digest( const WholeTextDigest &d)
	{
		// object表現のborrowは同期hashまでです
		raw( std::string_view( reinterpret_cast<const char *>( d.data()), d.size())) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 半開範囲を書きます
	 * @param[in] r 範囲
	 */
	//------------------------------------------------------------------------------
	void range( WholeTextByteRange r)
	{
		number( r.begin) ;
		number( r.end) ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief receipt全fieldを固定順へ書きます
 * @param[in,out] e writer
 * @param[in] r receipt
 */
//------------------------------------------------------------------------------
void encodeReceipt( Encoding &e, const WholeTextTokenReceipt &r)
{
	e.number( r.session) ;
	e.number( r.sequence) ;
	e.number( 2) ;
	e.number( r.prefixTokens) ;
	e.number( r.coreTokens) ;
	e.number( r.beforeTokens) ;
	e.number( r.afterTokens) ;
	e.number( r.fits) ;
	e.number( r.completeTokenDigest) ;
	// 全byte長とdigestを同じ順に束縛します
	for( const auto b : r.bytes) {
		e.number( b) ;
	}
	// 全要素を元の順序で処理し、範囲の終端で停止します
	for( const auto &d : r.sha256) {
		e.digest( d) ;
	}
	e.digest( r.candidateBinding) ;
	e.digest( r.tokenSha256) ;
}
//------------------------------------------------------------------------------
/**
 * @brief planの全fieldを固定順へ封印します
 * @param[in] p 計画
 * @param[in] cap 上限
 * @return 全SHA
 */
//------------------------------------------------------------------------------
std::string planEncoding( const WholeTextChunkPlan &p, std::uint64_t cap)
{
	Encoding e ;
	e.cap = cap ; // 有限writer
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_chunk_plan.v2" ; // NULも含む
	e.raw( std::string_view( HASH_DOMAIN, sizeof(HASH_DOMAIN))) ;
	e.digest( p.inputBinding) ;
	e.digest( p.originalSha256) ;
	e.digest( p.auxiliarySha256) ;
	e.number( p.measures) ;
	e.number( p.measuredBytes) ;
	e.number( p.chunks.size()) ;
	// 全recordを元順で封印します
	for( const auto &c : p.chunks) {
		e.number( c.index) ;
		e.range( c.core) ;
		e.range( c.before) ;
		e.range( c.after) ;
		e.number( c.verbatimWhitespace) ;
		// OとAは別roleとして件数付きで保持します
		for( const auto *list : {&c.original, &c.raw}) {
			e.number( list->size()) ;
			// 全要素を元の順序で処理し、範囲の終端で停止します
			for( const auto &s : *list) {
				e.number( s.origin) ;
				e.range( s.bytes) ;
			}
		}
		e.number( c.receipt.has_value()) ;
		// Verbatimにはtoken数を作りません
		if( c.receipt) {
			encodeReceipt( e, *c.receipt) ;
			e.digest( c.receipt->receiptSha256) ;
		}
	}
	e.number( p.assignments.size()) ;
	// uncertainな割当ての根拠も束縛します
	for( const auto &a : p.assignments) {
		e.number( a.original) ;
		e.number( a.knownStart) ;
		e.number( a.envelopeOverlap) ;
	}
	e.number( p.canonical.size()) ;
	// canonical物理byteの一次ownerを全件封印します
	for( const auto &c : p.canonical) {
		e.number( c.span.kind) ;
		e.number( c.span.origin) ;
		e.range( c.span.bytes) ;
		e.number( c.span.tokenCount) ;
		e.number( c.span.anchorSample) ;
		e.number( c.chunk) ;
	}
	return std::move( e.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 旧順のplan encodingを封印します
 * @param[in] p 全計画
 * @param[in] cap 上限
 * @return SHA
 */
//------------------------------------------------------------------------------
WholeTextDigest planDigest( const WholeTextChunkPlan &p, std::uint64_t cap)
{
	return wholeTextSha256( planEncoding( p, cap)) ;
}
/** @brief 確保前にescape長を数える局所JSON writerです */
class JsonWriter {
	public:
	/// 書込み巡だけ所有
	std::string	  bytes ;
	/// 展開長
	std::uint64_t size = 0 ;
	/// 二巡の区別
	bool		  encode = false ;
	//------------------------------------------------------------------------------
	/**
	 * @brief fragmentを加えます
	 * @param[in] s 全byte
	 */
	//------------------------------------------------------------------------------
	void		  raw( std::string_view s)
	{
		size = add( size, s.size(), 65536, Diag::PROJECTION_DENSITY) ;
		// 計数後だけ実byteを追加します
		if( encode) {
			bytes.append( s) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 符号付き64bit整数をlocaleに依存しない10進ASCII表現でJSON writerへ追加します
	 * @param[in] value JSON数値へ書き出す符号付き整数です。負数の符号も保持します
	 */
	//------------------------------------------------------------------------------
	void number( std::int64_t value)
	{
		std::array<char, 32> b{} ; // 最大i64表現
		const auto			 r = std::to_chars( b.data(), b.data() + b.size(), value) ; // locale非依存
		raw( std::string_view( b.data(), static_cast<std::size_t>( r.ptr - b.data()))) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 元byteをJSON escapeします
	 * @param[in] s strict UTF-8
	 */
	//------------------------------------------------------------------------------
	void quote( std::string_view s)
	{
		raw( "\"") ;
		// UTF-8 byteは変更せずJSON必須のASCIIだけをescapeします
		for( const unsigned char c : s) {
			// quoteとbackslashを二byteへ展開します
			if( c == '"' || c == '\\') {
				const char b[2] = {'\\', static_cast<char>( c)} ; // escape
				raw( std::string_view( b, 2)) ;
				// 成立する条件だけを処理し、未成立の値は元の状態へ残します
			} else if( c < 32) {
				constexpr char HEX[] = "0123456789abcdef" ; // 固定表
				const char	   b[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 15]} ; // 制御
				raw( std::string_view( b, 6)) ;
			} else {
				const char b = static_cast<char>( c) ; // 元byte
				raw( std::string_view( &b, 1)) ;
			}
		}
		raw( "\"") ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 元rangeをstrictに借用します
 * @param[in] s 元byte
 * @param[in] r 範囲
 * @return 無編集borrow
 */
//------------------------------------------------------------------------------
std::string_view sliceText( std::string_view s, WholeTextByteRange r)
{
	requireWholeTextPlan( r.begin <= r.end && boundary( s, r.begin) && boundary( s, r.end), Diag::COVERAGE) ;
	return s.substr( static_cast<std::size_t>( r.begin), static_cast<std::size_t>( r.end - r.begin)) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 候補全byteを束縛します
 * @param[in] p 全prompt
 * @return SHA
 */
//------------------------------------------------------------------------------
WholeTextDigest candidateDigest( const WholeTextChunkPrompt &p)
{
	Encoding e ; // 一候補だけ
	e.raw( "koefude.whole_text_candidate.v2") ;
	// 全五fieldの長さとbyteを固定順へ束縛します
	for( const auto *s : {&p.system, &p.user, &p.core, &p.before, &p.after}) {
		e.text( *s) ;
	}
	return wholeTextSha256( e.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief receiptの全数とbyteを照合します
 * @param[in] p 同じ候補
 * @param[in] r receipt
 */
//------------------------------------------------------------------------------
void validateReceipt( const WholeTextChunkPrompt &p, const WholeTextTokenReceipt &r)
{
	// この同期境界だけで使用する検証値
	const auto rendered = renderWholeTextGeneration( p.kind, p.system, p.user) ; // 共通renderer
	// 元の無編集byteの借用
	const std::array<std::string_view, 6> text{p.system, p.user, rendered, p.core, p.before, p.after} ;
	requireWholeTextPlan( r.session != 0 && r.sequence != 0 && r.candidateBinding == p.candidateBinding &&
							 r.receiptSha256 == wholeTextReceiptDigest( r),
						 Diag::COUNTER_RECEIPT) ;
	// 全fieldをhashと長さの両方で照合します
	for( std::size_t i = 0; i < text.size(); ++i) {
		requireWholeTextPlan( r.bytes[i] == text[i].size() && r.sha256[i] == wholeTextSha256( text[i]),
							 Diag::COUNTER_RECEIPT) ;
	}
	// この同期境界だけで使用する検証値
	const bool fits = r.prefixTokens > 0 && r.prefixTokens <= 4096 && r.coreTokens > 0 && r.coreTokens <= 512 &&
					  r.beforeTokens <= 128 && r.afterTokens <= 128 &&
					  static_cast<std::uint64_t>( r.prefixTokens) +
							  (p.kind == WholeTextGenerationKind::MINISTRAL_PLAIN ? 1536 : 2816) <=
						  (p.kind == WholeTextGenerationKind::MINISTRAL_PLAIN ? 5632 : 6912) ; // 独立実数だけ
	requireWholeTextPlan( r.fits == fits && (!fits || (r.completeTokenDigest && r.tokenSha256 != WholeTextDigest{})) &&
							 (p.before.empty() == (r.beforeTokens == 0)) && (p.after.empty() == (r.afterTokens == 0)),
						 Diag::COUNTER_RECEIPT) ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 本文を含まない診断を投げます
 * @param[in] valid 成立
 * @param[in] diagnostic 分類
 * @see koefude::requireWholeTextPlan
 */
//------------------------------------------------------------------------------
void requireWholeTextPlan( bool valid, WholeTextPlanDiagnostic diagnostic)
{
	// 固定診断とエラー分類だけを境界へ返します
	if( !valid) {
		// この同期境界だけで使用する検証値
		const auto kind = diagnostic == Diag::RUNTIME_CAPABILITY
							  ? ErrorKind::MODEL_REJECTED
							  : (diagnostic == Diag::COVERAGE || diagnostic == Diag::COUNTER_RECEIPT ||
										 diagnostic == Diag::NO_ORIGINAL_CONTENT
									 ? ErrorKind::INVALID_INPUT
									 : ErrorKind::RESULT_TOO_LARGE) ;
		throw OperationError( kind, static_cast<std::uint32_t>( diagnostic)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 全byteのSHAを計算します
 * @param[in] bytes 全入力
 * @return raw32byte
 * @see koefude::wholeTextSha256(std::span< const std::uint8_t >)
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextSha256( std::span<const std::uint8_t> bytes)
{
	requireWholeTextPlan( bytes.size() <= 268435456, Diag::PLAN_BYTES) ;
	// この同期境界で所有する値、または確定状態の検証値です
	BCRYPT_ALG_HANDLE  algorithm = nullptr ;
	BCRYPT_HASH_HANDLE hash = nullptr ; // OS所有資源
	// 現在の元raw origin
	const auto		   a = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) ;
	// provider失敗では未所有のhashを使いません
	if( a < 0) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( a)) ;
	}
	const auto b = BCryptCreateHash( algorithm, &hash, nullptr, 0, nullptr, 0, 0) ; // hash所有
	// 部分確保を解放して元codeを返します
	if( b < 0) {
		BCryptCloseAlgorithmProvider( algorithm, 0) ;
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( b)) ;
	}
	WholeTextDigest out{} ; // 全32byte
	// この同期境界だけで使用する検証値
	const auto		c =
		BCryptHashData( hash, const_cast<PUCHAR>( bytes.data()), static_cast<ULONG>( bytes.size()), 0) ; // APIは入力不変
	// この同期境界だけで使用する検証値
	const auto d = c < 0 ? c : BCryptFinishHash( hash, out.data(), static_cast<ULONG>( out.size()), 0) ;
	BCryptDestroyHash( hash) ;
	BCryptCloseAlgorithmProvider( algorithm, 0) ;
	// hash失敗は空digestへ置換しません
	if( d < 0) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( d)) ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief UTF-8 byteのSHAです
 * @param[in] bytes 全入力
 * @return raw32byte
 * @see koefude::wholeTextSha256(std::string_view)
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextSha256( std::string_view bytes)
{
	// object表現のborrowはこの同期callだけです
	return wholeTextSha256( std::span( reinterpret_cast<const std::uint8_t *>( bytes.data()), bytes.size())) ;
}
//------------------------------------------------------------------------------
/**
 * @brief SHAを小文字hexにします
 * @param[in] digest 全SHA
 * @return 64byte
 * @see koefude::wholeTextHex
 */
//------------------------------------------------------------------------------
std::string wholeTextHex( const WholeTextDigest &digest)
{
	constexpr char HEX[] = "0123456789abcdef" ; // 固定表
	// この境界で構築する所有結果
	std::string	   out ;
	out.reserve( 64) ; // SHAだけ
	// 全32byteを省略せず表現します
	for( const auto b : digest) {
		out.push_back( HEX[b >> 4]) ;
		out.push_back( HEX[b & 15]) ;
	}
	return out ;
}
//------------------------------------------------------------------------------
/**
 * @brief receiptの固定encodingをhashします
 * @param[in] receipt 全field
 * @return 全SHA
 * @see koefude::wholeTextReceiptDigest
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextReceiptDigest( const WholeTextTokenReceipt &receipt)
{
	Encoding e ;
	e.raw( "koefude.whole_text_tokens.v2") ;
	encodeReceipt( e, receipt) ;
	return wholeTextSha256( e.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 内部試験で不正fieldを再hashして構造拒否を独立確認します
 * @param[in] plan 全DTO
 * @return 固定順encodingのSHA、計画の受理ではありません
 * @see koefude::detail::wholeTextPlanDigest
 */
//------------------------------------------------------------------------------
WholeTextDigest detail::wholeTextPlanDigest( const WholeTextChunkPlan &plan)
{
	return planDigest( plan, 8388608) ;
}
//------------------------------------------------------------------------------
/**
 * @brief modelを開く前に全材料とindex確保上限を検査します
 * @param[in] input 全量borrow
 * @param[in] stop 停止。生成/計数は行いません
 * @see koefude::detail::preflightWholeTextPlanMaterials
 */
//------------------------------------------------------------------------------
void detail::preflightWholeTextPlanMaterials( const WholeTextPlanInputView &input, std::stop_token stop)
{
	static_cast<void>( buildIndex( input, {}, stop)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ材料だけから一候補を描画します
 * @param[in] in 全量borrow
 * @param[in] record 元range
 * @param[in] stop 停止
 * @return 一候補所有byte
 * @throws OperationError 不正range/密度超過
 * @see koefude::materializeWholeTextChunk
 */
//------------------------------------------------------------------------------
WholeTextChunkPrompt materializeWholeTextChunk( const WholeTextPlanInputView &in, const WholeTextChunkRecord &record,
											   std::stop_token stop)
{
	checkCancelled( stop) ;
	requireWholeTextPlan( record.core.begin < record.core.end && record.core.end - record.core.begin <= 4096 &&
							 record.before.begin <= record.before.end &&
							 record.before.end - record.before.begin <= 512 && record.after.begin <= record.after.end &&
							 record.after.end - record.after.begin <= 512,
						 Diag::PROJECTION_DENSITY) ;
	std::uint64_t originalBytes = 0 ; // append前の全tape長
	// 巨大な不正rangeを借用文字列のコピーより先に拒否します
	for( const auto &o : in.original) {
		originalBytes = add( originalBytes, o.text.size(), 8388608, Diag::PLAN_BYTES) ;
	}
	requireWholeTextPlan( in.system.size() <= 65536 && record.core.end <= originalBytes &&
							 record.before.end == record.core.begin && record.after.begin == record.core.end &&
							 record.after.end <= originalBytes,
						 Diag::COVERAGE) ;
	requireWholeTextPlan( record.original.size() + record.raw.size() <= 1024, Diag::PROJECTION_DENSITY) ;
	// この同期境界で所有する値、または確定状態の検証値です
	WholeTextChunkPrompt out ;
	out.kind = in.kind ;
	out.system = in.system ; // 一候補だけ所有
	std::array<std::vector<WholeTextPlanSlice>, 3> contextSlices ; // 元O/contextのIDとbyte
	std::uint64_t								   tape = 0 ; // 原文tape位置
	std::uint32_t								   originIndex = 0 ; // 元O番号
	// core/contextの元byteをseparatorなしで直接連結します
	for( const auto &o : in.original) {
		// この同期境界だけで使用する検証値
		const std::array<std::pair<WholeTextByteRange, std::string *>, 3> fields{
			{{record.core, &out.core}, {record.before, &out.before}, {record.after, &out.after}}} ;
		// 三つのroleは一次coverageと混同しません
		std::size_t role = 0 ; // core/before/after
		// 全要素を元の順序で処理し、範囲の終端で停止します
		for( const auto &[range, target] : fields) {
			// この同期境界だけで使用する検証値
			const auto b = std::max( tape, range.begin), e = std::min( tape + o.text.size(), range.end) ;
			// 正の交差だけを無編集で追加します
			if( b < e) {
				requireWholeTextPlan( contextSlices[0].size() + contextSlices[1].size() + contextSlices[2].size() +
											 record.raw.size() <
										 1024,
									 Diag::PROJECTION_DENSITY) ;
				contextSlices[role].push_back( {originIndex, {b - tape, e - tape}}) ;
				target->append( sliceText( o.text, {b - tape, e - tape})) ;
			}
			++role ;
		}
		tape += o.text.size() ;
		++originIndex ;
	}
	requireWholeTextPlan( record.core.begin < record.core.end && record.core.end <= tape &&
							 out.core.size() == record.core.end - record.core.begin &&
							 record.original == contextSlices[0],
						 Diag::COVERAGE) ;
	const auto writeOriginal =
		//------------------------------------------------------------------------------
		/**
		 * @brief 指定された原文sliceのID・byte範囲・時刻・無編集本文を同じ順序でJSON配列にします
		 * @param[in,out] w 今回のpromptを計数または書き込む有界writerのborrowです。呼出し中だけ更新します
		 * @param[in] slices 捕捉したin.originalをorigin番号と元行内byte範囲で参照する読取りroleの全slice列です。空なら空配列です
		 * @note 各IDはO:originとし、渡されたroleのsliceを元の順序で出力します
		 */
		//------------------------------------------------------------------------------
		[&]( JsonWriter &w, const std::vector<WholeTextPlanSlice> &slices)
		{
			w.raw( "[") ;
			bool comma = false ; // 配列の最初
			// 各sliceを元情報付きで書きます
			for( const auto &s : slices) {
				// 二件目以後だけ区切ります
				if( comma) {
					w.raw( ",") ;
				}
				comma = true ;
				const auto &o = in.original[s.origin] ; // 元行の無編集borrow
				w.raw( "{\"id\":") ;
				w.quote( "O:" + std::to_string( s.origin)) ;
				w.raw( ",\"begin\":") ;
				w.number( static_cast<std::int64_t>( s.bytes.begin)) ;
				w.raw( ",\"end\":") ;
				w.number( static_cast<std::int64_t>( s.bytes.end)) ;
				w.raw( ",\"start_ms\":") ;
				w.number( o.startMs) ;
				w.raw( ",\"end_ms\":") ;
				w.number( o.endMs) ;
				w.raw( ",\"text\":") ;
				w.quote( sliceText( o.text, s.bytes)) ;
				w.raw( "}") ;
			}
			w.raw( "]") ;
		} ;
	const auto write =
		//------------------------------------------------------------------------------
		/**
		 * @brief 元材料とchunkの束縛情報、原文・raw・補助情報を固定順のuser promptとして計数または出力します
		 * @param[in,out] w 今回のpromptを計数または書き込む有界writerのborrowです。捕捉したin/record等は変更しません
		 * @note 同じJSON描画手順を計数用と書込用のwriterで共用します
		 */
		//------------------------------------------------------------------------------
		[&]( JsonWriter &w)
		{
			w.raw( "DATA_JSON\n{\"schema\":\"koefude.whole_text_chunk_user.v2\",\"material_binding\":{"
			  "\"source_sha256\":") ;
			w.quote( wholeTextHex( in.sourceSha256)) ;
			w.raw( ",\"auxiliary_sha256\":") ;
			w.quote( wholeTextHex( in.auxiliarySha256)) ;
			w.raw( ",\"auxiliary_bytes\":") ;
			w.number( static_cast<std::int64_t>( in.auxiliary.size())) ;
			w.raw( ",\"request_binding\":") ;
			w.quote( wholeTextHex( in.inputBinding)) ;
			w.raw( ",\"original_bytes\":") ;
			w.number( static_cast<std::int64_t>( originalBytes)) ;
			w.raw( "},\"chunk_index\":") ;
			w.number( record.index) ;
			w.raw( ",\"core_range\":[") ;
			w.number( static_cast<std::int64_t>( record.core.begin)) ;
			w.raw( ",") ;
			w.number( static_cast<std::int64_t>( record.core.end)) ;
			w.raw( "],\"Original_before\":") ;
			writeOriginal( w, contextSlices[1]) ;
			w.raw( ",\"Original_core\":") ;
			writeOriginal( w, contextSlices[0]) ;
			w.raw( ",\"Original_after\":") ;
			writeOriginal( w, contextSlices[2]) ;
			w.raw( ",\"local_audio_hypotheses\":[") ;
			bool comma = false ; // raw配列区切り
			// Aの元IDと不確かな技術割当てを明記します
			for( const auto &s : record.raw) {
				requireWholeTextPlan( s.origin < in.raw.size(), Diag::COVERAGE) ;
				// 二件目以後だけ区切ります
				if( comma) {
					w.raw( ",") ;
				}
				comma = true ;
				const auto &a = in.raw[s.origin] ; // 元raw
				w.raw( "{\"id\":") ;
				w.quote( "A:w" + std::to_string( a.window) + ":" + std::to_string( a.row)) ;
				w.raw( ",\"begin\":") ;
				w.number( static_cast<std::int64_t>( s.bytes.begin)) ;
				w.raw( ",\"end\":") ;
				w.number( static_cast<std::int64_t>( s.bytes.end)) ;
				w.raw( ",\"window_samples\":[") ;
				w.number( static_cast<std::int64_t>( a.windowBegin)) ;
				w.raw( ",") ;
				w.number( static_cast<std::int64_t>( a.windowEnd)) ;
				w.raw( "],\"start_ms\":") ;
				// 未知時刻へ整数を補間しません
				if( a.startMs) {
					w.number( *a.startMs) ;
				} else {
					w.raw( "null") ;
				}
				w.raw( ",\"end_ms\":") ;
				// 元の厳密な区間だけを使用します
				if( a.endMs) {
					w.number( *a.endMs) ;
				} else {
					w.raw( "null") ;
				}
				w.raw( ",\"time_status\":") ;
				w.quote( a.startMs ? "KnownNativeInterval" : "NativeIntervalUnknown") ;
				w.raw( ",\"assignment\":") ;
				w.quote( a.startMs ? "KnownStartAnchor/OrdinalWithinAnchor" : "WindowStartFallback/OrdinalWithinAnchor") ;
				w.raw( ",\"uncertain\":true,\"text\":") ;
				w.quote( sliceText( a.text, s.bytes)) ;
				w.raw( "}") ;
			}
			w.raw( "],\"provenance_limits\":\"window/ordinal assignment is uncertain; token/trace "
			  "retained by auxiliary digest; no speaker or acoustic alignment\"}") ;
		} ;
	JsonWriter measure ;
	write( measure) ; // JSON展開の確保前計数
	requireWholeTextPlan( out.system.size() + measure.size + 70 <= 65536, Diag::PROJECTION_DENSITY) ;
	JsonWriter encoded ;
	encoded.encode = true ;
	encoded.bytes.reserve( static_cast<std::size_t>( measure.size)) ;
	write( encoded) ;
	out.user = std::move( encoded.bytes) ;
	// この同期境界だけで使用する検証値
	const auto rendered = renderWholeTextGeneration( in.kind, out.system, out.user) ; // 共通式
	requireWholeTextPlan( out.system.size() + out.user.size() + rendered.size() <= 65536, Diag::PROJECTION_DENSITY) ;
	out.candidateBinding = candidateDigest( out) ;
	checkCancelled( stop) ;
	return out ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全原文を分割して全rawを一回割当てます
 * @param[in] in 同じownerのborrow
 * @param[in] counter 生成なし実計数
 * @param[in] stop 協調停止
 * @param[in] limits 固定値か内部試験で縮小した値
 * @return 全coverage検証済み計画
 * @throws OperationError 部分計画を返しません
 * @see koefude::makeWholeTextChunkPlan
 */
//------------------------------------------------------------------------------
WholeTextChunkPlan makeWholeTextChunkPlan( const WholeTextPlanInputView &in, const WholeTextTokenCounter &counter,
										  std::stop_token stop, const WholeTextPlanLimits &limits)
{
	checkCancelled( stop) ;
	const WholeTextPlanLimits									  fixed ; // 調整口は固定上限を増やせません
	// この同期境界だけで使用する検証値
	const std::array<std::pair<std::uint64_t, std::uint64_t>, 14> bounds{
		{{limits.inputBytes, fixed.inputBytes},
		 {limits.auxiliaryBytes, fixed.auxiliaryBytes},
		 {limits.indexBytes, fixed.indexBytes},
		 {limits.checkpointBytes, fixed.checkpointBytes},
		 {limits.planBytes, fixed.planBytes},
		 {limits.chunks, fixed.chunks},
		 {limits.packets, fixed.packets},
		 {limits.physicalSpans, fixed.physicalSpans},
		 {limits.primarySlices, fixed.primarySlices},
		 {limits.promptBytes, fixed.promptBytes},
		 {limits.promptSlices, fixed.promptSlices},
		 {limits.candidatesPerChunk, fixed.candidatesPerChunk},
		 {limits.measureCalls, fixed.measureCalls},
		 {limits.measuredBytes, fixed.measuredBytes}}} ;
	// 内部試験の縮小だけを許可し、0や固定値超過を先に拒否します
	for( const auto &[actual, maximum] : bounds) {
		requireWholeTextPlan( actual > 0 && actual <= maximum, Diag::PLAN_BYTES) ;
	}
	requireWholeTextPlan( in.textOnly ? in.kind == WholeTextGenerationKind::MINISTRAL_PLAIN &&
										   in.system == textContextSystem() && in.raw.empty() &&
										   in.canonicalSpans.empty() && in.auxiliary.empty()
									 : std::any_of( in.raw.begin(), in.raw.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief textOnly以外の計画で、少なくとも一つ非空のraw本文があるか調べます
		 * @param[in] a any_ofが同期参照するraw originです。textの所有権は移しません
		 * @return a.textのbyte数が0でなければtrue。空白だけの本文もここではtrueで、非空白の検証とは別です
		 */
		//------------------------------------------------------------------------------
		[]( const auto &a)
		{
			return !a.text.empty() ;
		}),
						 Diag::COVERAGE) ;
	auto			   ix = buildIndex( in, limits, stop) ; // 全量とflat index
	WholeTextChunkPlan plan ; // 未完は外へ返しません
	plan.inputBinding = in.inputBinding ;
	plan.originalSha256 = wholeTextSha256( ix.tape) ;
	plan.auxiliarySha256 = in.auxiliarySha256 ;
	plan.assignments = ix.assignments ;
	requireWholeTextPlan( static_cast<bool>( counter), Diag::COUNTER_RECEIPT) ;
	std::uint64_t cursor = 0, totalSlices = 0, encodedBytes = 256 ; // 元tapeと有限encoding見積り
	// 全byteを元順へ進め、未計画の末尾を完了扱いにしません
	while( cursor < ix.tape.size()) {
		checkCancelled( stop) ;
		requireWholeTextPlan( plan.chunks.size() < limits.chunks, Diag::PLAN_CHUNKS) ;
		// この同期境界で所有する値、または確定状態の検証値です
		WholeTextChunkRecord c ;
		c.index = static_cast<std::uint32_t>( plan.chunks.size()) ; // 次record
		c.core = {cursor, std::min<std::uint64_t>( cursor + 4096, ix.tape.size())} ;
		// 最後のUTF-8境界へ戻します
		while( !boundary( ix.tape, c.core.end)) {
			--c.core.end ;
		}
		std::uint64_t preferred = c.core.begin ; // 優先境界がない時はUTF-8境界
		// 範囲内の最後の改行を優先します
		for( auto p = c.core.begin; p < c.core.end; ++p) {
			// 改行byteはscalar境界です
			if( ix.tape[static_cast<std::size_t>( p)] == '\n') {
				preferred = p + 1 ;
			}
		}
		// この同期境界だけで使用する検証値
		const auto rowEnd = std::upper_bound( ix.original.begin(), ix.original.end(), c.core.end,
			//------------------------------------------------------------------------------
			/**
			 * @brief core終端候補を原文行境界へ寄せるため、tape上のbyte位置を比較します
			 * @param[in] v 連結原文tape上のcore終端候補（UTF-8 byte offset）です
			 * @param[in] o 同期探索中だけ参照する原文indexで、tapeは同じtape中の行開始byteです
			 * @return vがo.tapeより小さい場合だけtrue。同じ位置または後ならfalseです
			 */
			//------------------------------------------------------------------------------
			[]( auto v, const auto &o)
			{
				return v < o.tape ;
			}) ; // 最後の行境界
		// 候補内の元行境界が改行より後なら優先します
		if( rowEnd != ix.original.begin()) {
			const auto p = (rowEnd - 1)->tape ; // 同じ原文tape上の境界
			// 開始より後にある境界だけ採ります
			if( p > preferred && p > c.core.begin) {
				preferred = p ;
			}
		}
		// EOFより前の候補だけを優先境界へ短縮し、全文末は実計数で採否を決めます
		if( c.core.end < ix.tape.size() && preferred > c.core.begin) {
			c.core.end = preferred ;
		}
		std::uint64_t leftCap = 512, rightCap = 512 ; // 半減済みの文脈予算をcore縮小後も保ちます
		std::uint64_t attempts = 0 ; // 一chunkの候補数
		// 固定半減列を一scalarまで調べます
		for(;;) {
			c.original = originalSlices( in, ix, c.core) ;
			c.raw = rawSlices( in, ix, c.original) ;
			bool hasContent = false ; // 空白copy判定
			// 元範囲内の非空白rankだけを調べます
			for( const auto &s : c.original) {
				hasContent =
					hasContent ||
					rank( in.original[s.origin].text, ix.original[s.origin], ix.points, s.bytes.end, true) >
						rank( in.original[s.origin].text, ix.original[s.origin], ix.points, s.bytes.begin, true) ;
			}
			// 空白だけならnativeを呼ばず元byteを引継ぎます
			if( !hasContent) {
				c.verbatimWhitespace = true ;
				c.before = {c.core.begin, c.core.begin} ;
				c.after = {c.core.end, c.core.end} ;
				break ;
			}
			c.before = {c.core.begin > leftCap ? c.core.begin - leftCap : 0, c.core.begin} ;
			// 外側の切断点をUTF-8境界へ寄せます
			while( !boundary( ix.tape, c.before.begin)) {
				++c.before.begin ;
			}
			// 内部境界では隣接一scalarを必ず残します
			if( c.before.begin == c.before.end && c.core.begin > 0) {
				--c.before.begin ;
				// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
				while( !boundary( ix.tape, c.before.begin)) {
					--c.before.begin ;
				}
			}
			c.after = {c.core.end, std::min<std::uint64_t>( c.core.end + rightCap, ix.tape.size())} ;
			// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
			while( !boundary( ix.tape, c.after.end)) {
				--c.after.end ;
			}
			// 新しいcore終端のscalar幅が変わっても一scalarを保持します
			if( c.after.begin == c.after.end && c.core.end < ix.tape.size()) {
				static_cast<void>( scalar( ix.tape, c.after.end)) ;
			}
			bool retryCore = false ; // 最小文脈でも不適合ならcore半減
			// 先に文脈を最低一scalarへ縮めます
			for(;;) {
				checkCancelled( stop) ;
				requireWholeTextPlan( ++attempts <= limits.candidatesPerChunk, Diag::PLAN_WORK) ;
				bool fits = false ; // 展開量または実数の採否
				try {
					const auto prompt = materializeWholeTextChunk( in, c, stop) ; // 一候補
					// この同期境界だけで使用する検証値
					const auto rendered = renderWholeTextGeneration( prompt.kind, prompt.system, prompt.user) ;
					requireWholeTextPlan( prompt.system.size() + prompt.user.size() + rendered.size() <=
												 limits.promptBytes &&
											 c.original.size() + c.raw.size() <= limits.promptSlices,
										 Diag::PROJECTION_DENSITY) ;
					// この同期境界だけで使用する検証値
					const auto measured = rendered.size() + prompt.core.size() + prompt.before.size() +
										  prompt.after.size() ; // 実tokenize入力総byte
					plan.measures = add( plan.measures, 1, limits.measureCalls, Diag::PLAN_WORK) ;
					plan.measuredBytes = add( plan.measuredBytes, measured, limits.measuredBytes, Diag::PLAN_WORK) ;
					const auto receipt = counter( prompt) ; // 全prefixを独立計数
					checkCancelled( stop) ;
					validateReceipt( prompt, receipt) ;
					fits = receipt.fits ;
					// 受理した候補だけを封印対象へ残します
					if( fits) {
						c.receipt = receipt ;
					}
				} catch( const OperationError &error) {
					// 展開密度だけは同じ候補列の縮小で処理します
					if( error.code() != static_cast<std::uint32_t>( Diag::PROJECTION_DENSITY)) {
						throw ;
					}
				}
				// 全数とreceiptが成立した候補で停止します
				if( fits) {
					break ;
				}
				// 内側のscalarを残した前文脈
				const auto before = halve( ix.tape, c.before, true) ;
				const auto after = halve( ix.tape, c.after, false) ; // 内側一scalarを保持
				// 文脈が最小なら次はcoreを縮めます
				if( before == c.before && after == c.after) {
					retryCore = true ;
					break ;
				}
				c.before = before ;
				c.after = after ;
				leftCap = before.end - before.begin ;
				rightCap = after.end - after.begin ;
			}
			// 受理済みならcoreを変えません
			if( !retryCore) {
				break ;
			}
			const auto smaller = halve( ix.tape, c.core, false) ; // 非単調性を仮定しない固定半減
			requireWholeTextPlan( smaller != c.core, Diag::PROJECTION_DENSITY) ;
			c.core = smaller ;
		}
		totalSlices = add( totalSlices, c.original.size() + c.raw.size(), limits.primarySlices, Diag::PLAN_BYTES) ;
		encodedBytes = add( encodedBytes, 768 + (c.original.size() + c.raw.size()) * 24, limits.planBytes,
						   Diag::PLAN_BYTES) ; // 全recordの上界を確保前加算
		cursor = c.core.end ;
		plan.chunks.push_back( std::move( c)) ;
	}
	// この同期境界だけで使用する検証値
	const auto generated = std::find_if( plan.chunks.begin(), plan.chunks.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 共通の補助packetを担当できる、生成が必要な最初のchunkを選びます
		 * @param[in] c find_ifが同期参照する確定前のchunk記録です。変更・生成は行いません
		 * @return 空白の無編集copy専用chunkでなければtrue。verbatimWhitespaceがtrueならfalseです
		 */
		//------------------------------------------------------------------------------
		[]( const auto &c)
		{
			return !c.verbatimWhitespace ;
		}) ; // 共通packetの既定owner
	requireWholeTextPlan( generated != plan.chunks.end(), Diag::NO_ORIGINAL_CONTENT) ;
	std::vector<std::uint32_t> firstRaw( in.raw.size(), UINT32_MAX) ; // 元rawの最初のowner
	// raw本文と零長originの一次ownerを記録します
	for( const auto &c : plan.chunks) {
		// 全要素を元の順序で処理し、範囲の終端で停止します
		for( const auto &s : c.raw) {
			firstRaw[s.origin] = std::min( firstRaw[s.origin], c.index) ;
		}
	}
	requireWholeTextPlan( in.canonicalSpans.size() <= limits.packets, Diag::PLAN_BYTES) ;
	// canonical全packetを一つの非空生成coreへ割当てます
	for( const auto &span : in.canonicalSpans) {
		std::uint32_t owner = generated->index ; // 共通/空窓の有限anchor
		// header/token packetは最初のraw担当へ置きます
		if( span.kind == 2 || span.kind == 3) {
			requireWholeTextPlan( span.origin < firstRaw.size(), Diag::COVERAGE) ;
			owner = firstRaw[span.origin] ;
			// 成立する条件だけを処理し、未成立の値は元の状態へ残します
		} else if( span.kind == 1) {
			// この同期境界だけで使用する検証値
			const auto	anchor = static_cast<std::int64_t>( span.anchorSample / 16) ; // 元window開始
			// この同期境界だけで使用する検証値
			auto		found = std::upper_bound( in.original.begin(), in.original.end(), anchor,
				//------------------------------------------------------------------------------
				/**
				 * @brief 補助packetの開始時刻に対応する原文行を選ぶため、開始時刻を比較します
				 * @param[in] v 元録音基準で求めた補助packetの対応開始時刻（ms）です
				 * @param[in] o 同期探索中だけ参照する原文行で、startMsも元録音基準です
				 * @return vがo.startMsより前ならtrue。同時刻または後ならfalseです
				 */
				//------------------------------------------------------------------------------
				[]( auto v, const auto &o)
				{
					return v < o.startMs ;
				}) ;
			// この同期境界だけで使用する検証値
			std::size_t target =
				found == in.original.begin() ? 0 : static_cast<std::size_t>( found - in.original.begin() - 1) ;
			const auto time = in.original[target].startMs ; // 同値は先頭O
			// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
			while( target > 0 && in.original[target - 1].startMs == time) {
				--target ;
			}
			// 全空白anchorは次の非空白行、それがなければ前へ寄せます
			if( ix.original[target].content == 0) {
				// この同期境界だけで使用する検証値
				auto next = target ;
				// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
				while( next < in.original.size() && ix.original[next].content == 0) {
					++next ;
				}
				// 見つかった元originの境界だけを次の割当てへ使います
				if( next < in.original.size()) {
					target = next ;
				} else {
					// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
					while( ix.original[target].content == 0) {
						--target ;
					}
				}
			}
			// この同期境界だけで使用する検証値
			const auto chunk =
				std::find_if( plan.chunks.begin(), plan.chunks.end(),
							 //------------------------------------------------------------------------------
							 /**
							  * @brief 指定originの原文sliceを持つ、生成対象chunkを検索します
							  * @param[in] c find_ifが同期参照するchunk記録です。捕捉したtargetは探す原文origin番号です
							  * @return 空白copy専用でなく、targetと同じoriginの原文sliceを一つ以上持つ場合だけtrueです
							  */
							 //------------------------------------------------------------------------------
							 [&]( const auto &c)
							 {
								 return !c.verbatimWhitespace && std::any_of( c.original.begin(), c.original.end(),
									 //------------------------------------------------------------------------------
									 /**
									  * @brief 補助packetの担当候補chunkに、対応する原文originのsliceがあるか照合します
									  * @param[in] s any_ofが同期参照する原文sliceです。origin番号だけを捕捉したtargetへ照合します
									  * @return s.originがtargetと等しい場合だけtrue。本文・時刻・意味の一致を判定するものではありません
									  */
									 //------------------------------------------------------------------------------
									 [&]( const auto &s)
									 {
										 return s.origin == target ;
									 }) ;
							 }) ;
			requireWholeTextPlan( chunk != plan.chunks.end(), Diag::COVERAGE) ;
			owner = chunk->index ;
		}
		encodedBytes = add( encodedBytes, 56, limits.planBytes, Diag::PLAN_BYTES) ;
		plan.canonical.push_back( {span, owner}) ;
	}
	plan.planSha256 = planDigest( plan, limits.planBytes) ;
	validateWholeTextChunkPlan( in, plan, stop) ;
	return plan ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元byte/ID/receipt/全coverageと封印を照合します
 * @param[in] in 同じowner
 * @param[in] plan 全計画
 * @param[in] stop 停止
 * @throws OperationError 不一致。原文を変更しません
 * @see koefude::validateWholeTextChunkPlan
 */
//------------------------------------------------------------------------------
void validateWholeTextChunkPlan( const WholeTextPlanInputView &in, const WholeTextChunkPlan &plan, std::stop_token stop)
{
	checkCancelled( stop) ;
	requireWholeTextPlan( !plan.chunks.empty() && plan.chunks.size() <= 16384 && plan.measures > 0 &&
							 plan.measures <= 262144 && plan.measuredBytes <= 268435456 &&
							 plan.canonical.size() <= 200004,
						 Diag::PLAN_BYTES) ;
	auto ix = buildIndex( in, {}, stop) ; // 同じ材料を独立再照合
	requireWholeTextPlan( plan.inputBinding == in.inputBinding && plan.originalSha256 == wholeTextSha256( ix.tape) &&
							 plan.auxiliarySha256 == in.auxiliarySha256 && plan.assignments == ix.assignments &&
							 !plan.chunks.empty(),
						 Diag::COVERAGE) ;
	std::uint64_t			   cursor = 0 ; // Oの次byte
	std::vector<std::uint64_t> raw( in.raw.size()) ; // Aごとの次byte
	std::vector<std::uint32_t> empty( in.raw.size()) ; // 零長originの件数
	std::vector<std::uint32_t> firstRaw( in.raw.size(), UINT32_MAX) ; // 最初のraw一次owner
	// この同期境界で所有する値、または確定状態の検証値です
	std::uint64_t			   session = 0, sequence = 0, slices = 0 ;
	// この同期境界で所有する値、または確定状態の検証値です
	std::uint64_t			   acceptedBytes = 0,
				  generatedCount = 0 ; // 受理候補だけの必要最小作業量。 // accepted receiptの同一sessionと順序
	// coreの元順/隙間/重複/receiptを全recordで検証します
	for( std::size_t i = 0; i < plan.chunks.size(); ++i) {
		checkCancelled( stop) ;
		const auto &c = plan.chunks[i] ; // 元順record
		requireWholeTextPlan( c.index == i && c.core.begin == cursor && c.core.begin < c.core.end &&
								 c.core.end <= ix.tape.size() && c.original == originalSlices( in, ix, c.core),
							 Diag::COVERAGE) ;
		requireWholeTextPlan( c.before.end == c.core.begin && c.after.begin == c.core.end &&
								 c.before.begin <= c.before.end && c.after.begin <= c.after.end &&
								 c.after.end <= ix.tape.size(),
							 Diag::COVERAGE) ;
		requireWholeTextPlan( c.core.end - c.core.begin <= 4096 && c.before.end - c.before.begin <= 512 &&
								 c.after.end - c.after.begin <= 512 && boundary( ix.tape, c.before.begin) &&
								 boundary( ix.tape, c.after.end),
							 Diag::COVERAGE) ;
		slices = add( slices, c.original.size() + c.raw.size(), 131072, Diag::PLAN_BYTES) ;
		requireWholeTextPlan( c.raw == rawSlices( in, ix, c.original), Diag::COVERAGE) ;
		// 各rawは元byteの一回だけの昇順被覆に限定します
		for( const auto &s : c.raw) {
			requireWholeTextPlan( s.origin < in.raw.size() && s.bytes.begin == raw[s.origin] &&
									 boundary( in.raw[s.origin].text, s.bytes.begin) &&
									 boundary( in.raw[s.origin].text, s.bytes.end) && s.bytes.begin <= s.bytes.end,
								 Diag::COVERAGE) ;
			raw[s.origin] = s.bytes.end ;
			firstRaw[s.origin] = std::min( firstRaw[s.origin], c.index) ;
			// 空rawは零長originとしてちょうど一回だけ記録します
			if( s.bytes.begin == s.bytes.end) {
				requireWholeTextPlan( in.raw[s.origin].text.empty() && ++empty[s.origin] == 1, Diag::COVERAGE) ;
			}
		}
		bool onlySpace = true ; // Verbatimの独立判定
		// 全core scalarを検査します
		for( auto p = c.core.begin; p < c.core.end;) {
			// 非空白が一つあれば生成対象です
			if( !whitespace( scalar( ix.tape, p))) {
				onlySpace = false ;
			}
		}
		requireWholeTextPlan( c.verbatimWhitespace == onlySpace && c.receipt.has_value() != c.verbatimWhitespace,
							 Diag::COVERAGE) ;
		// 生成recordは同じ全byteとaccepted receiptを必要とします
		if( c.receipt) {
			requireWholeTextPlan( (c.core.begin == 0 || c.before.begin < c.before.end) &&
									 (c.core.end == ix.tape.size() || c.after.begin < c.after.end) &&
									 (session == 0 || session == c.receipt->session) && c.receipt->sequence > sequence,
								 Diag::COUNTER_RECEIPT) ;
			session = c.receipt->session ;
			sequence = c.receipt->sequence ;
			++generatedCount ;
			// prefix/core/前後だけが実tokenizer入力です
			for( std::size_t part = 2; part < 6; ++part) {
				acceptedBytes = add( acceptedBytes, c.receipt->bytes[part], 268435456, Diag::PLAN_WORK) ;
			}
			const auto prompt = materializeWholeTextChunk( in, c, stop) ; // 一候補だけ再作成
			validateReceipt( prompt, *c.receipt) ;
			requireWholeTextPlan( c.receipt->fits, Diag::COUNTER_RECEIPT) ;
		}
		cursor = c.core.end ;
	}
	requireWholeTextPlan( sequence <= plan.measures && generatedCount <= plan.measures &&
							 acceptedBytes <= plan.measuredBytes,
						 Diag::PLAN_WORK) ;
	requireWholeTextPlan( cursor == ix.tape.size() && plan.canonical.size() == in.canonicalSpans.size(), Diag::COVERAGE) ;
	// Aの末尾まで全byteを被覆していることを検査します
	for( std::size_t a = 0; a < in.raw.size(); ++a) {
		requireWholeTextPlan( raw[a] == in.raw[a].text.size() && (!in.raw[a].text.empty() || empty[a] == 1),
							 Diag::COVERAGE) ;
	}
	cursor = 0 ; // canonical物理byte
	// この同期境界だけで使用する検証値
	const auto generated = std::find_if( plan.chunks.begin(), plan.chunks.end(),
		//------------------------------------------------------------------------------
		/**
		 * @brief 共通補助packetの期待ownerとなる、最初の生成対象chunkを検証時に検索します
		 * @param[in] c find_ifが同期参照する保存済み計画のchunk記録です。変更・生成は行いません
		 * @return 空白の無編集copy専用chunkでなければtrue。verbatimWhitespaceがtrueならfalseです
		 */
		//------------------------------------------------------------------------------
		[]( const auto &c)
		{
			return !c.verbatimWhitespace ;
		}) ; // 共通packet owner
	// domainからtrace末尾まで隙間なく一回だけ保持します
	for( std::size_t i = 0; i < plan.canonical.size(); ++i) {
		const auto &c = plan.canonical[i] ; // 元packet
		requireWholeTextPlan( c.span.kind <= 4, Diag::COVERAGE) ;
		requireWholeTextPlan( c.span == in.canonicalSpans[i] && c.span.bytes.begin == cursor &&
								 c.span.bytes.begin < c.span.bytes.end && c.span.bytes.end <= in.auxiliary.size() &&
								 c.chunk < plan.chunks.size() && !plan.chunks[c.chunk].verbatimWhitespace,
							 Diag::COVERAGE) ;
		std::uint32_t expectedOwner = generated->index ; // 共通packet
		// raw header/tokenは最初の担当chunkだけへ置きます
		if( c.span.kind == 2 || c.span.kind == 3) {
			requireWholeTextPlan( c.span.origin < firstRaw.size(), Diag::COVERAGE) ;
			expectedOwner = firstRaw[c.span.origin] ;
			// 成立する条件だけを処理し、未成立の値は元の状態へ残します
		} else if( c.span.kind == 1) {
			// この同期境界だけで使用する検証値
			const auto	anchor = static_cast<std::int64_t>( c.span.anchorSample / 16) ;
			// この同期境界だけで使用する検証値
			auto		found = std::upper_bound( in.original.begin(), in.original.end(), anchor,
				//------------------------------------------------------------------------------
				/**
				 * @brief 補助packetの期待ownerを再計算するため、開始時刻を比較します
				 * @param[in] v 元録音基準で求めた補助packetの対応開始時刻（ms）です
				 * @param[in] o 同期探索中だけ参照する原文行で、startMsも元録音基準です
				 * @return vがo.startMsより前ならtrue。同時刻または後ならfalseです
				 */
				//------------------------------------------------------------------------------
				[]( auto v, const auto &o)
				{
					return v < o.startMs ;
				}) ;
			// この同期境界だけで使用する検証値
			std::size_t target =
				found == in.original.begin() ? 0 : static_cast<std::size_t>( found - in.original.begin() - 1) ;
			// この同期境界だけで使用する検証値
			const auto time = in.original[target].startMs ;
			// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
			while( target > 0 && in.original[target - 1].startMs == time) {
				--target ;
			}
			// 見つかった元originの境界だけを次の割当てへ使います
			if( ix.original[target].content == 0) {
				// この同期境界だけで使用する検証値
				auto next = target ;
				// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
				while( next < in.original.size() && ix.original[next].content == 0) {
					++next ;
				}
				// 見つかった元originの境界だけを次の割当てへ使います
				if( next < in.original.size()) {
					target = next ;
				} else {
					// 元のUTF-8境界または固定したanchor条件を満たす位置まで進めます
					while( ix.original[target].content == 0) {
						--target ;
					}
				}
			}
			// この同期境界だけで使用する検証値
			const auto chunk = std::find_if( plan.chunks.begin(), plan.chunks.end(),
				//------------------------------------------------------------------------------
				/**
				 * @brief 指定originを含む生成対象chunkを再検索し、補助packetの期待ownerを検証します
				 * @param[in] item find_ifが同期参照する計画のchunk記録です。捕捉したtargetは探す原文origin番号です
				 * @return 空白copy専用でなく、targetと同じoriginの原文sliceを一つ以上持つ場合だけtrueです
				 */
				//------------------------------------------------------------------------------
				[&]( const auto &item)
				{
					return !item.verbatimWhitespace &&
						   std::any_of( item.original.begin(), item.original.end(),
									   //------------------------------------------------------------------------------
									   /**
									    * @brief 補助packetの期待owner候補に、対応する原文originのsliceがあるか照合します
									    * @param[in] s any_ofが同期参照する原文sliceです。origin番号だけを捕捉したtargetへ照合します
									    * @return s.originがtargetと等しい場合だけtrue。本文・時刻・意味の一致を判定するものではありません
									    */
									   //------------------------------------------------------------------------------
									   [&]( const auto &s)
									   {
										   return s.origin == target ;
									   }) ;
				}) ;
			requireWholeTextPlan( chunk != plan.chunks.end(), Diag::COVERAGE) ;
			expectedOwner = chunk->index ;
		}
		requireWholeTextPlan( c.chunk == expectedOwner, Diag::COVERAGE) ;
		// token列は元rowの全ordinal件数と照合します
		if( c.span.kind == 3) {
			requireWholeTextPlan( c.span.origin < in.raw.size() && c.span.tokenCount == in.raw[c.span.origin].tokenCount,
								 Diag::COVERAGE) ;
		}
		cursor = c.span.bytes.end ;
	}
	requireWholeTextPlan( cursor == in.auxiliary.size() && plan.planSha256 == planDigest( plan, 8388608), Diag::COVERAGE) ;
}
} // namespace koefude

//------------------------------------------------------------------------------
/**
 * @brief 旧封印と同一byteのpreimageを一つだけ返します
 * @param[in] plan 全計画
 * @return 最大8MiBの同一encoding。受理は行いません
 * @see koefude::detail::wholeTextPlanEncoding
 */
//------------------------------------------------------------------------------
std::string koefude::detail::wholeTextPlanEncoding( const WholeTextChunkPlan &plan)
{
	return planEncoding( plan, 8388608) ;
}

namespace koefude::detail {
//------------------------------------------------------------------------------
/**
 * @brief 全encodingを複製せずstream SHAを計算します
 * @param[in] readAt 有界同期reader
 * @param[in] bytes 全長
 * @param[in] stop 停止
 * @return 全SHA
 * @see koefude::detail::wholeTextStreamSha256
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextStreamSha256( const WholeTextReadAt &readAt, std::uint64_t bytes, std::stop_token stop)
{
	requireWholeTextPlan( readAt && bytes <= 268435456, WholeTextPlanDiagnostic::PLAN_BYTES) ;
	BCRYPT_ALG_HANDLE  algorithm = nullptr ; // 同期hash owner
	BCRYPT_HASH_HANDLE hash = nullptr ; // stream state
	const auto		   check =
		//------------------------------------------------------------------------------
		/**
		 * @brief CNG呼出しの失敗を元のNTSTATUSを保持したOperationErrorへ変換します
		 * @param[in] status 直前のCNG APIのNTSTATUSです。負数だけを失敗として扱います
		 * @throws OperationError statusが負の場合、ErrorKind::INTERNALと元の32bit codeを保持します
		 */
		//------------------------------------------------------------------------------
		[]( NTSTATUS status)
		{
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( status < 0) {
				throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
			}
		} ;
	try {
		check( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
		check( BCryptCreateHash( algorithm, &hash, nullptr, 0, nullptr, 0, 0)) ;
		std::array<std::uint8_t, 65536> block{} ; // 一blockだけ
		// 全byteを元順にhashし、要求範囲外を読みません
		for( std::uint64_t offset = 0; offset < bytes;) {
			checkCancelled( stop) ;
			// この同期境界で所有する値、または確定状態の検証値です
			const auto n = static_cast<std::size_t>( std::min<std::uint64_t>( block.size(), bytes - offset)) ; /* 残量 */
			readAt( offset, std::span( block).first( n)) ;
			check( BCryptHashData( hash, block.data(), static_cast<ULONG>( n), 0)) ;
			offset += n ;
		}
		WholeTextDigest digest{} ; // 固定結果
		check( BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size()), 0)) ;
		BCryptDestroyHash( hash) ;
		BCryptCloseAlgorithmProvider( algorithm, 0) ;
		return digest ;
	} catch( ...) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( hash) {
			BCryptDestroyHash( hash) ;
		}
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( algorithm) {
			BCryptCloseAlgorithmProvider( algorithm, 0) ;
		}
		throw ;
	}
}
namespace {
/** @brief planの固定幅値を一blockから復元します */
class StoredPlanReader {
	public:
	/// 同期reader
	const WholeTextReadAt		   &read ;
	/// 全encoding
	std::uint64_t					size ;
	/// 次byte
	std::uint64_t					offset = 0 ;
	/// 現block先頭
	std::uint64_t					blockAt = 0 ;
	/// 有効byte
	std::size_t						blockSize = 0 ;
	/// indexの確保上界
	std::uint64_t					allocated = 0 ;
	/// 協調停止
	std::stop_token					stop ;
	/// 唯一読取りbuffer
	std::array<std::uint8_t, 65536> block{} ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定readerを構築します
	 * @param[in] r read
	 * @param[in] n 全長
	 * @param[in] token 停止
	 */
	//------------------------------------------------------------------------------
	StoredPlanReader( const WholeTextReadAt &r, std::uint64_t n, std::stop_token token) : read( r), size( n), stop( token)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定byteを読みます
	 * @param[out] out 全域
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<std::uint8_t> out)
	{
		requireWholeTextPlan( offset <= size && out.size() <= size - offset, WholeTextPlanDiagnostic::COVERAGE) ;
		// buffer境界ごとに最大64KiBだけ読みます
		while( !out.empty()) {
			checkCancelled( stop) ;
			// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
			if( blockSize == 0 || offset < blockAt || offset >= blockAt + blockSize) {
				blockAt = offset ;
				blockSize = static_cast<std::size_t>( std::min<std::uint64_t>( block.size(), size - offset)) ;
				read( blockAt, std::span( block).first( blockSize)) ;
			}
			const auto at = static_cast<std::size_t>( offset - blockAt) ; /* buffer内位置 */
			const auto n = std::min( out.size(), blockSize - at) ; /* 今回copy数 */
			std::copy_n( block.begin() + static_cast<std::ptrdiff_t>( at), n, out.begin()) ;
			out = out.subspan( n) ;
			offset += n ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief u64を復元します
	 * @return 元幅値
	 */
	//------------------------------------------------------------------------------
	std::uint64_t number()
	{
		std::array<std::uint8_t, 8> b{} ; /* 固定幅 */
		raw( b) ;
		std::uint64_t n = 0 ; /* 元値 */
		for( unsigned i = 0; i < 8; ++i) {
			n |= static_cast<std::uint64_t>( b[i]) << (i * 8) ;
		}
		return n ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief u32の表現境界を確認します
	 * @return checked値
	 */
	//------------------------------------------------------------------------------
	std::uint32_t u32()
	{
		const auto n = number() ; /* 外部u64 */
		requireWholeTextPlan( n <= UINT32_MAX, WholeTextPlanDiagnostic::COVERAGE) ;
		return static_cast<std::uint32_t>( n) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief boolを復元します
	 * @return 0/1のみ
	 */
	//------------------------------------------------------------------------------
	bool flag()
	{
		const auto n = number() ; /* 外部bool */
		requireWholeTextPlan( n <= 1, WholeTextPlanDiagnostic::COVERAGE) ;
		return n != 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief SHAを復元します
	 * @return raw32
	 */
	//------------------------------------------------------------------------------
	WholeTextDigest digest()
	{
		WholeTextDigest d{} ; /* 固定SHA */
		raw( d) ;
		return d ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 範囲を復元します
	 * @return 半開範囲
	 */
	//------------------------------------------------------------------------------
	WholeTextByteRange range()
	{
		return {number(), number()} ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 確保前に全index上界を計上します
	 * @param[in] maximum 件数上限
	 * @param[in] element 1件memory上界
	 * @param[in] encoded 最低encoding幅
	 * @return 件数
	 */
	//------------------------------------------------------------------------------
	std::size_t count( std::uint64_t maximum, std::uint64_t element, std::uint64_t encoded)
	{
		const auto n = number() ; /* 外部件数 */
		requireWholeTextPlan( n <= maximum && offset <= size && n <= (size - offset) / encoded &&
								 allocated <= 16777216 && n <= (16777216 - allocated) / element,
							 WholeTextPlanDiagnostic::PLAN_BYTES) ;
		allocated += n * element ;
		return static_cast<std::size_t>( n) ;
	}
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 保存planを64KiB以下のreadで復元します
 * @param[in] readAt plan相対reader
 * @param[in] bytes 全長
 * @param[in] expected 期待SHA
 * @param[in] stop 停止
 * @return 16MiB以下の構造検証済みplan。tokenize/生成は行いません
 * @see koefude::detail::decodeStoredWholeTextPlan
 */
//------------------------------------------------------------------------------
WholeTextChunkPlan decodeStoredWholeTextPlan( const WholeTextReadAt &readAt, std::uint64_t bytes,
											 const WholeTextDigest &expected, std::stop_token stop)
{
	requireWholeTextPlan( bytes > 0 && bytes <= 8388608 && wholeTextStreamSha256( readAt, bytes, stop) == expected,
						 WholeTextPlanDiagnostic::COVERAGE) ;
	StoredPlanReader							  r( readAt, bytes, stop) ; // 独立parse、全encoding複製0
	constexpr char								  HASH_DOMAIN[] = "koefude.whole_text_chunk_plan.v2" ; // NUL込み
	std::array<std::uint8_t, sizeof(HASH_DOMAIN)> actual{} ; // 小さい識別子
	r.raw( actual) ;
	requireWholeTextPlan( std::equal( actual.begin(), actual.end(), reinterpret_cast<const std::uint8_t *>( HASH_DOMAIN)),
						 WholeTextPlanDiagnostic::COVERAGE) ;
	WholeTextChunkPlan p ; // 全検証後だけ返します
	p.inputBinding = r.digest() ;
	p.originalSha256 = r.digest() ;
	p.auxiliarySha256 = r.digest() ;
	p.measures = r.number() ;
	p.measuredBytes = r.number() ;
	requireWholeTextPlan( p.measures <= 262144 && p.measuredBytes <= 268435456, WholeTextPlanDiagnostic::PLAN_WORK) ;
	p.chunks.resize( r.count( 16384, sizeof(WholeTextChunkRecord), 80)) ;
	std::uint64_t coreEnd = 0 ; // 連続coverage
	// 全recordを元順に復元しreceiptを再hashします
	for( std::size_t i = 0; i < p.chunks.size(); ++i) {
		auto &c = p.chunks[i] ; /* record */
		c.index = r.u32() ;
		c.core = r.range() ;
		c.before = r.range() ;
		c.after = r.range() ;
		c.verbatimWhitespace = r.flag() ;
		requireWholeTextPlan( c.index == i && c.core.begin == coreEnd && c.core.end > c.core.begin &&
								 c.core.end <= 8388608 && c.before.begin <= c.before.end &&
								 c.before.end == c.core.begin && c.after.begin == c.core.end &&
								 c.after.begin <= c.after.end && c.after.end <= 8388608,
							 WholeTextPlanDiagnostic::COVERAGE) ;
		coreEnd = c.core.end ;
		// 原文とrawのsliceは別々の列として復元します
		for( auto *list : {&c.original, &c.raw}) {
			list->resize( r.count( 131072, sizeof(WholeTextPlanSlice), 24)) ;
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( auto &slice : *list) {
				slice.origin = r.u32() ;
				slice.bytes = r.range() ;
				// 原文は正の範囲、既存U2の空raw originだけは零長を保持します
				requireWholeTextPlan( slice.bytes.begin <= slice.bytes.end &&
										 (list == &c.raw || slice.bytes.begin < slice.bytes.end) &&
										 slice.bytes.end <= 8388608,
									 WholeTextPlanDiagnostic::COVERAGE) ;
			}
		}
		const bool receipt = r.flag() ; // 非空白だけ実計数
		requireWholeTextPlan( receipt != c.verbatimWhitespace, WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( receipt) {
			WholeTextTokenReceipt t ; /* 実計数保存値 */
			t.session = r.number() ;
			t.sequence = r.number() ;
			requireWholeTextPlan( r.number() == 2, WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
			t.prefixTokens = r.u32() ;
			t.coreTokens = r.u32() ;
			t.beforeTokens = r.u32() ;
			t.afterTokens = r.u32() ;
			t.fits = r.flag() ;
			t.completeTokenDigest = r.flag() ;
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( auto &n : t.bytes) {
				n = r.number() ;
			}
			// 定義済み範囲の全要素を順に処理し、終端または停止条件で終了します
			for( auto &sha : t.sha256) {
				sha = r.digest() ;
			}
			t.candidateBinding = r.digest() ;
			t.tokenSha256 = r.digest() ;
			t.receiptSha256 = r.digest() ;
			requireWholeTextPlan( t.session > 0 && t.sequence > 0 && t.prefixTokens > 0 && t.prefixTokens <= 4096 &&
									 t.coreTokens > 0 && t.coreTokens <= 512 && t.beforeTokens <= 128 &&
									 t.afterTokens <= 128 && t.fits && t.completeTokenDigest &&
									 t.tokenSha256 != WholeTextDigest{} && wholeTextReceiptDigest( t) == t.receiptSha256,
								 WholeTextPlanDiagnostic::COUNTER_RECEIPT) ;
			c.receipt = std::move( t) ;
		}
	}
	p.assignments.resize( r.count( 50000, sizeof(WholeTextRawAssignment), 24)) ;
	// 全raw割当てを保存順に復元します
	for( auto &a : p.assignments) {
		a.original = r.u32() ;
		a.knownStart = r.flag() ;
		a.envelopeOverlap = r.flag() ;
	}
	p.canonical.resize( r.count( 524288, sizeof(WholeTextCanonicalOwner), 56)) ;
	std::uint64_t auxEnd = 0 ; // canonical連続終端
	// canonical全byteを一意な生成coreへ束縛します
	for( auto &c : p.canonical) {
		c.span.kind = r.u32() ;
		c.span.origin = r.u32() ;
		c.span.bytes = r.range() ;
		c.span.tokenCount = r.number() ;
		c.span.anchorSample = r.number() ;
		c.chunk = r.u32() ;
		requireWholeTextPlan( c.span.kind <= 4 && c.span.bytes.begin == auxEnd && c.span.bytes.end > auxEnd &&
								 c.span.bytes.end <= 8388608 && c.chunk < p.chunks.size() &&
								 !p.chunks[c.chunk].verbatimWhitespace,
							 WholeTextPlanDiagnostic::COVERAGE) ;
		auxEnd = c.span.bytes.end ;
	}
	// 文章専用の空catalogは空aux/rawの封印だけで復元し、用途は保存境界で再照合します
	requireWholeTextPlan( r.offset == bytes && !p.chunks.empty() &&
							 (!p.canonical.empty() || (p.assignments.empty() && auxEnd == 0 &&
													   p.auxiliarySha256 == wholeTextSha256( std::string_view{}))),
						 WholeTextPlanDiagnostic::COVERAGE) ;
	p.planSha256 = expected ;
	return p ;
}
} // namespace koefude::detail
