//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file text_context_protection.cpp
 * @brief 原文と候補の限定保護spanを有界streamで照合します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343のsurface-v1保護検査を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1424の既知の行頭空白だけを整形
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 字面検査は未知名・主体関係・意味追加を全検出する保証ではありません
 */
//------------------------------------------------------------------------------
#include "core/whole_text_bounded_result.h"
#include "core/whole_text_correction.h"
#include <span>
#include <utility>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string_view>
namespace koefude {
namespace {
// 最長一致で同じ表現を二重計数しない固定policy表です
constexpr std::array<std::string_view, 20> K_CRITICAL = {"できません",	   "しなければならない",
														 "してはいけない", "できない",
														 "ではない",	   "ではありません",
														 "ません",		   "ない",
														 "不要",		   "禁止",
														 "必須",		   "義務",
														 "同意",		   "拒否",
														 "約束",		   "承諾",
														 "合意",		   "了承",
														 "断る",		   "必要"} ;
// 数字列の元byte表記を保ち、全角をASCIIへ正規化しません
constexpr std::array<std::string_view, 10> K_WIDE_DIGITS = {"０", "１", "２", "３", "４", "５", "６", "７", "８", "９"} ;
// 漢数字単独を数値にせず、後続の明示単位を必要とします
constexpr std::array<std::string_view, 16> K_KANJI_DIGITS = {"〇", "零", "一", "二", "三", "四", "五", "六",
															 "七", "八", "九", "十", "百", "千", "万", "億"} ;
// v1で保護する隣接単位・通貨だけの有限表です
constexpr std::array<std::string_view, 42> K_UNITS = {
	"GiB", "MiB", "GB", "MB", "kg", "cm", "mm", "km", "ms", "Hz",	"円",	  "年",		"月", "日",
	"時",  "分",  "秒", "人", "名", "個", "回", "本", "台", "件",	"枚",	  "歳",		"割", "％",
	"%",   "度",  "倍", "階", "号", "期", "週", "点", "組", "ドル", "ユーロ", "ポンド", "元", "ウォン"} ;
//------------------------------------------------------------------------------
/**
 * @brief 表の最長prefixだけを選びます
 * @param[in] text 同期borrow
 * @param[in] choices 有限表
 * @return 一致したbyte、なければ空
 */
//------------------------------------------------------------------------------
template <std::size_t N> std::string_view longest( std::string_view text, const std::array<std::string_view, N> &choices)
{
	std::string_view found ; // 二重計数しない一つの最長表現
	// 固定表を終端まで照合し、同じ位置の最長一致を選びます
	for( const auto value : choices) {
		// 短い別表現より長い完全一致を優先します
		if( value.size() > found.size() && text.starts_with( value)) {
			found = value ;
		}
	}
	return found ;
}
//------------------------------------------------------------------------------
/**
 * @brief 検証済みUTF-8の先頭scalar長を返します
 * @param[in] text 非空UTF-8
 * @return 先頭scalarのbyte数
 */
//------------------------------------------------------------------------------
std::size_t scalarBytes( std::string_view text)
{
	const auto first = static_cast<unsigned char>( text.front()) ; // UTF-8先頭byte
	return first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4 ;
}
//------------------------------------------------------------------------------
/**
 * @brief ASCII又は全角数字の先頭長です
 * @param[in] text 非空又は空のborrow
 * @return 数字ならbyte長、他は0
 */
//------------------------------------------------------------------------------
std::size_t digitBytes( std::string_view text)
{
	// 終端での数値継続を安全に止めます
	if( text.empty()) {
		return 0 ;
	}
	return text.front() >= '0' && text.front() <= '9' ? 1 : longest( text, K_WIDE_DIGITS).size() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 数字・符号・隣接単位の有限spanを抽出します
 * @param[in] text 最大512byte以上のlookahead又は終端
 * @return 数値span長、他は0
 */
//------------------------------------------------------------------------------
std::size_t numericBytes( std::string_view text)
{
	std::size_t position = 0 ; // 当該span内のbyte位置
	const auto	sign =
		longest( text, std::array<std::string_view, 8>{"+", "-", "＋", "－", "¥", "￥", "$", "＄"}) ; // 直前符号だけ
	position += sign.size() ;
	const bool arabic = digitBytes( text.substr( position)) != 0 ; // 漢数字との区別
	// 数字列を元byteのまま消費し、異常長は保守的に拒否します
	while( position < text.size()) {
		const auto digit =
			arabic ? digitBytes( text.substr( position)) : longest( text.substr( position), K_KANJI_DIGITS).size() ;
		// 次が数字でなければ列の終端です
		if( digit == 0) {
			break ;
		}
		position += digit ;
		requireWholeTextBounded( position < 256, WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED) ;
		const auto separator =
			longest( text.substr( position), std::array<std::string_view, 8>{".", ",", ":", "/", "．", "，", "：", "／"}) ;
		// 小数・日付・時刻の区切りは後続数字がある場合だけ同じspanに含めます
		if( arabic && !separator.empty() && digitBytes( text.substr( position + separator.size())) != 0) {
			position += separator.size() ;
		}
	}
	// 符号だけや普通の語を数値へ変えません
	if( position == sign.size()) {
		return 0 ;
	}
	const auto unit = longest( text.substr( position), K_UNITS) ; // 明示的に隣接した単位
	return arabic || !unit.empty() ? position + unit.size() : 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 敬称直前へ保持できる文字種を判定します
 * @param[in] text scalar一つ
 * @return 漢字1/カタカナ2/Latin3、対象外0
 */
//------------------------------------------------------------------------------
unsigned nameGroup( std::string_view text)
{
	std::uint32_t value = 0 ; // 当該scalarだけのUnicode値
	const auto	  first = static_cast<unsigned char>( text.front()) ; // UTF-8先頭
	const auto	  size = scalarBytes( text) ; // 一scalarだけを分類します
	value = first & (size == 1 ? 0x7Fu : size == 2 ? 0x1Fu : size == 3 ? 0x0Fu : 0x07u) ;
	// 継続byteを一scalarへ復号し、全文のUnicode配列は保持しません
	for( std::size_t index = 1; index < size; ++index) {
		value = (value << 6) | (static_cast<unsigned char>( text[index]) & 0x3Fu) ;
	}
	// ひらがなの接続語を氏名へ含めず、抽出可能な有限tokenだけを選びます
	if( value >= 0x4E00 && value <= 0x9FFF) {
		return 1 ;
	}
	// カタカナ長音も同じtokenへ含めます
	if( value >= 0x30A0 && value <= 0x30FF) {
		return 2 ;
	}
	return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ? 3u : 0u ;
}
//------------------------------------------------------------------------------
/**
 * @brief 入出力quota内のspan列だけをstream抽出します
 * @param[in] bytes 全byte数
 * @param[in] read 最大65536byteの同期充填
 * @param[in] names 明示話者名
 * @param[in] stop 停止
 * @return 出現順の保護span列
 */
//------------------------------------------------------------------------------
std::vector<std::string> protectedSpans( std::uint64_t bytes, const TextContextReader &read,
										const std::map<int, std::string> &names, std::stop_token stop)
{
	requireWholeTextBounded( bytes <= 8388608, WholeTextBoundedDiagnostic::AGGREGATE_BYTES, ErrorKind::RESULT_TOO_LARGE) ;
	std::vector<std::string>		result ; // 全保持byteは元input quota以下に制限
	std::array<std::uint8_t, 65536> block{} ; // 一回のread上限
	std::string						pending ; // 最大64KiBと512byteのlookaheadだけ
	std::string						preceding ; // 敬称直前の有限span、最大96byte
	unsigned						precedingGroup = 0 ; // 漢字/カタカナ/Latinの連続token種別
	std::uint64_t					offset = 0 ; // 入力全体の既読byte
	std::uint64_t					retained = 0 ; // 二重抽出を含む保持quota
	// 入力終端まで有限blockで読み、境界の語を次blockへ持ち越します
	while( offset < bytes || !pending.empty()) {
		checkCancelled( stop) ;
		const auto size = static_cast<std::size_t>( std::min<std::uint64_t>( block.size(), bytes - offset)) ;
		// 終端の残りlookaheadだけは新しいreadなしで検査します
		if( size != 0) {
			read( offset, std::span( block).first( size)) ;
			pending.append( reinterpret_cast<const char *>( block.data()), size) ;
			offset += size ;
		}
		const auto	limit = offset == bytes ? pending.size() : pending.size() - 512 ; // 未確定末尾
		std::size_t position = 0 ; // current block内の確定位置
		// lookaheadを残し、数値/保護語のbyte境界を一度だけ処理します
		while( position < limit) {
			const auto tail = std::string_view( pending).substr( position) ; // 同期borrow
			const auto numeric = numericBytes( tail) ; // 数値列全体
			const auto critical = longest( tail, K_CRITICAL) ; // 固定表の最長一致
			const auto honorific =
				longest( tail, std::array<std::string_view, 6>{"さん", "様", "氏", "先生", "殿", "君"}) ;
			std::string span ; // 当該位置の保護表現だけ
			std::size_t consumed = scalarBytes( tail) ; // 通常は一scalarだけ進みます
			// 数値を同一列として保護し、内部の数字を再計数しません
			if( numeric != 0) {
				consumed = numeric ;
				span = "N:" + std::string( tail.substr( 0, numeric)) ;
			} else if( !critical.empty()) { // 最長語の内部を再計数しません
				consumed = critical.size() ;
				span = "C:" + std::string( critical) ;
			} else if( !honorific.empty() && !preceding.empty()) { // 明示敬称直前だけを保護
				span = "H:" + preceding + std::string( honorific) ;
			}
			std::size_t nameOffset = 0 ; // 消費span内の全scalar開始位置
			// 明示話者名は数値/重要語の内側から始まる場合も全出現を保持します
			while( nameOffset < consumed) {
				const auto nameTail = tail.substr( nameOffset) ; // 同期borrow
				// 登録済み表示名だけを有限表として照合します
				for( const auto &[speaker, name] : names) {
					static_cast<void>( speaker) ; // file内番号は生成本文へ付けません
					// 空名と過大な名前を無制限lookaheadへ使いません
					if( !name.empty() && name.size() <= 512 && nameTail.starts_with( name)) {
						result.push_back( "S:" + name) ;
						retained += name.size() + 2 ;
					}
				}
				nameOffset += scalarBytes( nameTail) ;
			}
			// 保護表現がない通常の文字は結果へ複製しません
			if( !span.empty()) {
				retained += span.size() ;
				result.push_back( std::move( span)) ;
			}
			requireWholeTextBounded( retained <= 16777216 && result.size() <= 1048576,
									WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED, ErrorKind::RESULT_TOO_LARGE) ;
			std::size_t scanned = 0 ; // 消費したspan内のscalar位置
			// 各scalarの文字種で連続tokenを限定し、接続語を名前へ取り込みません
			while( scanned < consumed) {
				const auto current = tail.substr( scanned, scalarBytes( tail.substr( scanned))) ;
				const auto group = nameGroup( current) ; // 当該scalarの有限種別
				// 文字種変更・区切り・ひらがなで直前名候補をリセットします
				if( group == 0 || group != precedingGroup) {
					preceding.clear() ;
				}
				precedingGroup = group ;
				// 抽出できる文字種だけを敬称前tokenへ保持します
				if( group != 0) {
					preceding.append( current) ;
					// UTF-8 scalar境界を保って有限な直前範囲だけ残します
					while( preceding.size() > 96) {
						preceding.erase( 0, scalarBytes( preceding)) ;
					}
				}
				scanned += current.size() ;
			}
			position += consumed ;
		}
		pending.erase( 0, position) ;
	}
	return result ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 原文と候補の限定保護spanを出現順・字面で比較します
 * @param[in] source 不変原文と明示話者名
 * @param[in] bytes 候補の全byte数
 * @param[in] read 最大64KiBの同期読取り
 * @param[in] stop 協調停止
 * @return 表面保護spanが一致すればtrue。意味保存の保証ではありません
 * @throws OperationError quota/読取り失敗/停止。数値等の異常長spanも拒否します
 * @see koefude::textContextProtectedSpansMatch
 */
//------------------------------------------------------------------------------
bool textContextProtectedSpansMatch( const Transcript &source, std::uint64_t bytes, const TextContextReader &read,
									std::stop_token stop)
{
	std::uint64_t originalBytes = 0 ; // 無区切り原文tapeの全長
	// 元行を元順に数え、音声時刻を新しい観測に使いません
	for( const auto &line : source.lines) {
		originalBytes += line.text.size() ;
	}
	const TextContextReader original =
		//------------------------------------------------------------------------------
		/**
		 * @brief 原文owner内の無区切りtapeを指定byteだけ読みます
		 * @param[in] offset 全tape位置
		 * @param[out] out 全指定byteを充填する同期borrow
		 */
		//------------------------------------------------------------------------------
		[&source]( std::uint64_t offset, std::span<std::uint8_t> out)
		{
			// 全行を元順に走査し、指定範囲が満たされた時だけ終了します
			for( const auto &line : source.lines) {
				// 指定範囲の前にある行は読む必要がありません
				if( offset >= line.text.size()) {
					offset -= line.text.size() ;
					continue ;
				}
				const auto size = std::min( out.size(), line.text.size() - static_cast<std::size_t>( offset)) ;
				std::memcpy( out.data(), line.text.data() + offset, size) ;
				out = out.subspan( size) ;
				offset = 0 ;
				// 全指定byteを充填した時点で終了します
				if( out.empty()) {
					return ;
				}
			}
			requireWholeTextBounded( out.empty(), WholeTextBoundedDiagnostic::PROTECTED_SPAN_CHANGED) ;
		} ;
	return protectedSpans( originalBytes, original, source.speakerNames, stop) ==
		   protectedSpans( bytes, read, source.speakerNames, stop) ;
}
} // namespace koefude
