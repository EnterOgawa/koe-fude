//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_tokens.cpp
 * @brief CTCの本文を保持したまま、有効なtoken時刻だけを対応付けます
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 UTF-8境界、frame時刻、原文完全一致の検証を追加
 * 0.1.0 Shunji Ogawa 2026/09/10 発火位置を明示し、単語境界の確認前の分割を禁止
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
#include "core/asr_tokens.h"

#include <span>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief sherpaが明示したbyte fallbackだけを一つのbyteへ戻します
 * @param[in] piece 同期呼出し中に有効なtoken表記
 * @return 対応byteまたは元のUTF-8 tokenを所有する文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string decodePiece( const std::string_view piece)
{
	// 特定の完全表記だけを復号し、一般本文の括弧等は書き換えない
	if( piece.size() == 6 && piece.starts_with( "<0x") && piece.back() == '>') {
		unsigned int value = 0 ; // 検証してからbyteへ変換する値
		const auto	 parsed = std::from_chars( piece.data() + 3, piece.data() + 5, value, 16) ; // 全2桁
		// 全2桁を正常に読めた場合だけ元byteとして扱う
		if( parsed.ec == std::errc{} && parsed.ptr == piece.data() + 5 && value <= 255) {
			return std::string( 1, static_cast<char>( value)) ;
		}
	}
	return std::string( piece) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief CTCのtoken列が本文に完全一致した場合だけ、元音声の時刻を付けます
 * @param[in,out] segment 所有本文と元音声区間。既存tokensを消し、成功時だけ全対応を設定
 * @param[in] pieces 呼出し中だけ借用するUTF-8 token。ASCIIのbyte fallback表記を許可
 * @param[in] startsSeconds piecesと同じ件数の区間相対開始秒。非負・有限・単調順
 * @param[in] frameMs 固定モデルの発火frame幅（ms）。1～1000
 * @return 本文と全tokenが対応したらtrue。不一致なら本文を保ちtokensを空にしてfalse
 * @throws OperationError 不正な時刻・件数・上限。失敗時も本文と区間時刻は変更しません
 * @note 時刻はframeの発火位置であり、音素の厳密な継続時間を保証しません
 * 成功時はtoken_times_are_anchorsをtrueにし、全tokenのcan_split_afterをfalseにします
 * 話者交代で分割するには、別途単語等の境界と照合して分割許可を設定します
 * @see koefude::bindCtcTokens
 */
//------------------------------------------------------------------------------
bool bindCtcTokens( TextSegment &segment, const std::span<const std::string_view> pieces,
				   const std::span<const float> startsSeconds, const std::int64_t frameMs)
{
	segment.tokens.clear() ;
	segment.tokenTimesAreAnchors = false ;
	// API表現とframe幅を検証し、部分的な対応結果を残さない
	if( segment.startMs < 0 || segment.endMs <= segment.startMs || segment.endMs > K_MAXIMUM_TIMELINE_MS ||
	   pieces.size() != startsSeconds.size() || frameMs < 1 || frameMs > 1000) {
		throw OperationError( ErrorKind::INVALID_TIMELINE) ;
	}
	// tokenと本文の上限を、作業用copyを確保する前に照合する
	if( pieces.size() > K_MAXIMUM_ASR_TOKENS || segment.text.size() > K_MAXIMUM_TEXT_BYTES) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	std::string				  joined ; // tokenの元byteを順に戻した所有本文
	std::vector<std::size_t>  ends ; // 復号した各tokenの本文byte終端
	std::vector<std::int64_t> starts ; // 検証済みの元音声開始ms
	ends.reserve( pieces.size()) ;
	starts.reserve( pieces.size()) ;
	float	   previous = 0 ; // 外部float時刻の単調性を確認する前値
	const auto durationMs = segment.endMs - segment.startMs ; // 有界なASR入力範囲
	// 全tokenを検証し、本文との完全一致を確認してから対応結果を作る
	for( std::size_t index = 0; index < pieces.size(); ++index) { // 現在のtoken番号
		const float seconds = startsSeconds[index] ; // C APIからcopyした相対秒
		// 非有限・逆順・範囲外の時刻を整数へcastする前に拒否する
		if( !std::isfinite( seconds) || seconds < previous ||
		   static_cast<double>( seconds) * 1000 >= static_cast<double>( durationMs)) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		previous = seconds ;
		starts.push_back( segment.startMs + static_cast<std::int64_t>( std::llround( seconds * 1000.0))) ;
		const auto decoded = decodePiece( pieces[index]) ; // 元byteへ復号した有界token
		// 外部tokenをつないだ作業用本文も無制限に増やさない
		if( decoded.size() > K_MAXIMUM_TEXT_BYTES - joined.size()) {
			throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
		}
		joined += decoded ;
		ends.push_back( joined.size()) ;
	}
	const std::size_t leading =
		!joined.empty() && joined.front() == ' ' && std::string_view( joined).substr( 1) == segment.text
			? 1
			: 0 ; // sherpaが本文先頭だけで取り除く一つのASCII空白
	// 本文変換や未知のtoken形式が介在した場合は区間全体の対応を維持する
	if( std::string_view( joined).substr( leading) != segment.text || pieces.empty()) {
		return false ;
	}
	std::vector<TextToken> tokens ; // 全検証成功後だけ親DTOへ移す対応情報
	tokens.reserve( pieces.size()) ;
	std::size_t	 textBegin = 0 ; // 次に保持する文字の先頭byte
	std::int64_t groupStart = -1 ; // UTF-8を複数byte tokenで表すときの最初のframe
	// UTF-8を途中で分けず、同じ文字のbyte tokenを一つの対応範囲へまとめる
	for( std::size_t index = 0; index < pieces.size(); ++index) { // 現在の対応token
		const auto textEnd = ends[index] > leading ? ends[index] - leading : 0 ; // 補正済み終端
		// 先頭の除去済み空白や空tokenに本文の時刻を割り当てない
		if( textEnd == textBegin) {
			continue ;
		}
		// 一つのUTF-8文字を構成する最初のtoken時刻だけを保持する
		if( groupStart < 0) {
			groupStart = starts[index] ;
		}
		// 次のbyteが同じUTF-8文字の続きなら、文字の終端まで待つ
		if( textEnd < segment.text.size() && (static_cast<unsigned char>( segment.text[textEnd]) & 0xc0U) == 0x80U) {
			continue ;
		}
		const auto endMs = std::min( segment.endMs, starts[index] + frameMs) ; // 末尾だけclip
		// 丸め後にも正の音声範囲を保持できない時刻は使わない
		if( endMs <= groupStart) {
			throw OperationError( ErrorKind::INVALID_TIMELINE) ;
		}
		tokens.push_back( {groupStart, endMs, textEnd, false}) ;
		textBegin = textEnd ;
		groupStart = -1 ;
	}
	// 完全な本文を対応付けた結果だけを所有DTOへ移す
	if( textBegin != segment.text.size()) {
		return false ;
	}
	segment.tokens = std::move( tokens) ;
	segment.tokenTimesAreAnchors = true ;
	return true ;
}

} // namespace koefude
