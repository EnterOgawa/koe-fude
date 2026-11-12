//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_bounded_result.cpp
 * @brief 固定LE receiptと全文へ適用しない成功条件を照合します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1310のFinal1536・派生予算と限定検証へ整合
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "core/whole_text_bounded_result.h"
#include <algorithm>
#include <array>

#include <string>
namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief LE整数を固定bufferへ追加します
 * @param[in,out] out encoding
 * @param[in] value 非負値
 * @param[in] bytes byte幅
 */
//------------------------------------------------------------------------------
void number( std::string &out, std::uint64_t value, unsigned bytes)
{
	// 指定したbyte幅だけを小端順で追加します
	for( unsigned index = 0; index < bytes; ++index) { // 出力ordinal
		out.push_back( static_cast<char>( value >> (index * 8))) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief SHAの全32byteを追加します
 * @param[in,out] out encoding
 * @param[in] d 全SHA
 */
//------------------------------------------------------------------------------
void digest( std::string &out, const WholeTextDigest &d)
{
	out.append( reinterpret_cast<const char *>( d.data()), d.size()) ; // 値byteだけの同期境界
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief bounded条件を本文なし診断で拒否します
 * @param[in] valid 条件
 * @param[in] diagnostic 固定分類
 * @param[in] kind 失敗種別
 * @see koefude::requireWholeTextBounded
 */
//------------------------------------------------------------------------------
void requireWholeTextBounded( bool valid, WholeTextBoundedDiagnostic diagnostic, ErrorKind kind)
{
	// 不整合を部分成功として採用しません
	if( !valid) {
		throw OperationError( kind, static_cast<std::uint32_t>( diagnostic)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 成功生成receiptの固定LE encodingをhashします
 * @param[in] r 全値
 * @return 固定SHA
 * @see koefude::wholeTextGeneratedReceiptDigest
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextGeneratedReceiptDigest( const WholeTextBoundedChunkReceipt &r)
{
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_chunk_generated.v2" ; // 終端NUL込み
	std::string	   out( HASH_DOMAIN, sizeof(HASH_DOMAIN)) ; // 最大320byte程度のencoding
	number( out, r.session, 8) ;
	number( out, r.index, 4) ;
	digest( out, r.planSha256) ;
	digest( out, r.inputSha256) ;
	number( out, r.core.begin, 8) ;
	number( out, r.core.end, 8) ;
	digest( out, r.countSha256) ;
	number( out, r.prefixTokens, 4) ;
	number( out, r.renderedBytes, 8) ;
	digest( out, r.tokenSha256) ;
	digest( out, r.renderedSha256) ;
	number( out, r.naturalEog ? 1 : 0, 4) ;
	number( out, r.analysisTokens, 4) ;
	number( out, r.closingTokens, 4) ;
	number( out, r.finalTokens, 4) ;
	number( out, r.bodyBytes, 4) ;
	digest( out, r.bodySha256) ;
	return wholeTextSha256( out) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 無編集copyの固定LE encodingをhashします
 * @param[in] input 入力SHA
 * @param[in] plan 計画SHA
 * @param[in] index 元順
 * @param[in] core 元範囲
 * @param[in] body 元byte
 * @return copy証拠SHA
 * @see koefude::wholeTextVerbatimReceiptDigest
 */
//------------------------------------------------------------------------------
WholeTextDigest wholeTextVerbatimReceiptDigest( const WholeTextDigest &input, const WholeTextDigest &plan,
											   std::uint32_t index, WholeTextByteRange core, std::string_view body)
{
	constexpr char HASH_DOMAIN[] = "koefude.whole_text_verbatim_receipt.v2" ; // 固定domain
	std::string	   out( HASH_DOMAIN, sizeof(HASH_DOMAIN)) ; // copyはtoken計測ではありません
	digest( out, input) ;
	digest( out, plan) ;
	number( out, index, 4) ;
	number( out, core.begin, 8) ;
	number( out, core.end, 8) ;
	number( out, body.size(), 8) ;
	digest( out, wholeTextSha256( body)) ;
	return wholeTextSha256( out) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 生成本文のUTF8/NUL/markerと実counterを照合します
 * @param[in] r 全成功値
 * @param[in] body 同期借用本文
 * @param[in] kind 計数receiptを照合する生成方式です。Plainはanalysis/closingを0、Gemmaは個別の上限へ照合し、未知方式は拒否します
 * @see koefude::validateWholeTextGeneratedReceipt
 */
//------------------------------------------------------------------------------
void validateWholeTextGeneratedReceipt( const WholeTextBoundedChunkReceipt &r, std::string_view body,
									   WholeTextGenerationKind kind)
{
	// 既存のUTF8判定は全scalar/NULを厳密に検査します
	const bool textValid = !body.empty() && body.size() <= 65536 && body.find( '\0') == body.npos &&
						   body.find( "<|") == body.npos && body.find( "<channel|>") == body.npos &&
						   body.find( "<bos>") == body.npos && body.find( "<eos>") == body.npos ;
	bool nonblank = false ; // 全scalarを検査し空白だけのFinalを拒否
	// 元byteを変更せずUTF8全域のscalar境界を検査します
	for( std::size_t offset = 0; offset < body.size();) { // 次のscalar byte
		const auto	  lead = static_cast<unsigned char>( body[offset++]) ; // 先頭byte
		std::uint32_t point = lead ; // 復号値
		unsigned	  count = 0 ; // 継続数
		std::uint32_t minimum = 0 ; // overlong下限
		// 正式な2/3/4byte先頭だけを受け付けます
		if( lead >= 0xc2 && lead <= 0xdf) {
			count = 1 ;
			point &= 0x1f ;
			minimum = 0x80 ;
		}
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		else if( lead >= 0xe0 && lead <= 0xef) {
			count = 2 ;
			point &= 0xf ;
			minimum = 0x800 ;
		}
		// 不整合や終了状態を分岐させ、未確定のまま後続へ進みません
		else if( lead >= 0xf0 && lead <= 0xf4) {
			count = 3 ;
			point &= 7 ;
			minimum = 0x10000 ;
		} else {
			requireWholeTextBounded( lead < 0x80, WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		}
		requireWholeTextBounded( count <= body.size() - offset, WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		// 宣言した継続byteだけを消費します
		for( unsigned n = 0; n < count; ++n) { // 継続ordinal
			const auto unit = static_cast<unsigned char>( body[offset++]) ; // 継続byte
			requireWholeTextBounded( (unit & 0xc0) == 0x80, WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
			point = (point << 6) | (unit & 0x3f) ;
		}
		requireWholeTextBounded( point >= minimum && point > 0 && point <= 0x10ffff &&
									!(point >= 0xd800 && point <= 0xdfff),
								WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
		const bool blank = (point >= 9 && point <= 13) || (point >= 0x1c && point <= 0x20) || point == 0x85 ||
						   point == 0xa0 || point == 0x1680 || (point >= 0x2000 && point <= 0x200a) ||
						   point == 0x2028 || point == 0x2029 || point == 0x202f || point == 0x205f ||
						   point == 0x3000 || point == 0xfeff ; // 固定集合
		nonblank = nonblank || !blank ;
	}
	requireWholeTextBounded( textValid && nonblank && r.naturalEog && r.session != 0 && r.core.begin < r.core.end &&
								r.prefixTokens > 0 && r.prefixTokens <= 4096 &&
								(kind == WholeTextGenerationKind::MINISTRAL_PLAIN
									 ? (r.analysisTokens == 0 && r.closingTokens == 0)
									 : (kind == WholeTextGenerationKind::GEMMA_THOUGHT_FINAL && r.analysisTokens > 0 &&
										r.analysisTokens <= 1279 && r.closingTokens <= 1 &&
										r.analysisTokens + r.closingTokens + r.finalTokens <= 2816 &&
										(r.closingTokens == 0 || r.analysisTokens == 1279))) &&
								r.finalTokens >= 2 && r.finalTokens <= 1536 && r.bodyBytes == body.size() &&
								r.bodySha256 == wholeTextSha256( body) &&
								r.receiptSha256 == wholeTextGeneratedReceiptDigest( r),
							WholeTextBoundedDiagnostic::CHUNK_RECEIPT) ;
}
} // namespace koefude
