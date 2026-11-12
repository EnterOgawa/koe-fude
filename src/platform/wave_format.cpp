//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file wave_format.cpp
 * @brief PCM16 RIFF/WAVEの構造を部分読取りで検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 破損音声の部分成功を防ぐ有界検証を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 分岐意図を明記
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
#include "platform/wave_format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <string_view>

#include "core/types.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 1～4byteのlittle-endian整数をalignment依存なしで復号します
 * @param[in] bytes 呼出し中に生存する検証済み短いspan
 * @return uint64へ拡張した値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t littleInteger( const std::span<const std::byte> bytes)
{
	std::uint64_t result = 0 ; // 符号拡張しない復号先
	// 固定幅の全byteを、境界を越えるpointer castなしで積算する
	for( std::size_t index = 0; index < bytes.size(); ++index) { // 現在復号するbyte番号
		result |= std::to_integer<std::uint64_t>( bytes[index]) << (index * 8) ;
	}
	return result ;
}

//------------------------------------------------------------------------------
/**
 * @brief FOURCCを借用ASCII文字列と比較します
 * @param[in] bytes 呼出し中に有効な4byte span
 * @param[in] expected NULL終端を不要とする4文字の静的識別子
 * @return 全byteが一致した場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool matches( const std::span<const std::byte> bytes, const std::string_view expected)
{
	// 内部境界でも長さの不一致を先に拒否する
	if( bytes.size() != expected.size()) {
		return false ;
	}
	// 識別子の全byteをASCIIのunsigned値として比較する
	for( std::size_t index = 0; index < bytes.size(); ++index) { // 識別子内のbyte番号
		// 対象形式の識別子以外は一つの不一致で除外する
		if( std::to_integer<unsigned char>( bytes[index]) != static_cast<unsigned char>( expected[index])) {
			return false ;
		}
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 読取り量・実体境界・取消しを確認してcallbackを呼びます
 * @param[in] fileBytes 同じstreamの実byte数
 * @param[in] offset 原stream基準のbyte位置
 * @param[out] destination 最大18byteの呼出し中だけ有効な出力。失敗時は内容未確定
 * @param[in] readAt 同期の借用reader
 * @param[in] stop 協調的な取消しtoken
 * @throws OperationError 範囲外または短い読取り、取消し
 */
