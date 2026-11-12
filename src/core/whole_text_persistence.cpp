//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_persistence.cpp
 * @brief V7の有界証拠を厳密なLE形式で検証します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
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
#include "core/whole_text_persistence.h"
#include "core/history.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 不正保存証拠を部分適用せず拒否します
 * @param[in] valid 必須条件
 */
//------------------------------------------------------------------------------
void require( bool valid)
{
	// 不正な外部値を有効結果へ流しません
	if( !valid) {
		throw HistoryError( HistoryErrorKind::CORRUPT) ;
	}
}
/** @brief 小さいLE証拠の書込み器です */
class Writer {
	public:
	/// 有界encoding
	std::vector<std::uint8_t> bytes ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅整数を書きます
	 * @param[in] n 固定fieldへ符号化する符号なし整数。下位byteからlittle endianで追加します
	 * @param[in] width 出力するbyte数。呼出側は整数型の幅以内の固定field幅を渡します
	 */
	//------------------------------------------------------------------------------
	void					  number( std::uint64_t n, unsigned width = 4)
	{
		require( bytes.size() <= 8192 && width <= 8192 - bytes.size()) ;
		// 固定幅の下位byteから追加します
		for( unsigned i = 0; i < width; ++i) {
			bytes.push_back( static_cast<std::uint8_t>( n >> (i * 8))) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief raw byteを書きます
	 * @param[in] v 同期借用
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<const std::uint8_t> v)
	{
		require( bytes.size() <= 8192 && v.size() <= 8192 - bytes.size()) ;
		bytes.insert( bytes.end(), v.begin(), v.end()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief domainをNUL込みで書きます
	 * @param[in] v 静的ASCII
	 */
	//------------------------------------------------------------------------------
	template <std::size_t N> void domain( const char ( &v)[N])
	{
		raw( {reinterpret_cast<const std::uint8_t *>( v), N}) ; // char表現の同期borrow
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief ASCII文字列を書きます
	 * @param[in] v 全byte
	 */
	//------------------------------------------------------------------------------
	void text( std::string_view v)
	{
		number( v.size()) ;
		raw( {reinterpret_cast<const std::uint8_t *>( v.data()), v.size()}) ;
	}
} ;
/** @brief 小さい保存証拠を全消費する読取り器です */
class Reader {
	public:
	/// 同期borrow
	std::span<const std::uint8_t> left ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 入力を借用します
	 * @param[in] v 全byte
	 */
	//------------------------------------------------------------------------------
	explicit Reader( std::span<const std::uint8_t> v) : left( v)
	{
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅値を読みます
	 * @param[in] width 幅
	 * @return LE値
	 */
	//------------------------------------------------------------------------------
	std::uint64_t number( unsigned width = 4)
	{
		require( width <= left.size()) ;
		std::uint64_t n = 0 ; // 復元値
		// 宣言幅だけ復元します
		for( unsigned i = 0; i < width; ++i) {
			n |= static_cast<std::uint64_t>( left[i]) << (i * 8) ;
		}
		left = left.subspan( width) ;
		return n ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 真偽値を読みます
	 * @return 0/1だけ受理
	 */
	//------------------------------------------------------------------------------
	bool flag()
	{
		const auto n = number() ; /* 外部の元値 */
		require( n <= 1) ;
		return n != 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定byte列を読みます
	 * @param[out] v 全域を設定
	 */
	//------------------------------------------------------------------------------
	void raw( std::span<std::uint8_t> v)
	{
		require( v.size() <= left.size()) ;
		std::copy_n( left.begin(), v.size(), v.begin()) ;
		left = left.subspan( v.size()) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief NUL付きdomainを照合します
	 * @param[in] v 期待domain
	 */
	//------------------------------------------------------------------------------
	template <std::size_t N> void domain( const char ( &v)[N])
	{
		require( N <= left.size() &&
				std::equal( left.begin(), left.begin() + N, reinterpret_cast<const std::uint8_t *>( v))) ;
		left = left.subspan( N) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大64byte ASCIIを読みます
	 * @return 所有profile
	 */
	//------------------------------------------------------------------------------
	std::string text()
	{
		const auto n = number() ; /* 外部長さ */
		require( n > 0 && n <= 64 && n <= left.size()) ;
		std::string v( reinterpret_cast<const char *>( left.data()), static_cast<std::size_t>( n)) ; /* 同期byteのcopy */
		left = left.subspan( static_cast<std::size_t>( n)) ;
		return v ;
	}
} ;
//------------------------------------------------------------------------------
/**
 * @brief 要求を保存します
 * @param[in,out] w writer
 * @param[in] v 要求
 */
//------------------------------------------------------------------------------
void writeRequest( Writer &w, const WholeTextCorrectionRequest &v)
{
	w.number( v.source.historyId, 8) ;
	w.raw( v.source.sha256) ;
	w.number( v.operationId, 8) ;
	w.text( v.profileId) ;
	w.raw( v.profileSha256) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 要求を復元します
 * @param[in,out] r reader
 * @return 要求
 */
//------------------------------------------------------------------------------
WholeTextCorrectionRequest readRequest( Reader &r)
{
	WholeTextCorrectionRequest v ; /* 保存値 */
	v.source.historyId = r.number( 8) ;
	r.raw( v.source.sha256) ;
	v.operationId = r.number( 8) ;
	v.profileId = r.text() ;
	r.raw( v.profileSha256) ;
	require( v.source.historyId > 0 && v.operationId > 0 &&
			std::all_of( v.profileId.begin(), v.profileId.end(),
						//------------------------------------------------------------------------------
						/**
						 * @brief ASCIIだけ受理します
						 * @param[in] c byte
						 * @return 適合
						 */
						//------------------------------------------------------------------------------
						[]( unsigned char c)
						{
							return c > 0 && c < 128 ;
						})) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief pcmを保存します
 * @param[in,out] w writer
 * @param[in] v 小値
 */
//------------------------------------------------------------------------------
void writePcm( Writer &w, const DecodedPcmIdentity &v)
{
	w.number( v.version, 4) ;
	w.number( v.sampleCount, 8) ;
	w.number( v.sampleRate, 4) ;
	w.number( v.channels, 4) ;
	w.number( v.originSample, 8) ;
	w.raw( v.pcmSha256) ;
}
//------------------------------------------------------------------------------
/**
 * @brief pcmを復元します
 * @param[in,out] r reader
 * @return 小値
 */
//------------------------------------------------------------------------------
DecodedPcmIdentity readPcm( Reader &r)
{
	DecodedPcmIdentity v ; // 外部保存値
	v.version = static_cast<std::uint32_t>( r.number( 4)) ;
	v.sampleCount = static_cast<std::uint64_t>( r.number( 8)) ;
	v.sampleRate = static_cast<std::uint32_t>( r.number( 4)) ;
	v.channels = static_cast<std::uint32_t>( r.number( 4)) ;
	v.originSample = static_cast<std::uint64_t>( r.number( 8)) ;
	r.raw( v.pcmSha256) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief bindingを保存します
 * @param[in,out] w writer
 * @param[in] v 小値
 */
//------------------------------------------------------------------------------
void writeBinding( Writer &w, const WholeTextBoundedPromotion &v)
{
	w.raw( v.openBinding) ;
	w.raw( v.planSha256) ;
	w.raw( v.inputSha256) ;
	w.number( v.originalBytes, 8) ;
	w.number( v.measures, 8) ;
	w.number( v.measuredBytes, 8) ;
	w.number( v.recordCount, 4) ;
	w.number( v.generationCount, 4) ;
}
//------------------------------------------------------------------------------
/**
 * @brief bindingを復元します
 * @param[in,out] r reader
 * @return 小値
 */
//------------------------------------------------------------------------------
WholeTextBoundedPromotion readBinding( Reader &r)
{
	WholeTextBoundedPromotion v ; // 外部保存値
	r.raw( v.openBinding) ;
	r.raw( v.planSha256) ;
	r.raw( v.inputSha256) ;
	v.originalBytes = static_cast<std::uint64_t>( r.number( 8)) ;
	v.measures = static_cast<std::uint64_t>( r.number( 8)) ;
	v.measuredBytes = static_cast<std::uint64_t>( r.number( 8)) ;
	v.recordCount = static_cast<std::uint32_t>( r.number( 4)) ;
	v.generationCount = static_cast<std::uint32_t>( r.number( 4)) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief cursorを保存します
 * @param[in,out] w writer
 * @param[in] v 小値
 */
//------------------------------------------------------------------------------
void writeCursor( Writer &w, const WholeTextBoundedCursor &v)
{
	w.number( v.count, 4) ;
	w.number( v.coreEnd, 8) ;
	w.number( v.bodyBytes, 8) ;
	w.raw( v.bodySha256) ;
}
//------------------------------------------------------------------------------
/**
 * @brief cursorを復元します
 * @param[in,out] r reader
 * @return 小値
 */
//------------------------------------------------------------------------------
WholeTextBoundedCursor readCursor( Reader &r)
{
	WholeTextBoundedCursor v ; // 外部保存値
	v.count = static_cast<std::uint32_t>( r.number( 4)) ;
	v.coreEnd = static_cast<std::uint64_t>( r.number( 8)) ;
	v.bodyBytes = static_cast<std::uint64_t>( r.number( 8)) ;
	r.raw( v.bodySha256) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief closeを保存します
 * @param[in,out] w writer
 * @param[in] v 小値
 */
//------------------------------------------------------------------------------
void writeClose( Writer &w, const WholeTextNativeCloseReceipt &v)
{
	w.number( static_cast<std::uint32_t>( v.ownership)) ;
	w.number( v.attempted, 4) ;
	w.number( v.responseHeaderValid, 4) ;
	w.number( v.dispositionConsistent, 4) ;
	w.number( v.releaseConfirmed, 4) ;
	w.number( v.restartRequired, 4) ;
	w.number( v.rawStatus, 4) ;
	w.number( v.rawStructSize, 4) ;
	w.number( v.rawAbiVersion, 4) ;
	w.number( v.rawClosed, 4) ;
	w.number( v.rawDiagnostic, 4) ;
	w.number( std::bit_cast<std::uint32_t>( v.rawEngineCode)) ;
	w.number( v.rawReserved, 4) ;
}
//------------------------------------------------------------------------------
/**
 * @brief closeを復元します
 * @param[in,out] r reader
 * @return 小値
 */
//------------------------------------------------------------------------------
WholeTextNativeCloseReceipt readClose( Reader &r)
{
	WholeTextNativeCloseReceipt v ; // 外部保存値
	v.ownership = static_cast<WholeTextNativeOwnership>( r.number()) ;
	v.attempted = r.flag() ;
	v.responseHeaderValid = r.flag() ;
	v.dispositionConsistent = r.flag() ;
	v.releaseConfirmed = r.flag() ;
	v.restartRequired = r.flag() ;
	v.rawStatus = static_cast<std::uint32_t>( r.number( 4)) ;
	v.rawStructSize = static_cast<std::uint32_t>( r.number( 4)) ;
	v.rawAbiVersion = static_cast<std::uint32_t>( r.number( 4)) ;
	v.rawClosed = static_cast<std::uint32_t>( r.number( 4)) ;
	v.rawDiagnostic = static_cast<std::uint32_t>( r.number( 4)) ;
	v.rawEngineCode = std::bit_cast<std::int32_t>( static_cast<std::uint32_t>( r.number())) ;
	v.rawReserved = static_cast<std::uint32_t>( r.number( 4)) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief 補正試行の終端field群だけを固定順にwriterへ追加します
 * @param[in,out] w 現cursorから逐次追記する有界writerです。失敗前のprefixは巻き戻しません
 * @param[in] v outcome、失敗chunk、error、資源失敗、close状態等を読む終端記録のborrowです
 * @param[in] state 保存する試行状態のenumです。本文・plan・receipt全体はここでは保存しません
 */
//------------------------------------------------------------------------------
void writeTerminal( Writer &w, const WholeTextBoundedResult &v, WholeTextBoundedAttemptState state)
{
	w.number( static_cast<std::uint32_t>( state)) ;
	w.number( static_cast<std::uint32_t>( v.outcome)) ;
	w.number( v.failedIndex.has_value()) ;
	w.number( v.failedIndex.value_or( 0)) ;
	w.number( v.failedCore.begin, 8) ;
	w.number( v.failedCore.end, 8) ;
	w.number( static_cast<std::uint32_t>( v.errorKind)) ;
	w.number( v.errorCode) ;
	w.number( v.nativeStatus) ;
	w.number( v.nativeDiagnostic) ;
	w.number( std::bit_cast<std::uint32_t>( v.nativeEngineCode)) ;
	w.number( v.resourceFailure.reason) ;
	w.number( v.resourceFailure.code) ;
	writeClose( w, v.close) ;
	w.number( v.restartRequired) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存終端field群を順次読み、固定enum範囲を検証して復元します
 * @param[in,out] r 現cursorからfieldを消費するreaderです。失敗時も消費済み位置を巻き戻しません
 * @param[out] state 最初のfieldから設定する試行状態です。後段の検証失敗時にも代入済み値が残ります
 * @return 検証済みの終端field群を持つ所有値です。本文・plan・receiptは含まず、未完成値は返しません
 * @throws HistoryError 欠落field、不正flag、範囲外enumではCORRUPTを伝えます
 */
//------------------------------------------------------------------------------
WholeTextBoundedResult readTerminal( Reader &r, WholeTextBoundedAttemptState &state)
{
	WholeTextBoundedResult v ; // 元terminal
	state = static_cast<WholeTextBoundedAttemptState>( r.number()) ;
	v.outcome = static_cast<WholeTextBoundedOutcome>( r.number()) ;
	const bool present = r.flag() ; // currentの有無
	const auto index = static_cast<std::uint32_t>( r.number()) ; // 元index
	require( present || index == 0) ;
	// 未完currentだけindexを復元します
	if( present) {
		v.failedIndex = index ;
	}
	v.failedCore = {r.number( 8), r.number( 8)} ;
	v.errorKind = static_cast<ErrorKind>( r.number()) ;
	v.errorCode = static_cast<std::uint32_t>( r.number()) ;
	v.nativeStatus = static_cast<std::uint32_t>( r.number()) ;
	v.nativeDiagnostic = static_cast<std::uint32_t>( r.number()) ;
	v.nativeEngineCode = std::bit_cast<std::int32_t>( static_cast<std::uint32_t>( r.number())) ;
	v.resourceFailure.reason = static_cast<std::uint32_t>( r.number()) ;
	v.resourceFailure.code = static_cast<std::uint32_t>( r.number()) ;
	v.close = readClose( r) ;
	v.restartRequired = r.flag() ;
	require( static_cast<std::uint32_t>( state) <= 6 && static_cast<std::uint32_t>( v.outcome) <= 2 &&
			static_cast<std::uint32_t>( v.errorKind) <= static_cast<std::uint32_t>( ErrorKind::TEMPORARY_STORAGE) &&
			v.resourceFailure.reason <= 2 && static_cast<std::uint32_t>( v.close.ownership) <= 2) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief descriptorの可変長部を書きます
 * @param[in,out] w writer
 * @param[in] v 証拠
 */
//------------------------------------------------------------------------------
void writeDescriptor( Writer &w, const WholeTextBoundedDescriptor &v)
{
	const auto b = detail::encodeBoundedDescriptor( v) ; /* 小さい証拠だけ */
	w.number( b.size()) ;
	w.raw( b) ;
}
//------------------------------------------------------------------------------
/**
 * @brief descriptorを範囲付きで読みます
 * @param[in,out] r reader
 * @return 証拠
 */
//------------------------------------------------------------------------------
WholeTextBoundedDescriptor readDescriptor( Reader &r)
{
	const auto n = r.number() ; /* 外部長さ */
	require( n <= 2048 && n <= r.left.size()) ;
	// この同期境界で所有する値、または確定状態の検証値です
	auto v = detail::decodeBoundedDescriptor( r.left.first( static_cast<std::size_t>( n))) ; /* 検証済み証拠 */
	r.left = r.left.subspan( static_cast<std::size_t>( n)) ;
	return v ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief descriptorの範囲/状態/closeを検証します
 * @param[in] v 全値
 * @see koefude::detail::validateBoundedDescriptor
 */
//------------------------------------------------------------------------------
void detail::validateBoundedDescriptor( const WholeTextBoundedDescriptor &v)
{
	const auto &b = v.result.binding ; // 封印値
	const auto &c = v.result.committed ; // 耐久cursor
	const auto &close = v.result.close ; // 元close
	require( v.journalKey != WholeTextJournalKey{} && v.fileIdentity[0] != 0 && v.fileIdentity[1] != 0 &&
			v.reservedFileBytes == 25231360 && v.headerSha256 != WholeTextDigest{} &&
			v.result.request.operationId > 0 && v.result.request.source.historyId > 0 &&
			v.result.request.profileId.size() > 0 && v.result.request.profileId.size() <= 64 &&
			v.result.request.profileSha256 != WholeTextDigest{}) ;
	require( v.state >= WholeTextBoundedAttemptState::COMPLETE && v.state <= WholeTextBoundedAttemptState::INTERRUPTED &&
			v.result.pcm.version == 1 && v.result.pcm.sampleRate == 16000 && v.result.pcm.channels == 1 &&
			v.result.pcm.originSample == 0 && v.result.pcm.sampleCount > 0 &&
			v.result.pcm.pcmSha256 != WholeTextDigest{}) ;
	require( b.recordCount > 0 && b.recordCount <= 16384 && b.generationCount > 0 &&
			b.generationCount <= b.recordCount && b.originalBytes > 0 && b.originalBytes <= 8388608 && b.measures > 0 &&
			b.measures <= 262144 && b.measuredBytes <= 268435456 && b.planSha256 != WholeTextDigest{} &&
			b.openBinding != WholeTextDigest{} && b.inputSha256 == boundedInputBinding( b.openBinding, b.planSha256)) ;
	require( c.count <= b.recordCount && c.coreEnd <= b.originalBytes && c.bodyBytes <= 8388608 &&
			(c.count != 0 || (c.coreEnd == 0 && c.bodyBytes == 0)) &&
			(c.count == 0 || (c.coreEnd > 0 && c.bodyBytes > 0)) && static_cast<std::uint32_t>( v.result.outcome) <= 2 &&
			static_cast<std::uint32_t>( v.result.errorKind) <=
				static_cast<std::uint32_t>( ErrorKind::TEMPORARY_STORAGE) &&
			static_cast<std::uint32_t>( close.ownership) <= 2) ;
	// Completeは全coverageと正常な明示closeだけで成立します
	if( v.state == WholeTextBoundedAttemptState::COMPLETE) {
		require( v.result.outcome == WholeTextBoundedOutcome::READY_FOR_PERSISTENCE && c.count == b.recordCount &&
				c.coreEnd == b.originalBytes && !v.result.failedIndex && !v.result.restartRequired &&
				close.ownership == WholeTextNativeOwnership::CONFIRMED_RELEASED && close.attempted &&
				close.responseHeaderValid && close.dispositionConsistent && close.releaseConfirmed &&
				!close.restartRequired && close.rawStatus == 0 && close.rawClosed == 1 && close.rawReserved == 0) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief descriptorを固定LE形式へ変換します
 * @param[in] v 証拠
 * @return 最大2048byte
 * @see koefude::detail::encodeBoundedDescriptor
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> detail::encodeBoundedDescriptor( const WholeTextBoundedDescriptor &v)
{
	validateBoundedDescriptor( v) ;
	Writer w ; /* 小さい証拠 */
	w.domain( "koefude.whole_text_bounded_descriptor.v1") ;
	w.number( 1) ;
	w.raw( v.journalKey) ;
	w.number( v.fileIdentity[0], 8) ;
	w.number( v.fileIdentity[1], 8) ;
	w.number( v.reservedFileBytes, 8) ;
	w.raw( v.headerSha256) ;
	writeRequest( w, v.result.request) ;
	writePcm( w, v.result.pcm) ;
	writeBinding( w, v.result.binding) ;
	writeCursor( w, v.result.committed) ;
	writeTerminal( w, v.result, v.state) ;
	w.raw( wholeTextSha256( w.bytes)) ;
	require( w.bytes.size() <= 2048) ;
	return std::move( w.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief descriptorを厳密に復元します
 * @param[in] bytes 全encoding
 * @return 未開封Jの証拠
 * @see koefude::detail::decodeBoundedDescriptor
 */
//------------------------------------------------------------------------------
WholeTextBoundedDescriptor detail::decodeBoundedDescriptor( std::span<const std::uint8_t> bytes)
{
	require( bytes.size() <= 2048 && bytes.size() >= 32) ;
	Reader r( bytes) ; /* 同期borrow */
	r.domain( "koefude.whole_text_bounded_descriptor.v1") ;
	require( r.number() == 1) ;
	WholeTextBoundedDescriptor v ; /* 全検証前は非公開 */
	r.raw( v.journalKey) ;
	v.fileIdentity = {r.number( 8), r.number( 8)} ;
	v.reservedFileBytes = r.number( 8) ;
	r.raw( v.headerSha256) ;
	auto request = readRequest( r) ; /* 要求 */
	auto pcm = readPcm( r) ; /* 観測済みPCM */
	auto binding = readBinding( r) ; /* 封印 */
	auto cursor = readCursor( r) ; /* 確定分 */
	v.result = readTerminal( r, v.state) ;
	v.result.request = std::move( request) ;
	v.result.pcm = pcm ;
	v.result.binding = binding ;
	v.result.committed = cursor ;
	WholeTextDigest sha{} ; /* 外部SHA */
	r.raw( sha) ;
	require( r.left.empty() && sha == wholeTextSha256( bytes.first( bytes.size() - 32))) ;
	validateBoundedDescriptor( v) ;
	require( encodeBoundedDescriptor( v) == std::vector<std::uint8_t>( bytes.begin(), bytes.end())) ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief V7 entry拡張を変換します
 * @param[in] id 元ID
 * @param[in] v record
 * @return 最大8192byte
 * @see koefude::detail::encodeBoundedRecord
 */
//------------------------------------------------------------------------------
std::vector<std::uint8_t> detail::encodeBoundedRecord( std::uint64_t id, const WholeTextBoundedRecord &v)
{
	Writer w ; // 一entryだけ
	w.number( id, 8) ;
	w.number( static_cast<std::uint32_t>( v.active)) ;
	w.number( v.effective.has_value()) ;
	// 有効証拠は本文を複製しません
	if( v.effective) {
		writeDescriptor( w, *v.effective) ;
	}
	w.number( static_cast<std::uint32_t>( v.latestState)) ;
	w.number( v.latestRequest.has_value()) ;
	// Jなし失敗も元要求を保持します
	if( v.latestRequest) {
		writeRequest( w, *v.latestRequest) ;
	}
	w.number( v.latestTerminal.has_value()) ;
	// 観測済みのPCM/bindingだけを書きます
	if( v.latestTerminal) {
		w.number( v.pcmObserved) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( v.pcmObserved) {
			writePcm( w, v.latestTerminal->pcm) ;
		}
		w.number( v.bindingObserved) ;
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( v.bindingObserved) {
			writeBinding( w, v.latestTerminal->binding) ;
		}
		writeCursor( w, v.latestTerminal->committed) ;
		writeTerminal( w, *v.latestTerminal, v.latestState) ;
	}
	w.number( v.latest.has_value()) ;
	// descriptorは小さい固定上限です
	if( v.latest) {
		writeDescriptor( w, *v.latest) ;
	}
	w.number( v.inFlight.has_value()) ;
	// 予約は最新終端と独立します
	if( v.inFlight) {
		w.number( static_cast<std::uint32_t>( v.inFlight->state)) ;
		writeRequest( w, v.inFlight->request) ;
		w.raw( v.inFlight->reservationKey) ;
	}
	w.number( v.saveErrorPresent) ;
	w.number( v.saveErrorKind) ;
	w.number( v.saveErrorCode) ;
	w.number( v.prefixSelectionEligible) ;
	require( w.bytes.size() <= 8192) ;
	std::size_t consumed = 0 ; // 自身の形式整合も同じreaderで検査
	static_cast<void>( decodeBoundedRecord( w.bytes, id, consumed)) ;
	return std::move( w.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief V7 entry拡張を全消費します
 * @param[in] bytes 未読byte
 * @param[in] id 期待ID
 * @param[out] consumed 消費数
 * @return 検証済みrecord。bodyはplatformで復元します
 * @see koefude::detail::decodeBoundedRecord
 */
//------------------------------------------------------------------------------
WholeTextBoundedRecord detail::decodeBoundedRecord( std::span<const std::uint8_t> bytes, std::uint64_t id,
												   std::size_t &consumed)
{
	Reader				   r( bytes) ; // 呼出し元の未読範囲
	WholeTextBoundedRecord v ; // 部分結果を公開しません
	require( r.number( 8) == id) ;
	v.active = static_cast<WholeTextActiveFullKind>( r.number()) ;
	require( static_cast<std::uint32_t>( v.active) <= 2) ;
	// 保存済み有効証拠を復元します
	if( r.flag()) {
		v.effective = readDescriptor( r) ;
		require( v.effective->state == WholeTextBoundedAttemptState::COMPLETE) ;
	}
	v.latestState = static_cast<WholeTextBoundedAttemptState>( r.number()) ;
	require( static_cast<std::uint32_t>( v.latestState) <= 6) ;
	// 要求のpresenceを維持します
	if( r.flag()) {
		v.latestRequest = readRequest( r) ;
	}
	// J生成前の失敗には未観測PCMを捏造しません
	if( r.flag()) {
		v.pcmObserved = r.flag() ;
		DecodedPcmIdentity pcm ; /* 未観測なら使用しません */
		if( v.pcmObserved) {
			pcm = readPcm( r) ;
		}
		v.bindingObserved = r.flag() ;
		WholeTextBoundedPromotion binding ; /* 封印済みだけ有効 */
		if( v.bindingObserved) {
			binding = readBinding( r) ;
		}
		const auto					 cursor = readCursor( r) ; /* prefix */
		WholeTextBoundedAttemptState state ; /* 二重分類の照合 */
		v.latestTerminal = readTerminal( r, state) ;
		require( state == v.latestState && v.latestRequest.has_value()) ;
		v.latestTerminal->request = *v.latestRequest ;
		v.latestTerminal->pcm = pcm ;
		v.latestTerminal->binding = binding ;
		v.latestTerminal->committed = cursor ;
		require( v.bindingObserved || cursor.count == 0) ;
	}
	// 最新descriptorはterminalとbyte単位で一致させます
	if( r.flag()) {
		v.latest = readDescriptor( r) ;
		require( v.latestTerminal && v.pcmObserved && v.bindingObserved) ;
		auto d = *v.latest ; /* 重複値の比較用 */
		d.result = *v.latestTerminal ;
		d.state = v.latestState ;
		require( encodeBoundedDescriptor( d) == encodeBoundedDescriptor( *v.latest)) ;
	}
	// 新規予約を前終端へ混ぜません
	if( r.flag()) {
		WholeTextBoundedInFlight f ; /* 耐久予約 */
		f.state = static_cast<WholeTextBoundedAttemptState>( r.number()) ;
		require( f.state == WholeTextBoundedAttemptState::PENDING || f.state == WholeTextBoundedAttemptState::RUNNING) ;
		f.request = readRequest( r) ;
		r.raw( f.reservationKey) ;
		require( f.request.source.historyId == id && f.reservationKey != WholeTextJournalKey{}) ;
		v.inFlight = std::move( f) ;
	}
	v.saveErrorPresent = r.flag() ;
	v.saveErrorKind = static_cast<std::uint32_t>( r.number()) ;
	v.saveErrorCode = static_cast<std::uint32_t>( r.number()) ;
	v.prefixSelectionEligible = r.flag() ;
	require( v.saveErrorKind <= 7 && (v.saveErrorPresent || (v.saveErrorKind == 7 && v.saveErrorCode == 0))) ;
	require( !v.prefixSelectionEligible || (v.latest && v.latest->result.committed.count > 0)) ;
	require( (v.latestState == WholeTextBoundedAttemptState::IDLE) == !v.latestRequest.has_value()) ;
	require( v.latestState != WholeTextBoundedAttemptState::COMPLETE ||
			(v.latest && v.effective && encodeBoundedDescriptor( *v.latest) == encodeBoundedDescriptor( *v.effective))) ;
	require( v.active != WholeTextActiveFullKind::BOUNDED || v.effective.has_value()) ;
	// 全descriptorのsource IDを同じentryへ束縛します
	for( const auto *d : {&v.effective, &v.latest}) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( *d) {
			require( (*d)->result.request.source.historyId == id) ;
		}
	}
	consumed = bytes.size() - r.left.size() ;
	require( consumed <= 8192) ;
	v.saveState = v.saveErrorPresent ? WholeTextBoundedSaveState::SAVE_FAILED : WholeTextBoundedSaveState::COMMITTED ;
	return v ;
}
//------------------------------------------------------------------------------
/**
 * @brief 元pointerと制御blockを照合します
 * @param[in] a owner
 * @param[in] b owner
 * @return 同一ならtrue
 * @see koefude::detail::sameTranscriptOwner
 */
//------------------------------------------------------------------------------
bool detail::sameTranscriptOwner( const std::shared_ptr<const Transcript> &a,
								 const std::shared_ptr<const Transcript> &b) noexcept
{
	return a && a == b && !a.owner_before( b) && !b.owner_before( a) ;
}
//------------------------------------------------------------------------------
/**
 * @brief bindingを既存domain/幅で計算します
 * @param[in] q 全要求
 * @param[in] aux aux SHA
 * @return open binding
 * @see koefude::detail::boundedOpenBinding
 */
//------------------------------------------------------------------------------
WholeTextDigest detail::boundedOpenBinding( const WholeTextCorrectionRequest &q, const WholeTextDigest &aux)
{
	std::vector<std::uint8_t> bytes ; // 最大512byte
	const std::string_view	  domain = "koefude.whole_text_bounded_request.v2" ; // NULなし
	bytes.insert( bytes.end(), domain.begin(), domain.end()) ;
	const auto append =
		//------------------------------------------------------------------------------
		/**
		 * @brief 元domainと同じu64長さでfieldを追加します
		 * @param[in] value 同期byte
		 */
		//------------------------------------------------------------------------------
		[&bytes]( std::span<const std::uint8_t> value)
		{
			const auto n = value.size() ; /* 元長さ */
			for( unsigned i = 0; i < 8; ++i) {
				bytes.push_back( static_cast<std::uint8_t>( n >> (i * 8))) ;
			}
			bytes.insert( bytes.end(), value.begin(), value.end()) ;
		} ;
	const auto decimal =
		//------------------------------------------------------------------------------
		/**
		 * @brief 整数を既存decimal形式へ変換します
		 * @param[in] n 元ID
		 */
		//------------------------------------------------------------------------------
		[&append]( std::uint64_t n)
		{
			std::array<char, 32> b{} ; /* 固定decimal */
			const auto			 result = std::to_chars( b.data(), b.data() + b.size(), n) ; /* checked結果 */
			require( result.ec == std::errc{}) ;
			append( {reinterpret_cast<const std::uint8_t *>( b.data()), static_cast<std::size_t>( result.ptr - b.data())}) ;
		} ;
	decimal( q.source.historyId) ;
	append( q.source.sha256) ;
	decimal( q.operationId) ;
	append( {reinterpret_cast<const std::uint8_t *>( q.profileId.data()), q.profileId.size()}) ;
	append( q.profileSha256) ;
	append( aux) ;
	return wholeTextSha256( bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief planとopenを既存domainで束縛します
 * @param[in] open open値
 * @param[in] plan plan SHA
 * @return input SHA
 * @see koefude::detail::boundedInputBinding
 */
//------------------------------------------------------------------------------
WholeTextDigest detail::boundedInputBinding( const WholeTextDigest &open, const WholeTextDigest &plan)
{
	Writer w ; /* 固定domainと二SHA */
	w.domain( "koefude.whole_text_bounded_input.v2") ;
	w.number( 32, 8) ;
	w.raw( open) ;
	w.number( 32, 8) ;
	w.raw( plan) ;
	return wholeTextSha256( w.bytes) ;
}
//------------------------------------------------------------------------------
/**
 * @brief scalarとbody所有状態から操作を解決します
 * @param[in] r 不変record
 * @return 実装済み操作の可否
 * @see koefude::boundedActions
 */
//------------------------------------------------------------------------------
WholeTextBoundedActions boundedActions( const WholeTextBoundedRecord &r) noexcept
{
	WholeTextBoundedActions a ; // UI scalarだけ
	a.effective = r.effectiveText && r.effectiveText->complete() ;
	a.prefix = r.prefixSelectionEligible && r.latestText && r.latestState != WholeTextBoundedAttemptState::COMPLETE ;
	a.retrySave = r.saveState == WholeTextBoundedSaveState::SAVE_FAILED ;
	a.discard = !r.inFlight && r.latestRequest && r.latestState != WholeTextBoundedAttemptState::COMPLETE ;
	a.recreate =
		!r.inFlight && !a.retrySave && !(r.effective && r.latest && r.effective->journalKey != r.latest->journalKey) ;
	a.exportComplete = a.effective ;
	return a ;
}
//------------------------------------------------------------------------------
/**
 * @brief 元ownerを保ち表示意図だけ解決します
 * @param[in] p 保存した意図
 * @param[in] r 有界record
 * @param[in] v1 既存有効全文の有無
 * @param[in] reading 有効Readingの有無
 * @return 利用可能な表示。意図自体は書き換えません
 * @see koefude::resolveBoundedDisplay
 */
//------------------------------------------------------------------------------
ResultPreference resolveBoundedDisplay( ResultPreference p, const WholeTextBoundedRecord &r, bool v1,
									   bool reading) noexcept
{
	const auto a = boundedActions( r) ; // 利用可能性だけ
	// 明示原文は成功や復元でも保持します
	if( p == ResultPreference::ORIGINAL) {
		return p ;
	}
	// 未完prefixは明示選択だけで表示します
	if( p == ResultPreference::BOUNDED_PREFIX) {
		return a.prefix ? p : ResultPreference::ORIGINAL ;
	}
	// Automaticは保存済み有効全文だけ優先します
	if( p == ResultPreference::AUTOMATIC) {
		// この条件の対象だけを処理し、別の試行や所有資源へ適用しません
		if( (r.active == WholeTextActiveFullKind::BOUNDED && a.effective) || v1) {
			return ResultPreference::WHOLE_TEXT ;
		}
		return reading ? ResultPreference::LEGACY_FORMATTED : ResultPreference::ORIGINAL ;
	}
	// 明示全文が利用できない場合は原文へ表示だけ戻します
	if( p == ResultPreference::WHOLE_TEXT) {
		return a.effective || v1 ? p : ResultPreference::ORIGINAL ;
	}
	return reading ? ResultPreference::LEGACY_FORMATTED : ResultPreference::ORIGINAL ;
}
} // namespace koefude
