//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file flac_format.cpp
 * @brief FLAC marker、STREAMINFO、metadataの範囲と整数境界を検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 固定参照器と同じ拒否順序のpure検証器を追加
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
#include "platform/flac_format.h"

#include "core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

namespace koefude {
namespace {

constexpr std::uint64_t K_MAXIMUM_TOTAL_SAMPLES = (std::uint64_t{1} << 36) - 1 ; // 36bitの総数
constexpr std::uint32_t K_MAXIMUM_METADATA_BLOCKS = 4096 ; // 有界なmetadata走査件数

//------------------------------------------------------------------------------
/**
 * @brief 最大34byteの同期readを範囲と取消しの前後検査付きで行います
 * @param[in] fileBytes 元streamの実byte数
 * @param[in] readAt 呼出し中だけ借用する非空callback
 * @param[in] offset 元stream基準のbyte位置
 * @param[out] output 呼出し中だけ借用する固定buffer。失敗時は内容未確定
 * @param[in] stop 前後で確認する協調的取消しtoken
 * @throws OperationError 取消し、範囲違反、短read、過大返却
 * @note callbackの例外と元codeは変更せず伝播します
 */
//------------------------------------------------------------------------------
void					readExact( const std::uint64_t fileBytes, const FlacRead &readAt, const std::uint64_t offset,
								  const std::span<std::byte> output, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 加算せず残量で照合し、callbackへ範囲外や過大な要求を渡さない
	if( offset > fileBytes || output.empty() || output.size() > 34 || output.size() > fileBytes - offset) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto count = readAt( offset, output) ; // callbackの実read量
	checkCancelled( stop) ;
	// 取消し確認後に短readと契約違反の過大返却を区別せず拒否する
	if( count != output.size()) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 固定field幅のbig-endian整数をunsigned値へ復元します
 * @param[in] data 1～8byteの呼出し中だけ借用する固定field
 * @return fieldのuint64値
 * @pre 呼出元はSTREAMINFO/header内の固定幅だけを渡します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t bigEndian( const std::span<const std::byte> data)
{
	std::uint64_t value = 0 ; // 復元するunsigned整数
	// 固定幅の終端まで上位byteから取り込む
	for( const auto byte : data) { // 現在のfield byte
		value = (value << 8) | std::to_integer<std::uint64_t>( byte) ;
	}
	return value ;
}

//------------------------------------------------------------------------------
/**
 * @brief 検証済み34byteから候補属性を解析します
 * @param[in] data 呼出し中だけ借用するSTREAMINFO payload
 * @return 構造・対応範囲を確認した所有metadata。位置と件数は呼出元が補います
 * @throws OperationError 構造不正または初回対応範囲外
 */
//------------------------------------------------------------------------------
[[nodiscard]] FlacInputInfo streamInfo( const std::span<const std::byte, 34> data)
{
	FlacInputInfo info ; // 失敗時は外へ返さない所有属性
	info.minimumBlockSamples = static_cast<std::uint32_t>( bigEndian( data.first<2>())) ;
	info.maximumBlockSamples = static_cast<std::uint32_t>( bigEndian( data.subspan<2, 2>())) ;
	info.minimumFrameBytes = static_cast<std::uint32_t>( bigEndian( data.subspan<4, 3>())) ;
	info.maximumFrameBytes = static_cast<std::uint32_t>( bigEndian( data.subspan<7, 3>())) ;
	const auto packed = bigEndian( data.subspan<10, 8>()) ; // rate/channel/depth/36bit総数
	info.rate = static_cast<std::uint32_t>( packed >> 44) ;
	info.channels = static_cast<std::uint32_t>( (packed >> 41) & 7) + 1 ;
	info.bits = static_cast<std::uint32_t>( (packed >> 36) & 31) + 1 ;
	info.totalSamples = packed & K_MAXIMUM_TOTAL_SAMPLES ;
	// 仕様上の構造矛盾を未対応形式の判定より先に拒否する
	if( info.minimumBlockSamples < 16 || info.minimumBlockSamples > info.maximumBlockSamples ||
	   (info.minimumFrameBytes != 0 && info.maximumFrameBytes != 0 &&
		info.minimumFrameBytes > info.maximumFrameBytes) ||
	   info.bits < 4) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// 有効な形式でも初回候補にない属性と未知総数は未対応として区別する
	if( info.rate < 8000 || info.rate > 192000 || info.channels > 2 || (info.bits != 16 && info.bits != 24) ||
	   info.totalSamples == 0) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	info.idealOutputSamples = flacIdealOutputSamples( info.totalSamples, info.rate) ;
	// MD5存在だけを記録し、checksumの一致を推定しない
	for( const auto byte : data.subspan<18, 16>()) { // MD5欄の現在byte
		info.md5Present = info.md5Present || byte != std::byte{0} ;
	}
	return info ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 既知総sample数を商/余りで16kHzへ数学的に切り上げ換算します
 * @param[in] totalSamples 元channel当たりの正の36bit総sample数
 * @param[in] rate 8～192kHzの元周波数（Hz）
 * @return 既存時刻表現内の理想sample数。実MF出力数ではありません
 * @throws OperationError 不正属性はDecodeFailed、時刻表現超過はInputTooLarge
 * @see koefude::flacIdealOutputSamples
 */
//------------------------------------------------------------------------------
std::uint64_t flacIdealOutputSamples( const std::uint64_t totalSamples, const std::uint32_t rate)
{
	// division前にrateと36bitの正の総数を確認する
	if( totalSamples == 0 || totalSamples > K_MAXIMUM_TOTAL_SAMPLES || rate < 8000 || rate > 192000) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto whole = totalSamples / rate ; // 先に積を取らない整数部分
	const auto remainder = totalSamples % rate ; // rate未満の余り
	const auto value = whole * K_SAMPLE_RATE + (remainder * K_SAMPLE_RATE + rate - 1) / rate ; // 36bit内で安全な切上げ
	// 理想換算も既存timestampの表現範囲を超えさせない
	if( value > K_MAXIMUM_TIMELINE_SAMPLES) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	return value ;
}

//------------------------------------------------------------------------------
/**
 * @brief native FLACの固定属性と全metadataの宣言範囲を検証します
 * @param[in] fileBytes 同じstreamの実byte数。宣言時間から求めないこと
 * @param[in] readAt 空でない同期callback。原streamを変更しないこと
 * @param[in] stop 各read前後と終了前で確認する協調的取消しtoken
 * @return 確定した所有metadata。失敗時は途中の結果を返しません
 * @throws OperationError 入力不正、破損、未対応形式、資源上限、取消し
 * @note 最大34byte/read、4,096block、一定memoryです。録音時間上限は設けません
 * callbackの元例外と診断codeをそのまま伝播します。音声payloadを読みません
 * frame/CRC/MD5やmetadata payload内部の妥当性を検証しません
 * @see koefude::inspectNativeFlac
 */
//------------------------------------------------------------------------------
FlacInputInfo inspectNativeFlac( const std::uint64_t fileBytes, const FlacRead &readAt, const std::stop_token stop)
{
	// 非空callbackをhost契約とし、bad_function_callへ転化させない
	if( !readAt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// marker未満の実長は取消しや内容判定より先に破損として拒否する
	if( fileBytes < 4) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	std::array<std::byte, 4> marker{} ; // native/Ogg/ID3識別用の固定領域
	readExact( fileBytes, readAt, 0, marker, stop) ;
	// Ogg FLACと先頭ID3は初回候補のnative形式から分ける
	if( marker == std::array{std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}} ||
	   (marker[0] == std::byte{'I'} && marker[1] == std::byte{'D'} && marker[2] == std::byte{'3'})) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	// native marker以外の偽装やjunkを破損として拒否する
	if( marker != std::array{std::byte{'f'}, std::byte{'L'}, std::byte{'a'}, std::byte{'C'}}) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	FlacInputInfo info ; // 全metadata確認後にだけ返す所有属性
	std::uint64_t position = 4 ; // 次のmetadata headerの元byte位置
	bool		  last = false ; // 終端flag確認済みかどうか
	// 終端flagまで走査し、件数上限で実行時間も有界にする
	while( !last) {
		// 4,097件目の読取りを開始せず資源上限を返す
		if( info.metadataBlocks == K_MAXIMUM_METADATA_BLOCKS) {
			throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
		}
		std::array<std::byte, 4> header{} ; // 現在のmetadata headerだけの領域
		readExact( fileBytes, readAt, position, header, stop) ;
		const auto flags = std::to_integer<unsigned>( header[0]) ; // last flagと7bit型
		last = (flags & 0x80) != 0 ;
		const auto kind = flags & 0x7F ; // 現在のmetadata型
		const auto size = bigEndian( std::span( header).subspan<1, 3>()) ; // 24bit宣言byte数
		const auto start = position + 4 ; // read_exactが残量4byteを確認済みの位置
		// 先に実長へ照合し、予約型で範囲逸脱を覆い隠さない
		if( size > fileBytes - start) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		// 禁止型と先頭STREAMINFO違反を予約型の分類より先に拒否する
		if( kind == 127 || (info.metadataBlocks == 0 && kind != 0)) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		// 予約型を勝手にskipせず未対応として返す
		if( kind >= 7) {
			throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
		}
		// 唯一のSTREAMINFOだけを固定領域へ読んで解析する
		if( kind == 0) {
			// 重複または34byte以外のSTREAMINFOを破損として拒否する
			if( info.metadataBlocks != 0 || size != 34) {
				throw OperationError( ErrorKind::DECODE_FAILED) ;
			}
			std::array<std::byte, 34> data{} ; // 唯一確保するmetadata payload
			readExact( fileBytes, readAt, start, data, stop) ;
			info = streamInfo( data) ;
		}
		position = start + size ;
		++info.metadataBlocks ;
	}
	checkCancelled( stop) ;
	// last flagだけで成功にせず、音声領域を少なくとも1byte必要とする
	if( position == fileBytes) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	info.audioOffset = position ;
	info.audioBytes = fileBytes - position ;
	return info ;
}

} // namespace koefude