//------------------------------------------------------------------------------
void readExact( const std::uint64_t fileBytes, const std::uint64_t offset, const std::span<std::byte> destination,
			   const WaveRead &readAt, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 加算前に残り量を確認し、callbackへ不正なspan範囲を渡さない
	if( offset > fileBytes || destination.empty() || destination.size() > 18 ||
	   destination.size() > fileBytes - offset) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto read = readAt( offset, destination) ; // readerが実際に満たしたbyte数
	// 部分読取りをzero初期値で補って検証成功にしない
	if( read != destination.size()) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief RIFF/WAVEのPCM16構造とbyte/sample境界を検証します
 * @param[in] fileBytes 同じ読取りstreamの実byte数。宣言長だけを渡さないこと
 * @param[in] readAt 空でない同期callback。参照先は本呼出し中に生存すること
 * @param[in] stop 各read前と終了前に確認する協調的な取消しtoken
 * @return 確定済みの元属性とPCM範囲。失敗時はmetadataを返しません
 * @throws OperationError 破損、未対応形式、上限超過、callback不正、取消し
 * @note callbackの元例外は変更せず伝播します。最大4,096chunk・18byte/read
 * metadata/audio payloadの割当て・全量読取りを行わず、原streamを変更しません
 * @see koefude::inspectPcm16Wave
 */
//------------------------------------------------------------------------------
WaveInputInfo inspectPcm16Wave( const std::uint64_t fileBytes, const WaveRead &readAt, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// reader未設定はfile形式の不良へ読み替えない
	if( !readAt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// containerの最小headerすら存在しない入力を先に拒否する
	if( fileBytes < 12) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// RIFFの32bit宣言長を超える実体を暗黙に切り捨てない
	if( fileBytes > K_MAXIMUM_RIFF_BYTES) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	std::array<std::byte, 18> buffer{} ; // header/短いfmtだけを所有する一定容量
	readExact( fileBytes, 0, std::span( buffer).first( 12), readAt, stop) ;
	const auto bytes = std::span<const std::byte>( buffer) ; // 今回のheaderを借用するview
	const auto magic = bytes.first( 4) ; // container識別子
	// 既知の別byte順・大容量containerをPCM16 RIFFへ読み替えない
	if( matches( magic, "RF64") || matches( magic, "RIFX") || matches( magic, "BW64") || matches( magic, "riff")) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	// 偽装拡張子や宣言長の不一致をSource Readerの部分成功へ渡さない
	if( !matches( magic, "RIFF") || !matches( bytes.subspan( 8, 4), "WAVE") ||
	   littleInteger( bytes.subspan( 4, 4)) + 8 != fileBytes) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	WaveInputInfo info ; // 成功した場合だけ返す所有metadata
	std::uint64_t position = 12 ; // 次の最上位chunk headerのbyte位置
	std::uint64_t align = 0 ; // fmtで確定した1frameのbyte数。0は未確認
	bool		  hasData = false ; // 重複dataと空音声の区別用
	// 全最上位chunkを実file終端まで走査し、payloadはreadせず位置で飛ばす
	while( position < fileBytes) {
		// 過大なmetadata件数を有限時間の設計範囲で停止する
		if( info.chunkCount == K_MAXIMUM_WAVE_CHUNKS) {
			throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
		}
		readExact( fileBytes, position, std::span( buffer).first( 8), readAt, stop) ;
		const auto kind = bytes.first( 4) ; // bufferを変更する前に判定するchunk識別子
		const auto size = littleInteger( bytes.subspan( 4, 4)) ; // paddingを含まないpayload byte数
		const auto start = position + 8 ; // read_exactの範囲確認済みのpayload先頭
		// 32bit宣言の加算前に、実体内の残り量と照合する
		if( size > fileBytes - start) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		const auto end = start + size ; // 実体内と確認したpayload終端
		const auto padding = size & 1U ; // 次chunkを偶数位置へ揃える0/1byte
		// 宣言payloadが収まっても末尾のpadding欠落は拒否する
		if( padding > fileBytes - end) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		// fmtは一つ・dataより前に確定し、既存属性を上書きしない
		if( matches( kind, "fmt ")) {
			// fmt重複・順序違反・不足属性は破損として扱う
			if( align != 0 || hasData || size < 16) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			readExact( fileBytes, start, std::span( buffer).first( 16), readAt, stop) ;
			const auto tag = littleInteger( bytes.first( 2)) ; // PCMまたは対象外codecのtag
			const auto channels = littleInteger( bytes.subspan( 2, 2)) ; // 元channel数
			const auto rate = littleInteger( bytes.subspan( 4, 4)) ; // 元sample周波数（Hz）
			const auto average = littleInteger( bytes.subspan( 8, 4)) ; // 宣言byte/秒
			const auto block = littleInteger( bytes.subspan( 12, 2)) ; // 宣言frame byte数
			const auto bits = littleInteger( bytes.subspan( 14, 2)) ; // 宣言sample bit数
			// 初回で許可したPCM16 mono/stereoとrateだけへ限定する
			if( tag != 1 || bits != 16 || channels < 1 || channels > 2 || rate < 8000 || rate > 192000) {
				throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
			}
			// PCM fmtは16byteまたはcbSize0の18byteだけを認める
			if( size != 16 && size != 18) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			// 18byte fmtの追加属性を存在しないcodec設定へ読み替えない
			if( size == 18) {
				readExact( fileBytes, start + 16, std::span( buffer).first( 2), readAt, stop) ;
				// PCM16に不要な拡張属性はこの契約で受け付けない
				if( littleInteger( bytes.first( 2)) != 0) {
					throw OperationError( ErrorKind::DECODE_FAILED) ;
				}
			}
			// 属性を相互照合し、半frameや誤った期間の算出を防ぐ
			if( block != channels * 2 || average != rate * block) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			info.rate = static_cast<std::uint32_t>( rate) ;
			info.channels = static_cast<std::uint32_t>( channels) ;
			align = block ;
		}
		// 一つの連続PCM payloadだけを受け付ける
		else if( matches( kind, "data")) {
			// fmt未確認、重複、空data、半frameを部分成功へ変換しない
			if( align == 0 || hasData || size == 0 || size % align != 0) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			info.dataOffset = start ;
			info.dataBytes = size ;
			hasData = true ;
		}
		position = end + padding ;
		++info.chunkCount ;
	}
	checkCancelled( stop) ;
	// 必須chunkなしを有効な無音と呼ばない
	if( align == 0 || !hasData) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	info.frames = info.dataBytes / align ;
	// RIFFの現在の境界が小さくても、byte/sampleの換算前に表現範囲を検証する
	if( info.frames > (std::numeric_limits<std::uint64_t>::max() - (info.rate - 1)) / K_SAMPLE_RATE) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	info.idealOutputSamples = (info.frames * K_SAMPLE_RATE + info.rate - 1) / info.rate ;
	// 後段の100ns時刻表現を越えるPCM数を渡さない
	if( info.idealOutputSamples > K_MAXIMUM_TIMELINE_SAMPLES) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	return info ;
}

} // namespace koefude
