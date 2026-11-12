//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file generator.cpp
 * @brief UTF-8 rendererと自然EOGだけを採用する有限生成を実装します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 renderer・二段階proof・生成と解放を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1284の生成しない計数と有限計画の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1285の有界逐次補正を接続
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1308のV2未完2枝だけ診断を区別
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1310のFinal1536・派生予算と限定検証へ整合
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
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
#include "generator.h"
#include <span>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace koefude::whole_text {
namespace {
std::atomic<bool>		   counterSessionActive = false ; // 全session中のV1排他
std::atomic_flag		   generationActive = ATOMIC_FLAG_INIT ; // 待機せず同時要求を拒否
constexpr std::string_view K_OPENING = "<|channel>thought\n" ; // 固定raw開始
constexpr std::string_view K_CLOSING = "<channel|>" ; // 唯一のlane境界
//------------------------------------------------------------------------------
/**
 * @brief 条件不成立を固定診断へ変換します
 * @param[in] valid 満たすべきnative入力/状態条件。falseなら指定失敗を送出します
 * @param[in] diagnostic 条件不成立時に保存する固定KoefudeWtDiagnostic
 * @param[in] status 条件不成立時のKoefudeWtStatus。省略時はINVALID_INPUTです
 * @throws NativeWholeTextError valid=falseのとき、指定status/diagnosticで送出します。trueなら状態を変更しません
 */
//------------------------------------------------------------------------------
void					   requireNative( const bool valid, const KoefudeWtDiagnostic diagnostic,
										 const KoefudeWtStatus status = KoefudeWtStatus::INVALID_INPUT)
{
	// 部分採用せず全体拒否する
	if( !valid) {
		throw NativeWholeTextError{status, diagnostic} ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief Python str.stripのUnicode集合を判定します
 * @param[in] point Unicode scalar
 * @return strip対象ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool stripPoint( const std::uint32_t point) noexcept
{
	return (point >= 9 && point <= 13) || (point >= 0x1c && point <= 0x20) || point == 0x85 || point == 0xa0 ||
		   point == 0x1680 || (point >= 0x2000 && point <= 0x200a) || point == 0x2028 || point == 0x2029 ||
		   point == 0x202f || point == 0x205f || point == 0x3000 ;
}
//------------------------------------------------------------------------------
/**
 * @brief UTF-8全体を厳密検証し、再encodeせず両端byteだけ除きます
 * @param[in] text NUL不可の全byte
 * @param[in,out] hasBody null可。非空白scalarを見つけるとtrueにする呼出し中の借用
 * @return 元byteのtrim借用
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string_view checkedStrip( const std::string_view text, bool *hasBody = nullptr)
{
	std::size_t first = text.size() ; // 最初の非strip byte
	std::size_t last = 0 ; // 最後の非strip scalarの直後
	// 全scalarを一度検査し、不正UTF-8の見逃しと内部byteの変更を避ける
	for( std::size_t offset = 0; offset < text.size();) { // 検査中のbyte位置
		const auto	  start = offset ; // 現scalarの開始
		const auto	  lead = static_cast<unsigned char>( text[offset++]) ; // 先頭byte
		std::uint32_t point = lead ; // 復号中のscalar
		std::uint32_t count = 0 ; // 継続byte数
		std::uint32_t minimum = 0 ; // overlong拒否の最小値
		// 多byte先頭だけを定義済み範囲で受け付ける
		if( lead >= 0xc2 && lead <= 0xdf) {
			count = 1 ;
			point &= 0x1f ;
			minimum = 0x80 ;
		}
		// 3byte scalarを検証する
		else if( lead >= 0xe0 && lead <= 0xef) {
			count = 2 ;
			point &= 0x0f ;
			minimum = 0x800 ;
		}
		// 4byte scalarを検証する
		else if( lead >= 0xf0 && lead <= 0xf4) {
			count = 3 ;
			point &= 7 ;
			minimum = 0x10000 ;
		} else {
			requireNative( lead < 0x80, KoefudeWtDiagnostic::UTF8) ;
		}
		requireNative( count <= text.size() - offset, KoefudeWtDiagnostic::UTF8) ;
		// 定義された継続byteを上限内で消費する
		for( std::uint32_t index = 0; index < count; ++index) { // 継続番号
			const auto unit = static_cast<unsigned char>( text[offset++]) ; // 継続byte
			requireNative( (unit & 0xc0) == 0x80, KoefudeWtDiagnostic::UTF8) ;
			point = (point << 6) | (unit & 0x3f) ;
		}
		requireNative( point >= minimum && point != 0 && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff),
					  KoefudeWtDiagnostic::UTF8) ;
		// Finalの空白判定だけはFEFFも除き、byte集合による別scalarの誤拒否を避ける
		if( hasBody && !stripPoint( point) && point != 0xfeff) {
			*hasBody = true ;
		}
		// FEFFを含む非strip scalarの元byte境界を保持する
		if( !stripPoint( point)) {
			first = std::min( first, start) ;
			last = offset ;
		}
	}
	return last == 0 ? text.substr( 0, 0) : text.substr( first, last - first) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 研究と同じ予約tagの出現を検査します
 * @param[in] text 検証済みbyte
 * @return 禁止tagがなければtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool noTags( const std::string_view text) noexcept
{
	return text.find( "<|") == text.npos && text.find( "<channel|>") == text.npos && text.find( "<bos>") == text.npos &&
		   text.find( "<eos>") == text.npos ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同期callの停止を問い合わせます
 * @param[in] request callback借用
 * @return 停止時true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool stopped( const KoefudeWtRequestV1 &request)
{
	return request.shouldStop && request.shouldStop( request.callbackData) != 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 協調取消しを全体失敗へ変換します
 * @param[in] request callback借用
 */
//------------------------------------------------------------------------------
void checkStop( const KoefudeWtRequestV1 &request)
{
	requireNative( !stopped( request), KoefudeWtDiagnostic::CANCELLED, KoefudeWtStatus::CANCELLED) ;
}
//------------------------------------------------------------------------------
/**
 * @brief workerで実処理の段階を通知します
 * @param[in] request callback借用
 * @param[in] stage 固定段階
 * @param[in] completed 実完了数
 * @param[in] total 総数、0は不定
 */
//------------------------------------------------------------------------------
void notify( const KoefudeWtRequestV1 &request, const std::uint32_t stage, const std::uint32_t completed = 0,
			const std::uint32_t total = 0)
{
	// hostのcallback例外はhost内で捕捉され、停止returnで解放を要求する
	if( request.progress && request.progress( request.callbackData, stage, completed, total) != 0) {
		throw NativeWholeTextError{KoefudeWtStatus::CANCELLED, KoefudeWtDiagnostic::CALLBACK_FAILURE} ;
	}
	checkStop( request) ;
}
//------------------------------------------------------------------------------
/**
 * @brief decodeの元符号を保持し、前後の停止も確認します
 * @param[in,out] backend 同期engine
 * @param[in] request callback借用
 * @param[in] tokens returnまで有効な借用
 */
//------------------------------------------------------------------------------
void commit( NativeWholeTextBackend &backend, const KoefudeWtRequestV1 &request,
			const std::span<const std::int32_t> tokens)
{
	checkStop( request) ;
	const auto code = backend.decode( tokens) ; // 元の符号付きdecode値
	// decode異常では停止の有無を保持し、2以外を取消しに置き換えない
	if( code != 0) {
		throw NativeWholeTextError{code == 2 && stopped( request) ? KoefudeWtStatus::CANCELLED
																 : KoefudeWtStatus::INFERENCE_FAILED,
								   KoefudeWtDiagnostic::DECODE, code} ;
	}
	checkStop( request) ;
}
//------------------------------------------------------------------------------
/**
 * @brief prefixを有限bufferで一度tokenizeし完全往復を照合します
 * @param[in,out] backend live vocab
 * @param[in] prefix 全byte
 * @return token所有列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::int32_t> encodePrefix( NativeWholeTextBackend &backend, const std::string_view prefix)
{
	std::array<std::int32_t, 4097> buffer{} ; // 拡張再試行しない固定buffer
	const auto					   count = backend.tokenize( prefix, buffer) ; // 符号を変えず先に範囲確認
	requireNative( count > 0 && count <= 4096, KoefudeWtDiagnostic::PREFIX_TOKENS,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	std::vector<std::int32_t> result( buffer.begin(), buffer.begin() + count) ; // 所有prefix
	requireNative( backend.detokenize( result, K_MAXIMUM_BYTES) == prefix, KoefudeWtDiagnostic::FRAMING) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 3markerの単一IDと復号byteを照合します
 * @param[in,out] backend live vocab
 * @return opening/closing/think ID
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::array<std::int32_t, 3> encodeMarkers( NativeWholeTextBackend &backend)
{
	constexpr std::array<std::string_view, 3> MARKERS{"<|channel>", "<channel|>", "<|think|>"} ; // 固定順
	std::array<std::int32_t, 3>				  result{} ; // 同じ順のID
	// 各markerを独立に一回だけtokenizeする
	for( std::size_t index = 0; index < MARKERS.size(); ++index) { // marker番号
		std::array<std::int32_t, 2> token{} ; // 二個以上を拒否するbuffer
		requireNative( backend.tokenize( MARKERS[index], token) == 1 && token[0] >= 0 &&
						  token[0] < static_cast<std::int32_t>( K_VOCABULARY_TOKENS),
					  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
		result[index] = token[0] ;
		requireNative( !backend.isEog( token[0]) &&
						  backend.detokenize( std::span( token).first( 1), K_MAXIMUM_BYTES) == MARKERS[index],
					  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
	}
	requireNative( result[1] == 101 && result[0] != result[1] && result[0] != result[2] && result[1] != result[2],
				  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief Analysisを復号し固定openingと非空laneを検証します
 * @param[in,out] backend live vocab
 * @param[in] tokens opening込み、closingなし
 * @return opening込みの復号byte
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string analysisBytes( NativeWholeTextBackend &backend, const std::span<const std::int32_t> tokens)
{
	auto bytes = backend.detokenize( tokens, K_MAXIMUM_BYTES) ; // laneの独立復号
	static_cast<void>( checkedStrip( bytes)) ;
	requireNative( bytes.starts_with( K_OPENING) && bytes.size() > K_OPENING.size(), KoefudeWtDiagnostic::FRAMING) ;
	requireNative( noTags( std::string_view( bytes).substr( K_OPENING.size())), KoefudeWtDiagnostic::FRAMING) ;
	return bytes ;
}
//------------------------------------------------------------------------------
/**
 * @brief 採用前のFinal全byteを検証します
 * @param[in] body 復号byte
 * @param[in] maximum host残量
 */
//------------------------------------------------------------------------------
void validateBody( const std::string_view body, const std::uint32_t maximum)
{
	requireNative( body.size() <= maximum, KoefudeWtDiagnostic::OUTPUT_BYTES, KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	bool hasBody = false ; // 有効な非空白scalarの有無
	static_cast<void>( checkedStrip( body, &hasBody)) ;
	requireNative( hasBody && noTags( body), KoefudeWtDiagnostic::FRAMING) ;
}
/** @brief 取得済みguardだけをscope終了で解放します */
struct GenerationGuard {
	//------------------------------------------------------------------------------
	/**
	 * @brief 同時要求を待機せず拒否します
	 * @param[in] permitCounter trueは同じ計数sessionからの逐次生成を許可し、falseはlive計数sessionとの競合も拒否します。生成自体の同時実行は両方で拒否します
	 * @throws NativeWholeTextError 既存sessionまたは同時生成と競合した場合はBUSYです。競合検査で得たflagは必要時に戻します
	 */
	//------------------------------------------------------------------------------
	explicit GenerationGuard( bool permitCounter = false)
	{
		requireNative( permitCounter || !counterSessionActive.load(), KoefudeWtDiagnostic::BUSY, KoefudeWtStatus::BUSY) ;
		requireNative( !generationActive.test_and_set(), KoefudeWtDiagnostic::BUSY, KoefudeWtStatus::BUSY) ;
		// flag取得直前に始まった計数sessionも拒否します
		if( !permitCounter && counterSessionActive.load()) {
			generationActive.clear() ;
			throw NativeWholeTextError{KoefudeWtStatus::BUSY, KoefudeWtDiagnostic::BUSY} ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 次要求が同じkernelを再利用できるようにします
	 */
	//------------------------------------------------------------------------------
	~GenerationGuard()
	{
		generationActive.clear() ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ固定laneをmodel再利用の一chunkで実行します
 * @param[in,out] backend live tensor
 * @param[in] request callback値
 * @param[in] tokens 全prefix
 * @param[in] markers 固定marker
 * @param[in] maximumBodyBytes host残量
 * @return 完全Final
 */
//------------------------------------------------------------------------------
NativeWholeTextGenerated generateSerialLane( NativeWholeTextBackend &backend, const KoefudeWtRequestV1 &request,
											const std::vector<std::int32_t>	  &tokens,
											const std::array<std::int32_t, 3> &markers, std::uint32_t maximumBodyBytes)
{
	backend.openContext() ;
	checkStop( request) ;
	requireNative( backend.contextTokens() == 6912 && tokens.size() + 2816 <= 6912, KoefudeWtDiagnostic::CONTEXT,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	backend.openSampler() ;
	// 全prefixを実際の投入数から512以下に分割し、一度ずつcommitする
	for( std::size_t offset = 0; offset < tokens.size();) { // 投入済みtoken数
		const auto count = std::min<std::size_t>( 512, tokens.size() - offset) ; // 今回の数
		commit( backend, request, std::span( tokens).subspan( offset, count)) ;
		offset += count ;
		notify( request, 2, static_cast<std::uint32_t>( offset), static_cast<std::uint32_t>( tokens.size())) ;
	}
	NativeWholeTextGenerated generated ; // 採用候補の本文と実counter
	generated.prefixTokens = static_cast<std::uint32_t>( tokens.size()) ;
	std::vector<std::int32_t> raw ; // EOGを除く実commit列、最大2815
	std::vector<std::int32_t> analysis ; // opening込み、closingなし
	std::vector<std::int32_t> final ; // EOGなしのFinal
	raw.reserve( 2816) ;
	analysis.reserve( 1279) ;
	final.reserve( 1535) ;
	bool inFinal = false ; // closingのcommit後だけFinalへ移る
	bool naturalEog = false ; // sampled EOGだけが成功条件
	// 固定lane予算に到達するか自然EOGまで一個ずつsample/commitする
	while( !naturalEog) {
		checkStop( request) ;
		backend.validateLogits() ;
		const auto token = backend.sampleGreedy() ; // finite検査直後の一個
		requireNative( token >= 0 && token < static_cast<std::int32_t>( K_VOCABULARY_TOKENS), KoefudeWtDiagnostic::TOKEN,
					  KoefudeWtStatus::INFERENCE_FAILED) ;
		const bool eog = backend.isEog( token) ; // live vocabの分類
		requireNative( eog == (token == 1 || token == 50 || token == 106), KoefudeWtDiagnostic::MODEL_METADATA,
					  KoefudeWtStatus::ABI_MISMATCH) ;
		// EOGはFinalだけで認め、KVへdecodeしない
		if( eog) {
			requireNative( inFinal, KoefudeWtDiagnostic::ANALYSIS_EOG_BEFORE_FINAL, KoefudeWtStatus::INCOMPLETE) ;
			++generated.finalSampledTokens ;
			naturalEog = true ;
			continue ;
		}
		// laneを越えるmarkerは次のdecode/sampleより前に拒否する
		if( inFinal) {
			requireNative( token != markers[0] && token != markers[1] && token != markers[2],
						  KoefudeWtDiagnostic::FRAMING) ;
			commit( backend, request, std::span( &token, 1)) ;
			raw.push_back( token) ;
			final.push_back( token) ;
			++generated.finalSampledTokens ;
			requireNative( generated.finalSampledTokens < 1536, KoefudeWtDiagnostic::FINAL_TOKEN_LIMIT,
						  KoefudeWtStatus::INCOMPLETE) ;
		} else {
			requireNative( (generated.analysisSampledTokens == 0 ? token == markers[0] : token != markers[0]) &&
							  token != markers[2],
						  KoefudeWtDiagnostic::FRAMING) ;
			commit( backend, request, std::span( &token, 1)) ;
			raw.push_back( token) ;
			++generated.analysisSampledTokens ;
			// 自然closingは一回commitしてからFinalへ移る
			if( token == markers[1]) {
				static_cast<void>( analysisBytes( backend, analysis)) ;
				inFinal = true ;
			} else {
				analysis.push_back( token) ;
				// 1279個目のdecodeとlane検証を終えてから制御closingを一回投入する
				if( generated.analysisSampledTokens == 1279) {
					static_cast<void>( analysisBytes( backend, analysis)) ;
					commit( backend, request, std::span( &markers[1], 1)) ;
					raw.push_back( markers[1]) ;
					generated.closingControlTokens = 1 ;
					inFinal = true ;
				}
			}
		}
		requireNative( raw.size() <= 2815, KoefudeWtDiagnostic::INCOMPLETE, KoefudeWtStatus::INCOMPLETE) ;
		notify( request, 3, generated.analysisSampledTokens + generated.finalSampledTokens) ;
	}
	notify( request, 4) ;
	const auto rawBytes = backend.detokenize( raw, K_MAXIMUM_BYTES) ; // 全rawの独立復号
	const auto thought = analysisBytes( backend, analysis) ; // Analysis側の独立復号
	generated.body = backend.detokenize( final, std::min<std::uint32_t>( maximumBodyBytes, 65536)) ; // Final側の独立復号
	validateBody( generated.body, std::min( maximumBodyBytes, K_MAXIMUM_BYTES)) ;
	requireNative( rawBytes.size() <= K_MAXIMUM_BYTES && rawBytes == thought + std::string( K_CLOSING) + generated.body &&
					  generated.finalSampledTokens >= 2,
				  KoefudeWtDiagnostic::FRAMING) ;
	checkStop( request) ;
	return generated ;
}
//------------------------------------------------------------------------------
/**
 * @brief 公式の五つのframing controlをlive vocabから独立に照合します
 * @param[in,out] backend 同じ語彙
 * @return BOS/system-open/system-close/inst-open/inst-close
 */
//------------------------------------------------------------------------------
std::array<std::int32_t, 5> encodePlainMarkers( NativeWholeTextBackend &backend)
{
	constexpr std::array<std::string_view, 5> TEXTS{"<s>", "[SYSTEM_PROMPT]", "[/SYSTEM_PROMPT]", "[INST]",
													"[/INST]"} ; // 公式順
	std::array<std::int32_t, 5>				  ids{} ; // promotion後にも全IDを再照合
	// 五markerを一個ずつ解析し、重複ID・EOGを拒否します
	for( std::size_t i = 0; i < ids.size(); ++i) { // marker ordinal
		std::array<std::int32_t, 2> token{} ; // 一個以外は拒否
		requireNative( backend.tokenize( TEXTS[i], token) == 1 && token[0] >= 0 && token[0] < 131072 &&
						  backend.isControl( token[0]) && !backend.isEog( token[0]) &&
						  backend.detokenize( std::span( token).first( 1), 65536) == TEXTS[i],
					  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
		ids[i] = token[0] ;
		requireNative( std::find( ids.begin(), ids.begin() + i, ids[i]) == ids.begin() + i,
					  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
	}
	requireNative( ids[0] == 1, KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
	return ids ;
}
//------------------------------------------------------------------------------
/**
 * @brief prefix内のcontrolが公式五境界だけか順序も含め検査します
 * @param[in,out] backend live語彙
 * @param[in] tokens 完全prefix
 * @param[in] markers live五ID
 */
//------------------------------------------------------------------------------
void validatePlainPrefix( NativeWholeTextBackend &backend, std::span<const std::int32_t> tokens,
						 const std::array<std::int32_t, 5> &markers)
{
	requireNative( !tokens.empty() && tokens.front() == 1, KoefudeWtDiagnostic::FRAMING) ;
	std::size_t next = 0 ; // 期待control順
	// 入力全tokenを検査し、余分なcontrolを削除せず拒否します
	for( const auto token : tokens) { // 全prefixのID
		requireNative( token >= 0 && token < 131072, KoefudeWtDiagnostic::TOKEN) ;
		if( backend.isControl( token)) { // 公式framingだけを受け付けます
			requireNative( next < markers.size() && token == markers[next], KoefudeWtDiagnostic::FRAMING) ;
			++next ;
		}
	}
	requireNative( next == markers.size(), KoefudeWtDiagnostic::FRAMING) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 本文だけを自然EOSまで逐次生成します
 * @param[in,out] backend 同じtensor
 * @param[in] request callback値
 * @param[in] tokens 完全prefix
 * @param[in] maximumBodyBytes host残量
 * @return 完全本文。EOSは数えますがdecodeしません
 */
//------------------------------------------------------------------------------
NativeWholeTextGenerated generateSerialPlain( NativeWholeTextBackend &backend, const KoefudeWtRequestV1 &request,
											 const std::vector<std::int32_t> &tokens, std::uint32_t maximumBodyBytes)
{
	backend.openContext() ;
	checkStop( request) ;
	requireNative( backend.contextTokens() == 5632 && tokens.size() + 1536 <= 5632, KoefudeWtDiagnostic::CONTEXT,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	backend.openSampler() ;
	// 実prefixだけをbatch128以下で一度ずつ投入します
	for( std::size_t offset = 0; offset < tokens.size();) { // 投入済みtoken数
		const auto count = std::min<std::size_t>( 128, tokens.size() - offset) ; // 今回の有界数
		commit( backend, request, std::span( tokens).subspan( offset, count)) ;
		offset += count ;
		notify( request, 2, static_cast<std::uint32_t>( offset), static_cast<std::uint32_t>( tokens.size())) ;
	}
	NativeWholeTextGenerated generated ; // analysis/closingは0のまま
	generated.prefixTokens = static_cast<std::uint32_t>( tokens.size()) ;
	std::vector<std::int32_t> body ; // 非EOGの実commit列
	body.reserve( 1536) ;
	// 自然EOSか1536個目まで、sample後の分類をdecodeより先に行います
	while( generated.finalSampledTokens < 1536) {
		checkStop( request) ;
		backend.validateLogits() ;
		const auto token = backend.sampleGreedy() ; // greedy一個
		requireNative( token >= 0 && token < 131072, KoefudeWtDiagnostic::TOKEN) ;
		requireNative( backend.isEog( token) == (token == 2), KoefudeWtDiagnostic::MODEL_METADATA,
					  KoefudeWtStatus::ABI_MISMATCH) ;
		++generated.finalSampledTokens ;
		// 自然EOSだけを完成候補にし、空本文は未完として残します
		if( token == 2) {
			requireNative( !body.empty(), KoefudeWtDiagnostic::INCOMPLETE, KoefudeWtStatus::INCOMPLETE) ;
			generated.body = backend.detokenize( body, std::min<std::uint32_t>( maximumBodyBytes, 65536)) ;
			bool hasBody = false ; // 空白だけの本文を成功にしません
			static_cast<void>( checkedStrip( generated.body, &hasBody)) ;
			requireNative( hasBody, KoefudeWtDiagnostic::INCOMPLETE, KoefudeWtStatus::INCOMPLETE) ;
			validateBody( generated.body, std::min<std::uint32_t>( maximumBodyBytes, 65536)) ;
			notify( request, 4) ;
			checkStop( request) ;
			return generated ;
		}
		requireNative( !backend.isControl( token), KoefudeWtDiagnostic::FRAMING) ;
		commit( backend, request, std::span( &token, 1)) ;
		body.push_back( token) ;
		notify( request, 3, generated.finalSampledTokens) ;
	}
	throw NativeWholeTextError{KoefudeWtStatus::INCOMPLETE, KoefudeWtDiagnostic::FINAL_TOKEN_LIMIT} ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief ABI byte借用の構造を検査します
 * @param[in] bytes 入力POD
 * @return 元byte借用
 * @throws NativeWholeTextError 構造違反
 * @see koefude::whole_text::nativeBytes
 */
//------------------------------------------------------------------------------
std::string_view nativeBytes( const KoefudeWtBytesV1 &bytes)
{
	requireNative( bytes.reserved == 0 && (bytes.data || bytes.bytes == 0), KoefudeWtDiagnostic::ABI,
				  KoefudeWtStatus::ABI_MISMATCH) ;
	requireNative( bytes.bytes <= K_MAXIMUM_BYTES, KoefudeWtDiagnostic::PROMPT_BYTES,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	// byte viewだけを作り、所有や再encodeを行わないAPI境界
	return bytes.bytes == 0 ? std::string_view{}
							: std::string_view( reinterpret_cast<const char *>( bytes.data), bytes.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有効UTF-8だけをPython strip集合で描画します。NULを拒否します
 * @param[in] system system全byte
 * @param[in] user user全byte
 * @return 固定prefix全byte
 * @see koefude::whole_text::renderGemma4TwoTextThinking
 */
//------------------------------------------------------------------------------
std::string renderGemma4TwoTextThinking( const std::string_view system, const std::string_view user)
{
	const auto				   systemBody = checkedStrip( system) ; // 元byteの借用
	const auto				   userBody = checkedStrip( user) ; // 元byteの借用
	constexpr std::string_view HEAD = "<bos><|turn>system\n<|think|>\n" ; // 固定開始
	constexpr std::string_view MIDDLE = "<turn|>\n<|turn>user\n" ; // 固定境界
	constexpr std::string_view TAIL = "<turn|>\n<|turn>model\n" ; // generation prompt
	std::size_t				   remaining = K_MAXIMUM_BYTES - HEAD.size() - MIDDLE.size() - TAIL.size() ; // 残量
	requireNative( systemBody.size() <= remaining, KoefudeWtDiagnostic::PROMPT_BYTES,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	remaining -= systemBody.size() ;
	requireNative( userBody.size() <= remaining, KoefudeWtDiagnostic::PROMPT_BYTES,
				  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
	std::string result( HEAD) ; // 一つだけのprefix所有者
	result.append( systemBody).append( MIDDLE).append( userBody).append( TAIL) ;
	return result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 指定した語彙長のlogits全要素が有限かsample前に検査します
 * @param[in] logits 同期中だけ借用するfloat列です。dataは非NULLで、要素数はvocabularyTokensと一致させます
 * @param[in] vocabularyTokens 期待する語彙tokenの総要素数です。既定はK_VOCABULARY_TOKENSで、別方式は対応する語彙長を明示します
 * @throws NativeWholeTextError 非有限/長さ違反
 * @see koefude::whole_text::validateWholeTextNativeLogits
 */
//------------------------------------------------------------------------------
void validateWholeTextNativeLogits( const std::span<const float> logits, std::uint32_t vocabularyTokens)
{
	requireNative( logits.data() && logits.size() == vocabularyTokens, KoefudeWtDiagnostic::NONFINITE,
				  KoefudeWtStatus::INFERENCE_FAILED) ;
	// 全語彙をsample前に走査し、先頭だけの確認で非有限値を見逃さない
	for( const auto value : logits) { // 現logit
		requireNative( std::isfinite( value), KoefudeWtDiagnostic::NONFINITE, KoefudeWtStatus::INFERENCE_FAILED) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 一callの有界生成を行い、全解放後のFinalだけ返します
 * @param[in,out] backend 同期中だけ使う所有資源
 * @param[in] request 不変の借用要求
 * @param[in] maximumBodyBytes host残量
 * @return 完全なFinalと実counter
 * @throws NativeWholeTextError 全体失敗、部分本文は返しません
 * @note module内atomic guardで二件目を待たず拒否します
 * @see koefude::whole_text::generateGemma4WholeText
 */
//------------------------------------------------------------------------------
NativeWholeTextGenerated generateGemma4WholeText( NativeWholeTextBackend &backend, const KoefudeWtRequestV1 &request,
												 const std::uint32_t maximumBodyBytes)
{
	const GenerationGuard guard ; // 推論中の待機mutexは持たない
	bool				  finalized = false ; // 明示close後に同じfinalizationを繰り返さない
	try {
		requireNative( request.structSize == sizeof(request) && request.abiVersion == 1 && request.reserved == 0,
					  KoefudeWtDiagnostic::ABI, KoefudeWtStatus::ABI_MISMATCH) ;
		const auto system = nativeBytes( request.system) ; // 不変system借用
		const auto user = nativeBytes( request.user) ; // 不変user借用
		const auto prefix = renderGemma4TwoTextThinking( system, user) ; // model前に固定
		requireNative( !checkedStrip( system).empty() && !checkedStrip( user).empty() && noTags( system) && noTags( user),
					  KoefudeWtDiagnostic::FRAMING) ;
		checkStop( request) ;
		requireNative( request.modelPath && request.modelPathUnits >= 1 && request.modelPathUnits <= 32767,
					  KoefudeWtDiagnostic::ABI) ;
		const std::wstring_view path( request.modelPath, request.modelPathUnits) ; // lease借用
		requireNative( path.find( L'\0') == path.npos && request.modelPath[path.size()] == L'\0',
					  KoefudeWtDiagnostic::ABI) ;
		notify( request, 1) ;
		backend.openModel( path, true) ;
		checkStop( request) ;
		backend.validateMetadata() ;
		const auto tokens = encodePrefix( backend, prefix) ; // vocab-only proof
		const auto markers = encodeMarkers( backend) ; // vocab-only ID
		checkStop( request) ;
		backend.closeModel() ; // vocab pointerはここで破棄
		checkStop( request) ;
		backend.openModel( path, false) ;
		checkStop( request) ;
		backend.validateMetadata() ;
		requireNative( encodePrefix( backend, prefix) == tokens && encodeMarkers( backend) == markers,
					  KoefudeWtDiagnostic::MODEL_METADATA, KoefudeWtStatus::ABI_MISMATCH) ;
		checkStop( request) ;
		backend.openContext() ;
		checkStop( request) ;
		requireNative( backend.contextTokens() == 6912 && tokens.size() + 2816 <= 6912, KoefudeWtDiagnostic::CONTEXT,
					  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
		backend.openSampler() ;
		// 全prefixを実際の投入数から512以下に分割し、一度ずつcommitする
		for( std::size_t offset = 0; offset < tokens.size();) { // 投入済みtoken数
			const auto count = std::min<std::size_t>( 512, tokens.size() - offset) ; // 今回の数
			commit( backend, request, std::span( tokens).subspan( offset, count)) ;
			offset += count ;
			notify( request, 2, static_cast<std::uint32_t>( offset), static_cast<std::uint32_t>( tokens.size())) ;
		}
		NativeWholeTextGenerated generated ; // 採用候補の本文と実counter
		generated.prefixTokens = static_cast<std::uint32_t>( tokens.size()) ;
		std::vector<std::int32_t> raw ; // EOGを除く実commit列、最大2815
		std::vector<std::int32_t> analysis ; // opening込み、closingなし
		std::vector<std::int32_t> final ; // EOGなしのFinal
		raw.reserve( 2816) ;
		analysis.reserve( 1279) ;
		final.reserve( 1535) ;
		bool inFinal = false ; // closingのcommit後だけFinalへ移る
		bool naturalEog = false ; // sampled EOGだけが成功条件
		// 固定lane予算に到達するか自然EOGまで一個ずつsample/commitする
		while( !naturalEog) {
			checkStop( request) ;
			backend.validateLogits() ;
			const auto token = backend.sampleGreedy() ; // finite検査直後の一個
			requireNative( token >= 0 && token < static_cast<std::int32_t>( K_VOCABULARY_TOKENS),
						  KoefudeWtDiagnostic::TOKEN, KoefudeWtStatus::INFERENCE_FAILED) ;
			const bool eog = backend.isEog( token) ; // live vocabの分類
			requireNative( eog == (token == 1 || token == 50 || token == 106), KoefudeWtDiagnostic::MODEL_METADATA,
						  KoefudeWtStatus::ABI_MISMATCH) ;
			// EOGはFinalだけで認め、KVへdecodeしない
			if( eog) {
				requireNative( inFinal, KoefudeWtDiagnostic::INCOMPLETE, KoefudeWtStatus::INCOMPLETE) ;
				++generated.finalSampledTokens ;
				naturalEog = true ;
				continue ;
			}
			// laneを越えるmarkerは次のdecode/sampleより前に拒否する
			if( inFinal) {
				requireNative( token != markers[0] && token != markers[1] && token != markers[2],
							  KoefudeWtDiagnostic::FRAMING) ;
				commit( backend, request, std::span( &token, 1)) ;
				raw.push_back( token) ;
				final.push_back( token) ;
				++generated.finalSampledTokens ;
				requireNative( generated.finalSampledTokens < 1536, KoefudeWtDiagnostic::INCOMPLETE,
							  KoefudeWtStatus::INCOMPLETE) ;
			} else {
				requireNative( (generated.analysisSampledTokens == 0 ? token == markers[0] : token != markers[0]) &&
								  token != markers[2],
							  KoefudeWtDiagnostic::FRAMING) ;
				commit( backend, request, std::span( &token, 1)) ;
				raw.push_back( token) ;
				++generated.analysisSampledTokens ;
				// 自然closingは一回commitしてからFinalへ移る
				if( token == markers[1]) {
					static_cast<void>( analysisBytes( backend, analysis)) ;
					inFinal = true ;
				} else {
					analysis.push_back( token) ;
					// 1279個目のdecodeとlane検証を終えてから制御closingを一回投入する
					if( generated.analysisSampledTokens == 1279) {
						static_cast<void>( analysisBytes( backend, analysis)) ;
						commit( backend, request, std::span( &markers[1], 1)) ;
						raw.push_back( markers[1]) ;
						generated.closingControlTokens = 1 ;
						inFinal = true ;
					}
				}
			}
			requireNative( raw.size() <= 2815, KoefudeWtDiagnostic::INCOMPLETE, KoefudeWtStatus::INCOMPLETE) ;
			notify( request, 3, generated.analysisSampledTokens + generated.finalSampledTokens) ;
		}
		notify( request, 4) ;
		const auto rawBytes = backend.detokenize( raw, K_MAXIMUM_BYTES) ; // 全rawの独立復号
		const auto thought = analysisBytes( backend, analysis) ; // Analysis側の独立復号
		generated.body = backend.detokenize( final, K_MAXIMUM_BYTES) ; // Final側の独立復号
		validateBody( generated.body, std::min( maximumBodyBytes, K_MAXIMUM_BYTES)) ;
		requireNative( rawBytes.size() <= K_MAXIMUM_BYTES &&
						  rawBytes == thought + std::string( K_CLOSING) + generated.body &&
						  generated.finalSampledTokens >= 2,
					  KoefudeWtDiagnostic::FRAMING) ;
		finalized = true ;
		const auto cleanup = backend.close() ; // 成功でもcopy前に逆順全解放
		requireNative( cleanup == 0, KoefudeWtDiagnostic::CLEANUP, KoefudeWtStatus::INFERENCE_FAILED) ;
		checkStop( request) ; // 解放後の遅い取消しも成功より優先
		return generated ;
	} catch( ...) {
		// 明示finalization前の一次失敗だけで取得済み資源を解放する
		if( !finalized) {
			static_cast<void>( backend.close()) ;
		}
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief metadata/markerを照合して一回だけvocabを開きます
 * @param[in,out] backend closeまで生存するowner
 * @param[in] request 値copyするcallback要求
 * @param[in] session 非0で再利用しないID
 * @param[in] deferred native owner登録後のinitializeを選ぶ場合だけtrue
 * @see koefude::whole_text::NativeWholeTextCounter::NativeWholeTextCounter(koefude::whole_text::NativeWholeTextBackend &, const KoefudeWtCounterOpenV2 &, std::uint64_t, bool)
 */
//------------------------------------------------------------------------------
NativeWholeTextCounter::NativeWholeTextCounter( NativeWholeTextBackend &backend, const KoefudeWtCounterOpenV2 &request,
											   std::uint64_t session, bool deferred)
	: backend_( backend), session_( session)
{
	// exported ownerは先に登録し、初期化失敗の所有責任を保持します
	if( !deferred) {
		initialize( request) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief native owner確保後だけvocabを開きます
 * @param[in] request 同期要求
 * @see koefude::whole_text::NativeWholeTextCounter::initialize
 */
//------------------------------------------------------------------------------
void NativeWholeTextCounter::initialize( const KoefudeWtCounterOpenV2 &request)
{
	const GenerationGuard gate ; // V1を含め待機しない
	requireNative( !initialized_ && request.structSize == sizeof(request) && request.abiVersion == 2 &&
					  request.reserved == 0 && request.modelPath && request.modelPathUnits > 0 &&
					  request.modelPathUnits <= 32767 && request.modelPath[request.modelPathUnits] == L'\0' &&
					  session_ != 0 && request.bindingSha256 != WholeTextDigest{},
				  KoefudeWtDiagnostic::ABI, KoefudeWtStatus::ABI_MISMATCH) ;
	const std::wstring_view path( request.modelPath,
								 request.modelPathUnits) ; // 検査後だけborrow
	requireNative( path.find( L'\0') == path.npos, KoefudeWtDiagnostic::ABI) ;
	path_.assign( path) ;
	binding_ = request.bindingSha256 ;
	initialized_ = true ;
	callbacks_.shouldStop = request.shouldStop ;
	callbacks_.progress = request.progress ;
	callbacks_.callbackData = request.callbackData ;
	checkStop( callbacks_) ;
	open_ = true ;
	counterSessionActive.store( true) ; // 部分loadも同じownerがcleanupします
	try {
		backend_.openModel( std::wstring_view( request.modelPath, request.modelPathUnits), true) ;
		backend_.validateMetadata() ;
		// 固有方式の語彙だけを検証し、別モデルのmarkerを受け付けません
		if( backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN) {
			plainMarkers_ = encodePlainMarkers( backend_) ;
		} else {
			markers_ = encodeMarkers( backend_) ;
		}
		checkStop( callbacks_) ;
		open_ = true ;
		counterSessionActive.store( true) ;
	} catch( ...) {
		const auto cleanup = close() ; // 失敗でも全解放
		static_cast<void>( cleanup) ; // primary例外を保ちclose証拠へ別に残します
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 残資源を例外なしで解放します
 * @see koefude::whole_text::NativeWholeTextCounter::~NativeWholeTextCounter
 */
//------------------------------------------------------------------------------
NativeWholeTextCounter::~NativeWholeTextCounter()
{
	static_cast<void>( close()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief closeを一回行います
 * @return 元cleanup符号、二回目0
 * @see koefude::whole_text::NativeWholeTextCounter::close
 */
//------------------------------------------------------------------------------
std::uint32_t NativeWholeTextCounter::close() noexcept
{
	// close済みならmodel/backendを再度解放しません
	if( !open_ || unknown_) {
		return cleanupCode_ != 0 ? cleanupCode_
								 : (unknown_ ? static_cast<std::uint32_t>( KoefudeWtDiagnostic::CLEANUP) : 0) ;
	}
	const auto code = backend_.close() ;
	// chunk cleanupと全体cleanupの最初の元codeを保持します
	if( cleanupCode_ == 0) {
		cleanupCode_ = code ;
	}
	open_ = false ;
	counterSessionActive.store( false) ;
	return cleanupCode_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じrenderer/vocab/flagsで独立計数します
 * @param[in] request 同期borrow
 * @return 実receipt。失敗後は再実行しません
 * @see koefude::whole_text::NativeWholeTextCounter::measure
 */
//------------------------------------------------------------------------------
WholeTextTokenReceipt NativeWholeTextCounter::measure( const KoefudeWtMeasureRequestV2 &request)
{
	const GenerationGuard gate( true) ; // 二重measureとV1を待たず拒否
	requireNative( open_ && !failed_ && !tensor_, KoefudeWtDiagnostic::ABI) ;
	try {
		checkStop( callbacks_) ;
		requireNative( request.structSize == sizeof(request) && request.abiVersion == 2 && request.reserved[0] == 0 &&
						  request.reserved[1] == 0,
					  KoefudeWtDiagnostic::ABI, KoefudeWtStatus::ABI_MISMATCH) ;
		const auto system = nativeBytes( request.system), user = nativeBytes( request.user) ;
		const auto core = nativeBytes( request.core), before = nativeBytes( request.before),
				   after = nativeBytes( request.after) ;
		requireNative( !checkedStrip( system).empty() && !checkedStrip( user).empty() && !core.empty() && noTags( system) &&
						  noTags( user),
					  KoefudeWtDiagnostic::FRAMING) ;
		const auto rendered = renderWholeTextGeneration( backend_.generationKind(), system,
														user) ; // 同じ不変renderer
		requireNative( system.size() + user.size() + rendered.size() <= 65536, KoefudeWtDiagnostic::PROMPT_BYTES,
					  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
		const auto amount = rendered.size() + core.size() + before.size() + after.size() ; // 全実入力byte
		requireNative( sequence_ < 262144 && amount <= 268435456 - measuredBytes_, KoefudeWtDiagnostic::PROMPT_BYTES,
					  KoefudeWtStatus::RESOURCE_UNAVAILABLE) ;
		measuredBytes_ += amount ;
		++sequence_ ;
		WholeTextTokenReceipt out ;
		out.session = session_ ;
		out.sequence = sequence_ ;
		out.candidateBinding = request.candidateBinding ;
		const std::array<std::string_view, 6> texts{system, user, rendered, core, before, after} ;
		// 計数対象でないsystem/userも全byteをreceiptへ束縛します
		for( std::size_t i = 0; i < texts.size(); ++i) {
			out.bytes[i] = texts[i].size() ;
			out.sha256[i] = wholeTextSha256( texts[i]) ;
		}
		std::array<std::uint32_t, 4> counts{} ; // prefix/core/before/after独立数
		bool						 prefixComplete = false ; // 完全ID列がある時だけtrue
		// 全prefixと各plain componentを同vocab/flagsで独立計数します
		for( std::size_t i = 0; i < counts.size(); ++i) {
			checkStop( callbacks_) ;
			const auto text = texts[i + 2] ; // rendered/core/before/after
			// file端の空文脈はtokenizerを呼ばず0です
			if( text.empty()) {
				continue ;
			}
			static_cast<void>( checkedStrip( text)) ;
			std::array<std::int32_t, 4097> tokens{} ; // 必要数で拡大しない固定buffer
			const auto					   count = backend_.tokenize( text, tokens) ; // 符号を保つ
			// INT_MINとbuffer以下の負必要数は正式API矛盾です
			if( count < 0) {
				requireNative( count != std::numeric_limits<std::int32_t>::min() &&
								  -count > static_cast<std::int32_t>( tokens.size()),
							  KoefudeWtDiagnostic::TOKEN) ;
				counts[i] = static_cast<std::uint32_t>( -count) ;
				continue ;
			}
			requireNative( count > 0 && static_cast<std::size_t>( count) <= tokens.size(), KoefudeWtDiagnostic::TOKEN) ;
			const auto ids = std::span( tokens).first( static_cast<std::size_t>( count)) ; // 全ID
			// 語彙外IDはdetokenizerへ渡しません
			for( const auto id : ids) {
				requireNative( id >= 0 && id < (backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN
												   ? 131072
												   : static_cast<std::int32_t>( K_VOCABULARY_TOKENS)),
							  KoefudeWtDiagnostic::TOKEN) ;
			}
			requireNative( backend_.detokenize( ids, 65536) == text, KoefudeWtDiagnostic::TOKEN) ;
			counts[i] = static_cast<std::uint32_t>( count) ;
			// 全prefixだけをLE u32の列としてhashします
			if( i == 0) {
				// 完全prefix内のcontrolもtensor/contextを開く前に拒否します
				if( backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN) {
					validatePlainPrefix( backend_, ids, plainMarkers_) ;
				}
				std::array<std::uint8_t, 4097 * 4> bytes{} ; // 固定LE buffer
				// 全要素を元の順序で処理し、範囲の終端で停止します
				for( std::size_t n = 0; n < ids.size(); ++n) {
					const auto id = static_cast<std::uint32_t>( ids[n]) ;
					// 全要素を元の順序で処理し、範囲の終端で停止します
					for( unsigned b = 0; b < 4; ++b) {
						bytes[n * 4 + b] = static_cast<std::uint8_t>( id >> (b * 8)) ;
					}
				}
				out.tokenSha256 = wholeTextSha256( std::span( bytes).first( ids.size() * 4)) ;
				prefixComplete = true ;
			}
		}
		out.prefixTokens = counts[0] ;
		out.coreTokens = counts[1] ;
		out.beforeTokens = counts[2] ;
		out.afterTokens = counts[3] ;
		out.completeTokenDigest = prefixComplete ;
		out.fits = prefixComplete && counts[0] <= 4096 && counts[1] <= 512 && counts[2] <= 128 && counts[3] <= 128 ;
		out.receiptSha256 = wholeTextReceiptDigest( out) ;
		checkStop( callbacks_) ;
		return out ;
	} catch( ...) {
		failed_ = true ;
		throw ;
	} // 同じ失敗sessionを再実行しません
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ計数sessionの封印値を照合し、vocab-only modelをtensor付きCPU modelへ一度だけ昇格します
 * @param[in] r returnまで借用するV2昇格要求。open/plan/inputのSHA、元byte数、計数回数/総量とrecord/生成件数を同じsessionへ照合します
 * @throws NativeWholeTextError 未open/失敗済み/昇格済み、競合、要求不整合、停止、再openまたはmetadata/marker不一致です
 * @note modelの再open以降の例外はfailed状態を記録します。成功時はtensor modelと封印計画を保持し、失敗後もownerにclose責任が残ります
 */
//------------------------------------------------------------------------------
void NativeWholeTextCounter::promote( const KoefudeWtPromoteRequestV2 &r)
{
	const GenerationGuard gate( true) ; // 同じsessionの非待機entry
	requireNative( open_ && !failed_ && !tensor_, KoefudeWtDiagnostic::ABI) ;
	try {
		requireNative( r.structSize == sizeof(r) && r.abiVersion == 2 && r.reserved[0] == 0 && r.reserved[1] == 0 &&
						  r.openBindingSha256 == binding_ && r.planSha256 != WholeTextDigest{} &&
						  r.inputSha256 != WholeTextDigest{} && r.originalBytes > 0 && r.originalBytes <= 8388608 &&
						  r.planMeasures == sequence_ && r.planMeasuredBytes == measuredBytes_ && r.recordCount > 0 &&
						  r.recordCount <= 16384 && r.generationCount > 0 && r.generationCount <= r.recordCount,
					  KoefudeWtDiagnostic::ABI) ;
		checkStop( callbacks_) ;
		backend_.closeModel() ; // vocabとtensorを同時保持しません
		checkStop( callbacks_) ;
		backend_.openModel( path_, false) ; // 同じpathとlease、一回だけ
		backend_.validateMetadata() ;
		// tensor再open後も同じ語彙と全境界IDを要求します
		if( backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN) {
			requireNative( encodePlainMarkers( backend_) == plainMarkers_, KoefudeWtDiagnostic::MODEL_METADATA,
						  KoefudeWtStatus::ABI_MISMATCH) ;
		} else {
			requireNative( encodeMarkers( backend_) == markers_, KoefudeWtDiagnostic::MODEL_METADATA,
						  KoefudeWtStatus::ABI_MISMATCH) ;
		}
		checkStop( callbacks_) ;
		plan_ = r.planSha256 ;
		input_ = r.inputSha256 ;
		originalBytes_ = r.originalBytes ;
		recordCount_ = r.recordCount ;
		generationCount_ = r.generationCount ;
		tensor_ = true ;
	} catch( ...) {
		failed_ = true ;
		throw ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 昇格済み同modelで一coreの封印/計数を再照合し、自然終端とchunk解放を確認して返します
 * @param[in] r 同期借用するV2要求。昇順core範囲、chunk位置、plan/inputのSHA、expectedReceiptと全prefix byte列を元計画へ照合します
 * @param[in] maximum 本文の最大byte数。1～65536で、host残容量を超えない値です
 * @param[out] receipt 正常復帰時の生成counter/hash/範囲を持つ成功receipt。例外時は途中の代入が残り得るため採用しません
 * @return 独立所有の完成Final本文と実counter。自然終端なし、停止、再照合/推論/解放失敗では部分本文を返しません
 * @throws NativeWholeTextError 状態/要求/順序/上限違反、競合、取消し、計数不一致、未完、推論またはcleanup失敗です
 * @note 試行開始後の例外はfailed状態を記録します。解放未確認時はretainUnknownで再freeを抑止し、model/sessionの終了責任をownerに残します
 */
//------------------------------------------------------------------------------
NativeWholeTextGenerated NativeWholeTextCounter::generate( const KoefudeWtChunkRequestV2 &r, std::uint32_t maximum,
														  WholeTextBoundedChunkReceipt &receipt)
{
	const GenerationGuard gate( true) ; // V1と全V2 kernelで同じ排他
	requireNative( open_ && !failed_ && tensor_, KoefudeWtDiagnostic::ABI) ;
	bool chunkStarted = false ; // context作成前の拒否ではchunk cleanup不要
	bool chunkClosed = false ; // 不明なfreeを再試行しません
	try {
		const auto &expected = r.expectedReceipt ; // 受理済みの過去receipt
		requireNative( r.structSize == sizeof(r) && r.abiVersion == 2 && r.reserved0 == 0 && r.reserved1[0] == 0 &&
						  r.reserved1[1] == 0 && r.chunkIndex < recordCount_ && attempts_ < generationCount_ &&
						  (attempts_ == 0 || r.chunkIndex > lastIndex_) && r.coreBegin >= lastCoreEnd_ &&
						  r.coreBegin < r.coreEnd && r.coreEnd <= originalBytes_ && r.planSha256 == plan_ &&
						  r.inputSha256 == input_ && maximum > 0 && maximum <= 65536 &&
						  expected.structSize == sizeof(expected) && expected.abiVersion == 2 &&
						  expected.sessionId == session_ && expected.sequence > 0 && expected.sequence <= sequence_ &&
						  expected.accountingContract == 2 && expected.flags == 3 && expected.reserved[0] == 0 &&
						  expected.reserved[1] == 0 && expected.diagnostic == 0 && expected.engineCode == 0 &&
						  r.prompt.structSize == sizeof(r.prompt) && r.prompt.abiVersion == 2 &&
						  r.prompt.reserved[0] == 0 && r.prompt.reserved[1] == 0,
					  KoefudeWtDiagnostic::ABI) ;
		++attempts_ ;
		lastIndex_ = r.chunkIndex ;
		lastCoreEnd_ = r.coreEnd ;
		WholeTextTokenReceipt accepted{expected.sessionId,
									   expected.sequence,
									   expected.prefixTokens,
									   expected.coreTokens,
									   expected.beforeTokens,
									   expected.afterTokens,
									   true,
									   true,
									   expected.bytes,
									   expected.sha256,
									   expected.candidateBinding,
									   expected.tokenSha256,
									   expected.receiptSha256} ; // paddingを解釈しません
		requireNative( accepted.receiptSha256 == wholeTextReceiptDigest( accepted) &&
						  accepted.candidateBinding == r.prompt.candidateBinding && accepted.prefixTokens <= 4096 &&
						  accepted.coreTokens > 0 && accepted.coreTokens <= 512 && accepted.beforeTokens <= 128 &&
						  accepted.afterTokens <= 128,
					  KoefudeWtDiagnostic::TOKEN) ;
		const auto							  system = nativeBytes( r.prompt.system) ; // returnまで同期borrow
		const auto							  user = nativeBytes( r.prompt.user) ; // 全候補byte
		const auto							  rendered = renderWholeTextGeneration( backend_.generationKind(), system,
																				   user) ; // 同じrenderer
		const std::array<std::string_view, 6> texts{system,
													user,
													rendered,
													nativeBytes( r.prompt.core),
													nativeBytes( r.prompt.before),
													nativeBytes( r.prompt.after)} ; // 同じ順
		requireNative( system.size() + user.size() + rendered.size() <= 65536 && noTags( system) && noTags( user) &&
						  !checkedStrip( system).empty() && !checkedStrip( user).empty(),
					  KoefudeWtDiagnostic::FRAMING) ;
		// 全componentのbyte/SHAをcontext作成前に照合します
		for( std::size_t i = 0; i < texts.size(); ++i) { // component ordinal
			requireNative( expected.bytes[i] == texts[i].size() && expected.sha256[i] == wholeTextSha256( texts[i]),
						  KoefudeWtDiagnostic::TOKEN) ;
		}
		checkStop( callbacks_) ;
		const auto						   tokens = encodePrefix( backend_, rendered) ; // 固定4097、拡張再試行0
		std::array<std::uint8_t, 4097 * 4> ids{} ; // 完全LE u32列
		// 同数でIDだけ違う場合もprefill前に拒否します
		for( std::size_t i = 0; i < tokens.size(); ++i) { // prefix ordinal
			requireNative( tokens[i] >= 0 &&
							  tokens[i] < (backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN
											   ? 131072
											   : static_cast<std::int32_t>( K_VOCABULARY_TOKENS)),
						  KoefudeWtDiagnostic::TOKEN) ;
			// 一IDの全byteを小端順で保存します
			for( unsigned b = 0; b < 4; ++b) {
				ids[i * 4 + b] = static_cast<std::uint8_t>( static_cast<std::uint32_t>( tokens[i]) >> (b * 8)) ;
			} // byte ordinal
		}
		const auto tokenSha = wholeTextSha256( std::span( ids).first( tokens.size() * 4)) ; // 全列
		requireNative( tokens.size() == expected.prefixTokens && tokenSha == expected.tokenSha256,
					  KoefudeWtDiagnostic::TOKEN) ;
		// 生成直前にも完全prefix controlを照合し、余分なBOSも拒否します
		if( backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN) {
			validatePlainPrefix( backend_, tokens, plainMarkers_) ;
		}
		chunkStarted = true ;
		auto	   generated = backend_.generationKind() == WholeTextGenerationKind::MINISTRAL_PLAIN
								   ? generateSerialPlain( backend_, callbacks_, tokens, maximum)
								   : generateSerialLane( backend_, callbacks_, tokens, markers_, maximum) ;
		const auto cleanup = backend_.closeChunk() ; // 次chunk前に必ず明示解放
		chunkClosed = true ;
		// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		if( cleanupCode_ == 0) {
			cleanupCode_ = cleanup.code ;
		}
		// 解放不明では全体close/destructorからも再freeさせません
		if( !cleanup.released) {
			unknown_ = true ;
			backend_.retainUnknown() ;
		}
		// 所有と解放確認の状態に従い、未確認の資源を再解放しません
		if( !cleanup.released || cleanup.code != 0) {
			throw NativeWholeTextError{KoefudeWtStatus::INTERNAL, KoefudeWtDiagnostic::CLEANUP,
									   static_cast<std::int32_t>( cleanup.code)} ;
		}
		checkStop( callbacks_) ;
		receipt = {session_,
				   r.chunkIndex,
				   {r.coreBegin, r.coreEnd},
				   plan_,
				   input_,
				   expected.receiptSha256,
				   tokenSha,
				   expected.sha256[2],
				   wholeTextSha256( generated.body),
				   {},
				   expected.bytes[2],
				   static_cast<std::uint32_t>( generated.body.size()),
				   generated.prefixTokens,
				   generated.analysisSampledTokens,
				   generated.closingControlTokens,
				   generated.finalSampledTokens,
				   true} ;
		receipt.receiptSha256 = wholeTextGeneratedReceiptDigest( receipt) ;
		return generated ;
	} catch( ...) {
		failed_ = true ;
		// current chunkだけを一回閉じ、未確認ならownerを毒化します
		if( chunkStarted && !chunkClosed) {
			const auto cleanup = backend_.closeChunk() ; // 一次例外を保持
			// 所有と解放確認の状態に従い、未確認の資源を再解放しません
			if( cleanupCode_ == 0) {
				cleanupCode_ = cleanup.code ;
			}
			// 所有と解放確認の状態に従い、未確認の資源を再解放しません
			if( !cleanup.released) {
				unknown_ = true ;
				backend_.retainUnknown() ;
			}
		}
		throw ;
	}
}
} // namespace koefude::whole_text
