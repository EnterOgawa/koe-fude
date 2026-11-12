//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file comparison_alignment.h
 * @brief 旧保存本文の表示位置だけを有界な文字列特徴で対応させます
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1364の高確度原順anchorと未対応fallbackを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note worker専用です。保存本文・話者・時刻・意味の同一性を変更/確定しません
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/pipeline.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace koefude {
/** @brief 保存の発言IDとは独立した、表示だけの1:1位置です */
struct ComparisonPositionPair {
	/// 元groupの0始まり番号
	std::size_t left = 0 ;
	/// 改善blockの0始まり番号
	std::size_t right = 0 ;
} ;
/** @brief 位置対応探索の有限結果です。上限時は全文未対応で表示します */
struct ComparisonAlignment {
	/// 両列で単調増加する高確度位置だけ
	std::vector<ComparisonPositionPair> pairs ;
	/// 処理量上限により対応を全て保留した状態
	bool								limited = false ;
	/// 実施した候補特徴比較数
	std::size_t							comparisons = 0 ;
} ;
namespace comparison_detail {
constexpr std::size_t K_MAXIMUM_BLOCKS = 8192 ; // 各列の解析対象数
constexpr std::size_t K_MAXIMUM_UNITS = 2 * 1024 * 1024 ; // 両列で走査するUTF-16 code unit数
constexpr std::size_t K_MAXIMUM_BLOCK_UNITS = 32768 ; // これを超えるblockは無編集で未対応
constexpr std::size_t K_MAXIMUM_FEATURES = 262144 ; // 長会話の両列を含むindex特徴の総数
constexpr std::size_t K_FEATURES_PER_BLOCK = 128 ; // 低hash値の固定上限
constexpr std::size_t K_MAXIMUM_POSTINGS = 8 ; // 頻出gramを候補生成から除く閾値
constexpr std::size_t K_MAXIMUM_CANDIDATES = 32 ; // 一つの元blockからの比較上限
constexpr std::size_t K_MAXIMUM_COMPARISONS = 16384 ; // 全比較での候補評価上限
/** @brief 表示本文と独立した照合用の特徴です */
struct Features {
	/// 空白/句読点だけを除いた有界本文
	std::wstring			   normalized ;
	/// 数字列の変更を位置一致へ昇格させない固定照合値
	std::wstring			   numbers ;
	/// 重複なしのbottom-hash 4-gram
	std::vector<std::uint64_t> grams ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 本文照合時にだけ無視する空白/句読点を判定します
 * @param[in] c UTF-16 code unit
 * @return 表示位置照合から除外する場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline bool separator( wchar_t c)
{
	return c <= L' ' || c == L'　' || c == L'。' || c == L'、' || c == L'，' || c == L'．' || c == L',' || c == L'.' ||
		   c == L'!' || c == L'?' || c == L'！' || c == L'？' || c == L'「' || c == L'」' || c == L'『' || c == L'』' ||
		   c == L'"' || c == L'\'' ;
}
//------------------------------------------------------------------------------
/**
 * @brief 全文を切らず、上限内のblockだけから照合用特徴を作ります
 * @param[in] body 無編集の表示本文
 * @param[in] stop 協調取消し
 * @return 短文/巨大blockでは空特徴。保存本文へは反映しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline Features features( std::wstring_view body, std::stop_token stop)
{
	Features result ; // 比較専用buffer
	// 巨大blockをprefixだけで誤対応させず、全体を未対応に残します
	if( body.size() > K_MAXIMUM_BLOCK_UNITS) {
		return result ;
	}
	bool inNumber = false ; // raw本文のASCII数字列境界。空白/小数点も区切りです
	// 文字内容はそのままにし、見た目の空白/句読点だけを別bufferで除きます
	for( std::size_t at = 0; at < body.size(); ++at) {
		// 長い文字列にも有限間隔で取消しを反映します
		if( at % 1024 == 0) {
			checkCancelled( stop) ;
		}
		// 空白や句読点を除く前の数字列を保持し、1 2/12や1.5/15を同一化しません
		if( body[at] >= L'0' && body[at] <= L'9') {
			result.numbers.push_back( body[at]) ;
			inNumber = true ;
		} else if( inNumber) { // rawの区切りは照合値にも残します
			result.numbers.push_back( L'|') ;
			inNumber = false ;
		}
		// 位置照合用の正規化だけであり、表示本文は一字も変更しません
		if( !separator( body[at])) {
			result.normalized.push_back( body[at]) ;
		}
	}
	// 短い返答は出現順/話者だけで対応させません
	if( result.normalized.size() < 24) {
		return result ;
	}
	// 終端数字列の区切りも中間と同じ形式へそろえます
	if( inNumber) {
		result.numbers.push_back( L'|') ;
	}
	// 4文字特徴を一回ずつ準備します
	for( std::size_t at = 0; at < result.normalized.size(); ++at) {
		// 終端3文字は完全な4-gramを持ちません
		if( at + 4 > result.normalized.size()) {
			continue ;
		}
		std::uint64_t hash = 14695981039346656037ULL ; // 固定64bit FNV-1a
		// 4 code unit全体をhashし、位置変化によらない候補特徴にします
		for( std::size_t unit = 0; unit < 4; ++unit) {
			hash ^= static_cast<std::uint16_t>( result.normalized[at + unit]) ;
			hash *= 1099511628211ULL ;
		}
		result.grams.push_back( hash) ;
	}
	std::sort( result.grams.begin(), result.grams.end()) ;
	result.grams.erase( std::unique( result.grams.begin(), result.grams.end()), result.grams.end()) ;
	// 多様性のない長い反復を、特徴的な長文と扱いません
	if( result.grams.size() < 8) {
		result.grams.clear() ;
	} else if( result.grams.size() > K_FEATURES_PER_BLOCK) { // 全blockで同じbottom-hashを使います
		result.grams.resize( K_FEATURES_PER_BLOCK) ;
	}
	return result ;
}
/** @brief 相互一位と曖昧さを保持する候補状態です */
struct Best {
	/// 最高得点の相手
	std::size_t index = 0 ;
	/// 0..10000の特徴一致度
	std::size_t first = 0 ;
	/// 二位。同点は曖昧として拒否
	std::size_t second = 0 ;
	/// 強い包含候補数。merge/splitを保留する根拠
	std::size_t contained = 0 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 上位二件を保持します
	 * @param[in] other 相手番号
	 * @param[in] score 正の特徴一致度
	 */
	//------------------------------------------------------------------------------
	void		add( std::size_t other, std::size_t score)
	{
		// 一位が変わったときだけ相手番号を更新します
		if( score > first) {
			second = first ;
			first = score ;
			index = other ;
		} else { // 同点も二位に残し、反復を任意に選びません
			second = std::max( second, score) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定閾値と二位との差を満たすか調べます
	 * @return 高確度ならtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool confident() const
	{
		return first >= 8000 && first - second >= 1200 && contained <= 1 ;
	}
} ;
} // namespace comparison_detail

//------------------------------------------------------------------------------
/**
 * @brief 旧本文の高確度1:1表示位置だけを有界indexから抽出します
 * @param[in] left 元group本文
 * @param[in] right 保存labelで区切った改善本文
 * @param[in] stop workerの協調取消し
 * @return 原順位置と処理量。上限時pairsは空
 * @note 話者/時刻を入力にせず、短文/重複/数字不一致/交差/包含競合を未対応へ残します
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ComparisonAlignment alignComparisonText( std::span<const std::wstring> left,
															 std::span<const std::wstring> right, std::stop_token stop)
{
	using comparison_detail::Best ;
	using comparison_detail::Features ;
	ComparisonAlignment result ; // 上限時にも本文の表示は呼出元が保全します
	checkCancelled( stop) ;
	// 巨大なblock表へ無制限な作業配列を確保しません
	if( left.size() > comparison_detail::K_MAXIMUM_BLOCKS || right.size() > comparison_detail::K_MAXIMUM_BLOCKS) {
		result.limited = true ;
		return result ;
	}
	std::size_t units = 0 ; // 走査予定の総code unit数
	// allocation前に全本文の走査量を検証します
	for( const auto column : {left, right}) {
		// 各block本文を一度だけ容量へ含めます
		for( const auto &block : column) {
			checkCancelled( stop) ;
			// 上限時はprefix対応も作らず、色付き全文を未対応に保ちます
			if( block.size() > comparison_detail::K_MAXIMUM_UNITS - units) {
				result.limited = true ;
				return result ;
			}
			units += block.size() ;
		}
	}
	std::vector<Features> a ; // 元本文の有限特徴
	std::vector<Features> b ; // 改善本文の有限特徴
	std::size_t			  features = 0 ; // indexのentry上限
	// 両列の特徴を作り、全文のNxM走査を避けます
	for( const auto &side : {std::pair{left, &a}, std::pair{right, &b}}) {
		side.second->reserve( side.first.size()) ;
		// 一blockごとに取消し/特徴上限を確認します
		for( const auto &block : side.first) {
			checkCancelled( stop) ;
			auto value = comparison_detail::features( block, stop) ; // 別bufferのみ
			features += value.grams.size() ;
			// index容量超過は対応なしへ戻し、全文描画を続けます
			if( features > comparison_detail::K_MAXIMUM_FEATURES) {
				result.limited = true ;
				return result ;
			}
			side.second->push_back( std::move( value)) ;
		}
	}
	std::unordered_map<std::uint64_t, std::vector<std::size_t>> index ; // rare gramの転置index
	// 改善側の各特徴は各blockに最大一回だけ登録します
	for( std::size_t at = 0; at < b.size(); ++at) {
		checkCancelled( stop) ;
		// 頻出gramは閾値+1で打ち切り、巨大postingを作りません
		for( const auto gram : b[at].grams) {
			auto &posting = index[gram] ; // 高々9個の相手番号
			// 閾値を超えた特徴は後で候補生成に使用しません
			if( posting.size() <= comparison_detail::K_MAXIMUM_POSTINGS) {
				posting.push_back( at) ;
			}
		}
	}
	std::vector<Best> bestA( a.size()) ; // 元側の相互一位検証
	std::vector<Best> bestB( b.size()) ; // 改善側の相互一位検証
	// 元blockごとにrare gramを持つ相手だけを検証します
	for( std::size_t at = 0; at < a.size(); ++at) {
		checkCancelled( stop) ;
		std::unordered_map<std::size_t, std::size_t> votes ; // 最大128*8件
		// rare gramごとの候補出現数を有界に集計します
		for( const auto gram : a[at].grams) {
			const auto found = index.find( gram) ; // 改善側の実在特徴のみ
			// 頻出/未出現gramは発言の識別根拠にしません
			if( found == index.end() || found->second.size() > comparison_detail::K_MAXIMUM_POSTINGS) {
				continue ;
			}
			// 同じ相手に共通する独立特徴数を数えます
			for( const auto other : found->second) {
				++votes[other] ;
			}
		}
		std::vector<std::size_t> candidates ; // 得票4以上の有限候補
		// 反復的な低得票は高価な特徴比較へ進めません
		for( const auto &[other, count] : votes) {
			// 十分な共有特徴がある候補だけを評価します
			if( count >= 4) {
				candidates.push_back( other) ;
			}
		}
		// 候補爆発を勝手な先着選択へ変えず全対応を保留します
		if( candidates.size() > comparison_detail::K_MAXIMUM_CANDIDATES) {
			result.limited = true ;
			return result ;
		}
		std::sort( candidates.begin(), candidates.end()) ; // 実装/環境に依存しない評価順
		// 最大32候補の特徴を交差走査します
		for( const auto other : candidates) {
			checkCancelled( stop) ;
			// 全体処理量を超えた後に部分anchorを採用しません
			if( ++result.comparisons > comparison_detail::K_MAXIMUM_COMPARISONS) {
				result.limited = true ;
				result.comparisons = comparison_detail::K_MAXIMUM_COMPARISONS ;
				return result ;
			}
			const auto &x = a[at] ; // 有界な元特徴
			const auto &y = b[other] ; // 同じ有界な改善特徴
			std::size_t common = 0 ; // 重複なしの共有特徴数
			std::size_t i = 0 ; // 元特徴の走査位置
			std::size_t j = 0 ; // 改善特徴の走査位置
			// sort済みの高々128特徴ずつを線形に照合します
			while( i < x.grams.size() && j < y.grams.size()) {
				// 一致した特徴は両側で一回だけ数えます
				if( x.grams[i] == y.grams[j]) {
					++common ;
					++i ;
					++j ;
				} else if( x.grams[i] < y.grams[j]) { // 元側の未共有特徴
					++i ;
				} else { // 改善側の未共有特徴
					++j ;
				}
			}
			// 小さいblockの大半を含む相手が複数あればmerge/splitを保留します
			if( common * 100 >= std::min( x.grams.size(), y.grams.size()) * 80) {
				++bestA[at].contained ;
				++bestB[other].contained ;
			}
			// 数字不一致/長さ差は近似した別発言や結合の可能性を残します
			if( x.numbers != y.numbers || std::min( x.normalized.size(), y.normalized.size()) * 100 <
											 std::max( x.normalized.size(), y.normalized.size()) * 80) {
				continue ;
			}
			const auto score = common * 20000 / (x.grams.size() + y.grams.size()) ; // Dice係数
			bestA[at].add( other, score) ;
			bestB[other].add( at, score) ;
		}
	}
	std::vector<ComparisonPositionPair> possible ; // 交差順序を除く前の相互一位
	// 両側で十分な差がある場合だけ1:1の表示位置候補にします
	for( std::size_t at = 0; at < bestA.size(); ++at) {
		const auto &best = bestA[at] ; // 元側の上位状態
		// 同じ文章の反復や片方向の類似だけを対応としません
		if( best.confident() && bestB[best.index].confident() && bestB[best.index].index == at) {
			possible.push_back( {at, best.index}) ;
		}
	}
	std::vector<std::size_t> suffix( possible.size() + 1,
									std::numeric_limits<std::size_t>::max()) ; // 後方最小番号
	// 交差した二候補のどちらかを恣意的に選ばず両方を保留します
	for( std::size_t at = possible.size(); at > 0; --at) {
		suffix[at - 1] = std::min( suffix[at], possible[at - 1].right) ;
	}
	std::size_t previous = 0 ; // それ以前の最大番号+1
	// 元順/改善順の両方を満たす位置だけを返します
	for( std::size_t at = 0; at < possible.size(); ++at) {
		checkCancelled( stop) ;
		const auto pair = possible[at] ; // 候補は元順に一回だけ処理
		// crossing/逆順の候補を未対応に保ちます
		if( pair.right >= previous && pair.right < suffix[at + 1]) {
			result.pairs.push_back( pair) ;
		}
		previous = std::max( previous, pair.right + 1) ;
	}
	return result ;
}
} // namespace koefude
