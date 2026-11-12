//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_identity.cpp
 * @brief CNGで全正規化PCMのbit列を有界に照合します
 * @version 1.0.0
 * @date 2026/10/19
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/19 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/19 16384sample単位の全量hashと独立進捗を追加
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
#include "core/audio_identity.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <windows.h>

// Win32基本型を定義してからCNG宣言を読み込みます
#include <bcrypt.h>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief CNGの元診断を保持します
 * @param[in] status API結果
 */
//------------------------------------------------------------------------------
void checkCrypto( const NTSTATUS status)
{
	// 未完成hashは返しません
	if( status < 0) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
}
/** @brief providerを全終了経路で閉じます */
struct AlgorithmDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有providerを閉じます
	 * @param[in] value 所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_ALG_HANDLE value) const noexcept
	{
		BCryptCloseAlgorithmProvider( value, 0) ;
	}
} ;
/** @brief hashを全終了経路で閉じます */
struct HashDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有hashを閉じます
	 * @param[in] value 所有handle
	 */
	//------------------------------------------------------------------------------
	void operator()( BCRYPT_HASH_HANDLE value) const noexcept
	{
		BCryptDestroyHash( value) ;
	}
} ;
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 全PCMを有界に読み同一性を採取します
 * @param[in] audio worker所有の全view。sourceとsamplesの同時使用は禁止
 * @param[in] stop 協調取消し
 * @param[in] progress 実sample進捗、空可
 * @return 全読取り成功後だけ返す実測値
 * @throws OperationError 短縮view、不正PCM、I/O異常、取消し
 * @see koefude::fingerprintDecodedPcm
 */
//------------------------------------------------------------------------------
DecodedPcmIdentity fingerprintDecodedPcm( const AudioData &audio, const std::stop_token stop,
										 const ProgressCallback &progress)
{
	static_assert( sizeof(float) == 4 && std::numeric_limits<float>::is_iec559) ;
	const auto count = audio.sampleCount() ; // durationから推測しない実整数
	const auto full = audio.source ? audio.source->size() : audio.samples.size() ; // 所有元全長
	// 全録音の証拠として短縮viewと二重所有を受理しません
	if( (audio.source && !audio.samples.empty()) || count == 0 || count != full || count > K_MAXIMUM_TIMELINE_SAMPLES) {
		throw OperationError( ErrorKind::INVALID_INPUT, 0x57550008) ;
	}
	checkCancelled( stop) ;
	BCRYPT_ALG_HANDLE rawAlgorithm = nullptr ; // CNG出力先
	checkCrypto( BCryptOpenAlgorithmProvider( &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) ;
	const std::unique_ptr<void, AlgorithmDeleter> algorithm( rawAlgorithm) ; // provider所有
	BCRYPT_HASH_HANDLE							  rawHash = nullptr ; // CNG出力先
	checkCrypto( BCryptCreateHash( algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0)) ;
	const std::unique_ptr<void, HashDeleter> hash( rawHash) ; // hash所有
	std::array<float, 16384>				 samples{} ; // 全録音に依存しない固定buffer
	std::array<std::uint8_t, 16384 * 4>		 bytes{} ; // 明示little endian表現
	// 先頭から末尾まで重複せず全bitを読みます
	for( std::uint64_t offset = 0; offset < count;) { // 完了sample数
		checkCancelled( stop) ;
		const auto size = static_cast<std::size_t>( std::min<std::uint64_t>( samples.size(), count - offset)) ; // 今回分
		audio.readSamples( offset, std::span( samples.data(), size)) ;
		// 有限な正規化PCMだけを、負0も修理せずbit列へ写します
		for( std::size_t index = 0; index < size; ++index) { // buffer内sample番号
			const auto sample = samples[index] ; // 入力値
			// decode後の契約違反を認識成功へ通しません
			if( !std::isfinite( sample) || sample < -1 || sample > 1) {
				throw OperationError( ErrorKind::INVALID_INPUT, 0x57550008) ;
			}
			const auto bits = std::bit_cast<std::uint32_t>( sample) ; // 値変換なし
			// binary32の4byte全てを順序固定します
			for( std::size_t byte = 0; byte < 4; ++byte) { // little endian byte位置
				bytes[index * 4 + byte] = static_cast<std::uint8_t>( bits >> (byte * 8)) ;
			}
		}
		checkCrypto( BCryptHashData( hash.get(), bytes.data(), static_cast<ULONG>( size * 4), 0)) ;
		offset += size ;
		// decode100%とは独立した実測phaseです
		if( progress) {
			progress( {Stage::VALIDATING, static_cast<int>( offset * 100 / count), WholeTextPhase::AUDIO_IDENTITY}) ;
		}
	}
	DecodedPcmIdentity result ; // 成功時だけ返す小さい値
	result.sampleCount = count ;
	checkCrypto( BCryptFinishHash( hash.get(), result.pcmSha256.data(), 32, 0)) ;
	checkCancelled( stop) ;
	return result ;
}
} // namespace koefude
