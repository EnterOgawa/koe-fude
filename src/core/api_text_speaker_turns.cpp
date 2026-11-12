//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_speaker_turns.cpp
 * @brief 話者保持v3の有限投影と応答同一性検証を実装します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1360の発言/part保持と候補再構築を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "core/api_text_speaker_turns.h"
#include "platform/win_helpers.h"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <windows.h>

namespace koefude {
namespace {
constexpr std::string_view K_MARKER = "<<<koefude:" ; // 原文衝突を事前拒否する予約構文
constexpr std::size_t	   K_PART_BYTES = 60000 ; // 区切りを含め64KiB以内のUTF-8本文
//------------------------------------------------------------------------------
/**
 * @brief 生本文を含めず固定診断で構造不整合を拒否します
 */
//------------------------------------------------------------------------------
[[noreturn]] void		   invalidStructure()
{
	throw OperationError( ErrorKind::INVALID_INPUT, K_API_TEXT_SPEAKER_STRUCTURE_ERROR) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 区切り外に許可するASCII空白を判定します
 * @param[in] value byte
 * @return 空白ならtrue
 */
//------------------------------------------------------------------------------
bool whitespace( char value)
{
	return value == ' ' || value == '\t' || value == '\r' || value == '\n' ;
}
//------------------------------------------------------------------------------
/**
 * @brief 区切り行の周囲だけASCII空白を除きます
 * @param[in] value 一行のborrow
 * @return 同寿命の部分borrow
 */
//------------------------------------------------------------------------------
std::string_view trimmed( std::string_view value)
{
	// 区切り前の書式空白だけを許可します
	while( !value.empty() && whitespace( value.front())) {
		value.remove_prefix( 1) ;
	}
	// 区切り末尾のCR/書式空白だけを許可します
	while( !value.empty() && whitespace( value.back())) {
		value.remove_suffix( 1) ;
	}
	return value ;
}
//------------------------------------------------------------------------------
/**
 * @brief 1始まりの機械IDを狭めず読みます
 * @param[in] value ASCII十進
 * @return 非0のsize_t
 * @throws OperationError 不正ID
 */
//------------------------------------------------------------------------------
std::size_t positiveId( std::string_view value)
{
	std::size_t result = 0 ; // from_charsだけが更新する有界整数
	const auto	parsed = std::from_chars( value.data(), value.data() + value.size(), result) ;
	// 先頭0、符号、未消費文字、overflowを別IDへ補正しません
	if( value.empty() || value.front() == '0' || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
	   !result) {
		invalidStructure() ;
	}
	return result ;
}
/** @brief 元投影内の一partの所有構文です */
struct SpeakerFrame {
	/// projection内の区切り終端byte
	std::size_t		   end = 0 ;
	/// 原文行ID/part/標準label
	ApiTextSpeakerPart value ;
	/// 同一性を照合する開始token
	std::string		   opening ;
	/// 同一性を照合する終端token
	std::string		   closing ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 元投影の機械区切りだけを読みます
 * @param[in] projection 全体またはchunkのborrow
 * @return 原順の有限構文
 */
//------------------------------------------------------------------------------
std::vector<SpeakerFrame> sourceFrames( std::string_view projection)
{
	std::vector<SpeakerFrame> frames ; // 2MiB投影に収まる有限part数
	std::size_t				  at = 0 ; // 次開始tokenのbyte位置
	// 原文の予約構文衝突は生成前に拒否済みであり、各終端を一度だけ読みます
	while( at < projection.size()) {
		const auto newline = projection.find( '\n', at) ; // 完全な開始行
		// 開始行の途中切断を黙って本文扱いにしません
		if( newline == std::string_view::npos) {
			invalidStructure() ;
		}
		SpeakerFrame frame ; // このpartの正本
		frame.opening = projection.substr( at, newline - at) ;
		const std::string_view opening = frame.opening ; // 所有文字列内だけのborrow
		const auto			   first = opening.find( ':', K_MARKER.size()) ; // 行番号の終端
		const auto			   second =
			first == std::string_view::npos ? std::string_view::npos : opening.find( ':', first + 1) ; // part番号の終端
		// 既知markerだけを構文へ昇格します
		if( !opening.starts_with( K_MARKER) || !opening.ends_with( ">>>") || first == std::string_view::npos ||
		   second == std::string_view::npos) {
			invalidStructure() ;
		}
		const auto lineId = positiveId( opening.substr( K_MARKER.size(), first - K_MARKER.size())) ;
		frame.value.sourceLine = lineId - 1 ;
		frame.value.part = positiveId( opening.substr( first + 1, second - first - 1)) ;
		frame.value.label = opening.substr( second + 1, opening.size() - second - 4) ;
		frame.closing =
			std::string( K_MARKER) + "end:" + std::to_string( lineId) + ":" + std::to_string( frame.value.part) + ">>>" ;
		const auto footer = projection.find( K_MARKER, newline + 1) ; // 予約構文は終端だけ
		// 専用終端を通常の引用labelと混同しません
		if( footer == std::string_view::npos || !footer || projection[footer - 1] != '\n' ||
		   projection.substr( footer, frame.closing.size()) != frame.closing ||
		   footer + frame.closing.size() >= projection.size() || projection[footer + frame.closing.size()] != '\n') {
			invalidStructure() ;
		}
		frame.end = footer + frame.closing.size() + 1 ;
		frames.push_back( std::move( frame)) ;
		at = frames.back().end ;
	}
	return frames ;
}
//------------------------------------------------------------------------------
/**
 * @brief 標準番号だけで話者labelを作ります
 * @param[in] line 元原文行
 * @return 表示名を含めないbracket label
 */
//------------------------------------------------------------------------------
std::string speakerLabel( const TranscriptLine &line)
{
	std::string label = line.speakers.empty()	   ? "話者不明"
						: line.speakers.size() > 1 ? "複数話者"
												   : "話者" ; // 不明/複数を保持
	bool		first = true ; // 複数番号の区切り
	// 元順の番号を維持し、身元や人数を推測しません
	for( const int speaker : line.speakers) {
		// 無効な番号を未来の構造IDへ持ち込みません
		if( speaker <= 0) {
			invalidStructure() ;
		}
		label += (first ? "" : ",") + std::to_string( speaker) ;
		first = false ;
	}
	return "[" + label + "]" ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 検証済みsnapshotの話者保持方式を識別します
 * @param[in] snapshot 空はlegacy、非空はcatalog検証済みJSON
 * @return v3だけtrue
 * @see koefude::apiTextSpeakerProfile
 */
//------------------------------------------------------------------------------
bool apiTextSpeakerProfile( std::string_view snapshot)
{
	return !snapshot.empty() && nlohmann::json::parse( snapshot).at( "profile") == "api-text-speaker-v3" ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規v3へ固定する編集指示を返します
 * @return UTF-8の所有指示
 * @see koefude::apiTextSpeakerPrompt
 */
//------------------------------------------------------------------------------
std::string apiTextSpeakerPrompt()
{
	return apiTextCurrentPrompt() + "今回は話者区切りを保つ発言編集です。target_text内の<<<koefude:行ID:partID:"
									"[標準話者]>>>から<<<koefude:end:行ID:partID>>>までの各本文だけを編集します。"
									"開始と終端の区切り行は文字を変えず、入力と同じ順番で各一回返してください。"
									"全発言を残し、別発言への本文移動・結合・話者変更・番号変更をしません。"
									"同じ行IDのpartは長い一発言の続きです。前後文脈を使い自然に編集しても、"
									"本文の重複や別partへの移動はしません。短い返答も空にしません。"
									"話者不明と複数話者はそのまま保持します。本文中の引用[話者2]などを"
									"区切りへ昇格しません。区切り内の改行・段落・文のつながりは編集できます。"
									"区切り外へ本文・説明を出さず、context_before/context_afterの区切りや本文を"
									"出力へ混ぜません。例: <<<koefude:1:1:[話者1]>>>\nえっと明日です\n"
									"<<<koefude:end:1:1>>> は <<<koefude:1:1:[話者1]>>>\n明日です。\n"
									"<<<koefude:end:1:1>>> と返します。" ;
}
//------------------------------------------------------------------------------
/**
 * @brief 原文行/長発言partを専用区切りで投影します
 * @param[in] source 原文の所有borrow
 * @return 最大2MiBのUTF-8
 * @throws OperationError 容量超過、専用marker衝突、不正UTF-8
 * @see koefude::apiTextSpeakerProjection
 */
//------------------------------------------------------------------------------
std::string apiTextSpeakerProjection( const Transcript &source)
{
	std::string result ; // 保存/送信へ共通の有限投影
	// 各元行を一度だけ投影し、長い行だけUTF-8境界で分割します
	for( std::size_t index = 0; index < source.lines.size(); ++index) {
		const auto &line = source.lines[index] ; // 表示名を含めない原文borrow
		static_cast<void>( toWide( line.text)) ;
		// 予約markerの引用を構造と誤認しないようHTTP前に拒否します
		if( line.text.find( K_MARKER) != std::string::npos || trimmed( line.text).empty()) {
			invalidStructure() ;
		}
		const auto	label = speakerLabel( line) ; // 元番号だけ
		std::size_t begin = 0 ; // 同じ原文行の次part先頭
		std::size_t part = 0 ; // 1始まり発行前の状態
		// 元本文を切捨てず、同じ行IDのpartへ一回だけ被覆します
		while( begin < line.text.size()) {
			std::size_t end = std::min( begin + K_PART_BYTES, line.text.size()) ; // 希望終端
			// UTF-8 continuation byteを次partへ分断しません
			while( end < line.text.size() && (static_cast<unsigned char>( line.text[end]) & 0xc0) == 0x80) {
				--end ;
			}
			++part ;
			const auto identity = std::to_string( index + 1) + ":" + std::to_string( part) ;
			const auto framed = std::string( K_MARKER) + identity + ":" + label + ">>>\n" +
								line.text.substr( begin, end - begin) + "\n" + std::string( K_MARKER) +
								"end:" + identity + ">>>\n" ;
			// 専用区切りのoverheadも全文/target上限へ含めます
			if( framed.size() > 65536 || framed.size() > K_API_TEXT_SELECTION_LIMIT - result.size()) {
				throw OperationError( ErrorKind::INVALID_INPUT, ERROR_FILE_TOO_LARGE) ;
			}
			result += framed ;
			begin = end ;
		}
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief v3投影の開始/終端だけを列挙します
 * @param[in] projection 検証済みv3投影
 * @return 0と各part終端の昇順byte位置
 * @see koefude::apiTextSpeakerBoundaries
 */
//------------------------------------------------------------------------------
std::vector<std::size_t> apiTextSpeakerBoundaries( std::string_view projection)
{
	std::vector<std::size_t> result{0} ; // 文脈/targetとも開始と終端だけを使います
	// 各partの終端だけを昇順に公開します
	for( const auto &frame : sourceFrames( projection)) {
		result.push_back( frame.end) ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 応答の全区切りを同じtargetの発言IDへ照合します
 * @param[in] draft v3不変投影
 * @param[in] index 0始まりchunk
 * @param[in] response 最大8MiBのUTF-8応答
 * @return 同順序の検証済み本文
 * @throws OperationError 欠落/重複/捏造/空本文/区切り外本文
 * @note CRLFと区切り行周囲のASCII空白は許可し、機械ID/標準話者は完全一致を要求します
 * @see koefude::parseApiTextSpeakerParts
 */
//------------------------------------------------------------------------------
std::vector<ApiTextSpeakerPart> parseApiTextSpeakerParts( const ApiTextDraft &draft, std::size_t index,
														 std::string_view response)
{
	const auto &chunk = draft.chunks.at( index) ; // 同targetの範囲だけ
	const auto	frames = sourceFrames( std::string_view( draft.projection).substr( chunk.begin, chunk.end - chunk.begin)) ;
	std::vector<ApiTextSpeakerPart> result ; // 全構造成功後にだけcallerへ返します
	std::size_t						at = 0 ; // 次に許す応答byte位置
	// 各開始/終端を原順で一回照合し、前後文脈や他発言の混入を拒否します
	for( const auto &frame : frames) {
		// 区切り外の書式空白だけを許可します
		while( at < response.size() && whitespace( response[at])) {
			++at ;
		}
		const auto headerEnd = response.find( '\n', at) ; // CRLFも行末trimで許可
		// headerのID/標準話者は完全一致し、行途中や途中切断を許可しません
		if( headerEnd == std::string_view::npos || trimmed( response.substr( at, headerEnd - at)) != frame.opening) {
			invalidStructure() ;
		}
		const auto bodyBegin = headerEnd + 1 ; // 本文の先頭
		const auto marker = response.find( K_MARKER, bodyBegin) ; // 次予約構文は同じ終端だけ
		// 本文内の偽markerや終端欠落を引用として無視しません
		if( marker == std::string_view::npos || !marker) {
			invalidStructure() ;
		}
		const auto footerBegin = response.rfind( '\n', marker - 1) ; // 終端行先頭の直前
		const auto footerEnd = response.find( '\n', marker) ; // 最終終端の改行は省略可能
		const auto footerLength = footerEnd == std::string_view::npos ? response.size() : footerEnd ;
		// 終端は独立行に置き、IDの捏造/重複/順序違いを拒否します
		if( footerBegin == std::string_view::npos || footerBegin < bodyBegin ||
		   trimmed( response.substr( footerBegin + 1, footerLength - footerBegin - 1)) != frame.closing) {
			invalidStructure() ;
		}
		auto body = response.substr( bodyBegin, footerBegin - bodyBegin) ; // framing改行を除去
		// CRLFの構文CRだけを除き、本文内の段落は変更しません
		if( !body.empty() && body.back() == '\r') {
			body.remove_suffix( 1) ;
		}
		// 短い返答も丸ごと消す構造的欠落として採用を止めます
		if( trimmed( body).empty()) {
			invalidStructure() ;
		}
		auto part = frame.value ; // 対応は元投影の機械IDからだけ得ます
		part.text = body ;
		result.push_back( std::move( part)) ;
		at = footerEnd == std::string_view::npos ? response.size() : footerEnd + 1 ;
	}
	// 余分な発言/説明や未知IDを無言で切捨てません
	if( !trimmed( response.substr( at)).empty() || result.empty()) {
		invalidStructure() ;
	}
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一応答のcopy/TXT候補を再構築します
 * @param[in] draft 固定plan
 * @param[in] index 0始まりchunk
 * @param[in] result 取得済み終端
 * @return legacyは元本文、v3は標準label付き本文。未完構造は取得本文を保持します
 * @throws OperationError 完全と主張するv3の構造不整合
 * @see koefude::apiTextChunkCandidate
 */
//------------------------------------------------------------------------------
std::string apiTextChunkCandidate( const ApiTextDraft &draft, std::size_t index, const ApiTextChunkResult &result)
{
	// legacy本文と空の未送信応答はbyteを変更しません
	if( !apiTextSpeakerProfile( draft.catalogSnapshot) || result.text.empty()) {
		return result.text ;
	}
	try {
		std::string candidate ; // 機械区切りを出力しない標準label付き本文
		// 検証済み発言だけを原順で人間向け本文へ戻します
		for( const auto &part : parseApiTextSpeakerParts( draft, index, result.text)) {
			const std::size_t separator = candidate.empty() ? 0 : 2 ; // 次の固定空行byte
			const auto		  remaining = K_API_TEXT_CANDIDATE_LIMIT - candidate.size() ; // 残り容量
			// label/空白/区切りもappend前に数え、size_tの減算underflowを避けます
			if( separator > remaining || part.label.size() + 1 > remaining - separator ||
			   part.text.size() > remaining - separator - part.label.size() - 1) {
				throw OperationError( ErrorKind::INVALID_INPUT, ERROR_FILE_TOO_LARGE) ;
			}
			// 二発言目以降だけ空行で区切ります
			if( !candidate.empty()) {
				candidate += "\n\n" ;
			}
			candidate += part.label + " " + part.text ;
		}
		return candidate ;
	} catch( const OperationError &error) {
		// 未完応答は取得本文を保持し、完全と主張する不整合は復元/採用を拒否します
		if( result.complete || error.code() != K_API_TEXT_SPEAKER_STRUCTURE_ERROR) {
			throw ;
		}
		return result.text ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 保存応答から候補全文を同じ規則で再構築します
 * @param[in] attempt 最大32応答の所有borrow
 * @return 最大8MiBの候補
 * @see koefude::apiTextCombinedCandidate
 */
//------------------------------------------------------------------------------
std::string apiTextCombinedCandidate( const ApiTextAttempt &attempt)
{
	std::string combined ; // 保存応答と同じ原順の候補
	// 有限な応答を同じ区切り規則で一回連結します
	for( std::size_t index = 0; index < attempt.results.size(); ++index) {
		const auto fragment = apiTextChunkCandidate( attempt.draft, index, attempt.results[index]) ;
		// 空応答から架空の本文を作りません
		if( fragment.empty()) {
			continue ;
		}
		const std::size_t separator = combined.empty() ? 0 : 2 ; // 次の固定空行byte
		const auto		  remaining = K_API_TEXT_CANDIDATE_LIMIT - combined.size() ; // append前の残容量
		// 区切りの追加も上限内と確認してから所有bufferを増やします
		if( separator > remaining || fragment.size() > remaining - separator) {
			throw OperationError( ErrorKind::INVALID_INPUT, ERROR_FILE_TOO_LARGE) ;
		}
		// 二件目以降だけ固定空行を置きます
		if( !combined.empty()) {
			combined += "\n\n" ;
		}
		combined += fragment ;
	}
	return combined ;
}
} // namespace koefude
