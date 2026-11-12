//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file mp3_format.cpp
 * @brief MPEG-1/2 Layer IIIのframe境界とID3外枠を検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 最大10byte/readのpure MP3検証器を追加
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
#include "platform/mp3_format.h"

#include <algorithm>
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

constexpr std::array<std::uint32_t, 15> K_MPEG1_BITRATES{0,	  32,  40,	48,	 56,  64,  80, 96,
														 112, 128, 160, 192, 224, 256, 320} ; // kbps/index
constexpr std::array<std::uint32_t, 15> K_MPEG2_BITRATES{0,	 8,	 16, 24,  32,  40,	48, 56,
														 64, 80, 96, 112, 128, 144, 160} ; // kbps/index
constexpr std::array<std::uint32_t, 3>	K_MPEG1_RATES{44100, 48000, 32000} ; // Hz/index

/** @brief 1frameの確定属性と宣言byte/sample数です */
struct FrameInfo {
	/// MPEG versionの1または2
	std::uint32_t version ;
	/// 元周波数（Hz）
	std::uint32_t rate ;
	/// mono/stereoのchannel数
	std::uint32_t channels ;
	/// paddingを含むframeのbyte数
	std::uint64_t bytes ;
	/// 元channel当たりのcoded sample数
	std::uint64_t samples ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定識別子をbyte単位で比較します
 * @param[in] bytes 呼出し中だけ借用する小さいview
 * @param[in] expected NULL終端を不要とする静的ASCII識別子
 * @return 長さと全byteが一致した場合true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool matches( const std::span<const std::byte> bytes, const std::string_view expected)
{
	// 異なる長さを比較してview境界を越えない
	if( bytes.size() != expected.size()) {
		return false ;
	}
	// 固定識別子の全byteを符号なし値として照合する
	for( std::size_t index = 0; index < bytes.size(); ++index) { // 今回の識別子byte
		// 一つの不一致で対象外として終了する
		if( std::to_integer<unsigned char>( bytes[index]) != static_cast<unsigned char>( expected[index])) {
			return false ;
		}
	}
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 有界な同期readの範囲・充足・前後の取消しを確認します
 * @param[in] fileBytes streamの実byte数
 * @param[in] offset 元stream先頭基準の位置（byte）
 * @param[out] output 呼出し中だけ有効な最大10byteの借用span。失敗時内容は未確定
 * @param[in] readAt 空でない同期callback
 * @param[in] stop 協調的取消しtoken
 * @throws OperationError 不正範囲、短read/超過返却、取消し。元callback例外も伝播します
 */
//------------------------------------------------------------------------------
void readExact( const std::uint64_t fileBytes, const std::uint64_t offset, const std::span<std::byte> output,
			   const Mp3Read &readAt, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// 加算やcallback実行前に範囲と最大read量を確認する
	if( offset > fileBytes || output.empty() || output.size() > 10 || output.size() > fileBytes - offset) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto count = readAt( offset, output) ; // callbackが実際に満たしたbyte数
	checkCancelled( stop) ;
	// zero初期値や返却量超過を成功のheaderへ変換しない
	if( count != output.size()) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 必要な場合だけ冒頭ID3の外枠を読み飛ばします
 * @param[in] fileBytes 実streamのbyte数
 * @param[in] readAt 空でない同期reader
 * @param[in] stop read前後で確認する取消しtoken
 * @return 最初のaudio frameの候補byte位置。タグ無しなら0
 * @throws OperationError 短header、不正flags/size/footer、未対応版/圧縮、取消し
 * @note metadata payloadを解釈/読取りしません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t inspectId3Prefix( const std::uint64_t fileBytes, const Mp3Read &readAt,
											 const std::stop_token stop)
{
	std::array<std::byte, 10> buffer{} ; // 同期readごとに再使用する固定header buffer
	readExact( fileBytes, 0, std::span( buffer).first( 3), readAt, stop) ;
	// 識別子がない場合はframeを探して飛ばさず、先頭そのものを音声候補とする
	if( !matches( std::span( buffer).first( 3), "ID3")) {
		return 0 ;
	}
	readExact( fileBytes, 0, buffer, readAt, stop) ;
	const auto version = std::to_integer<unsigned>( buffer[3]) ; // ID3のmajor version
	const auto revision = std::to_integer<unsigned>( buffer[4]) ; // 初回は0のみ対応
	const auto flags = std::to_integer<unsigned>( buffer[5]) ; // version別の既知flags
	// 外枠の版を未知の構造として暗黙に読み替えない
	if( (version != 2 && version != 3 && version != 4) || revision != 0) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	const auto mask = version == 2 ? 0xC0U : version == 3 ? 0xE0U : 0xF0U ; // 既知bit
	// 予約flagsは破損として区別する
	if( (flags & ~mask) != 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	std::uint64_t size = 0 ; // synchsafe 28bitのpayload宣言byte数
	// sizeの4byteすべての高bitを確認して積算する
	for( std::size_t index = 6; index < 10; ++index) { // 今回のsize byte
		const auto value = std::to_integer<unsigned>( buffer[index]) ; // 符号なしheader値
		// 高bitを捨てて偽の小さい宣言長にしない
		if( (value & 0x80U) != 0) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		size = size * 128 + value ;
	}
	// v2.2圧縮タグは初回の外枠契約から除外する
	if( version == 2 && (flags & 0x40U) != 0) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	auto				offset = size + 10 ; // 28bit値との加算なのでuint64内
	const bool			hasFooter = version == 4 && (flags & 0x10U) != 0 ; // v2.4の追加10byte
	const std::uint64_t footerBytes = hasFooter ? 10 : 0 ; // 範囲照合用byte数
	// footer分を含む宣言が実stream外ならpayloadを読まず拒否する
	if( footerBytes > fileBytes || offset > fileBytes - footerBytes) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// footerありの場合だけheaderとの完全一致を確認する
	if( hasFooter) {
		const std::array tail{buffer[3], buffer[4], buffer[5], buffer[6],
							  buffer[7], buffer[8], buffer[9]} ; // headerの比較対象7byte
		readExact( fileBytes, offset, buffer, readAt, stop) ;
		// 識別子以外の7byteも一致しなければ別タグとして読み替えない
		if( !matches( std::span( buffer).first( 3), "3DI") || !std::equal( tail.begin(), tail.end(), buffer.begin() + 3)) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		offset += 10 ;
	}
	return offset ;
}

//------------------------------------------------------------------------------
/**
 * @brief 4byteのMPEG headerから許可frameの宣言属性を求めます
 * @param[in] bytes 呼出し中に有効な検証済み4byte span
 * @return 許可されたversion/rate/channel/byte/sample数
 * @throws OperationError 不正headerまたは未対応codec
 */
//------------------------------------------------------------------------------
[[nodiscard]] FrameInfo inspectFrameHeader( const std::span<const std::byte> bytes)
{
	std::uint32_t header = 0 ; // alignmentに依存しないbig-endian復号値
	// 検証済み4byteだけを順に積算し、pointer castを使わない
	for( const auto value : bytes) { // 現在のheader byte
		header = (header << 8) | std::to_integer<std::uint32_t>( value) ;
	}
	// 音声開始位置でsyncがなければ後続byteを探索して救済しない
	if( (header >> 21) != 0x7FFU) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto versionBits = (header >> 19) & 3U ; // MPEGのversion bit
	const auto layer = (header >> 17) & 3U ; // Layer IIIは1
	const auto bitrateIndex = (header >> 12) & 15U ; // 0/15は別扱い
	const auto rateIndex = (header >> 10) & 3U ; // 3は予約
	// 予約値を有効な未対応形式と混同しない
	if( versionBits == 1 || layer == 0 || bitrateIndex == 15 || rateIndex == 3 || (header & 3U) == 2) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	// MPEG-2.5、他layer、free-formatはこの初回契約に含めない
	if( versionBits == 0 || layer != 1 || bitrateIndex == 0) {
		throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
	}
	const std::uint32_t version = versionBits == 3 ? 1 : 2 ; // 製品metadataのversion表現
	const auto			rate = K_MPEG1_RATES[rateIndex] / version ; // 検証済み3rateと低周波拡張
	const std::uint32_t channels = ((header >> 6) & 3U) == 3 ? 1 : 2 ; // modeからのchannel数
	const std::uint64_t bitrate =
		(version == 1 ? K_MPEG1_BITRATES : K_MPEG2_BITRATES)[bitrateIndex] * 1000ULL ; // bit/秒
	const auto length =
		(version == 1 ? 144ULL : 72ULL) * bitrate / rate + ((header >> 9) & 1U) ; // paddingを含むframe byte数
	const std::uint64_t crc = (header & (1U << 16)) != 0 ? 0 : 2 ; // CRCが占めるbyte数のみ
	const std::uint64_t side =
		version == 1 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17) ; // side information byte数
	// 必須header/CRC/sideが収まらない宣言を許可しない
	if( length < 4 + crc + side) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	return {version, rate, channels, length, version == 1 ? 1152ULL : 576ULL} ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 符号化sample数を商/余り分解で16kHzへ数学的に切り上げ換算します
 * @param[in] codedSamples 元channel当たりの正の符号化sample数
 * @param[in] rate 対応する6種の元周波数（Hz）
 * @return 既存時刻表現内のnominal数。実MF出力数やdelay補正値ではありません
 * @throws OperationError 不正属性はDecodeFailed、時刻表現超過はInputTooLarge
 * @see koefude::mp3NominalOutputSamples
 */
//------------------------------------------------------------------------------
std::uint64_t mp3NominalOutputSamples( const std::uint64_t codedSamples, const std::uint32_t rate)
{
	// 正のsample数と対応rate以外を除外してdivisionの前提を確定する
	if( codedSamples == 0 ||
	   (rate != 16000 && rate != 22050 && rate != 24000 && rate != 32000 && rate != 44100 && rate != 48000)) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	const auto whole = codedSamples / rate ; // 大きい積を作る前の整数秒数
	const auto remainder = codedSamples % rate ; // rate未満の元sample端数
	// 大きい商を乗算する前に既存時刻表現の上限を照合する
	if( whole > K_MAXIMUM_TIMELINE_SAMPLES / K_SAMPLE_RATE) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	const auto base = whole * K_SAMPLE_RATE ; // 先に上限確認した16kHz sample数
	const auto scaled = remainder * K_SAMPLE_RATE ; // 48,000未満の小さい値だけを乗算
	const auto tail = scaled / rate + (scaled % rate != 0 ? 1ULL : 0ULL) ; // 端数の切上げ
	// 加算前に残り量を確認して換算/時刻の境界を維持する
	if( tail > K_MAXIMUM_TIMELINE_SAMPLES - base) {
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
	return base + tail ;
}

//------------------------------------------------------------------------------
/**
 * @brief MPEG-1/2 Layer IIIとID3外枠を全frameの宣言範囲で検証します
 * @param[in] fileBytes 同じ読取りstreamの実byte数。宣言時間から求めないこと
 * @param[in] readAt 空でない同期callback。参照先は本呼出し中に生存すること
 * @param[in] stop read前後・frame間・終了前で確認する協調的取消しtoken
 * @return 確定した所有metadata。失敗時は途中のmetadataを返しません
 * @throws OperationError 不正入力、破損、未対応形式、整数境界、取消し
 * @note 最大10byte/read・一定memory。録音時間の固定上限は設けません
 * callbackの元例外/codeをそのまま伝播し、原streamやpayloadを書き換えません
 * CRC/Huffman/bit reservoir/ID3内部を検証せず、音声内容の完全性を保証しません
 * @see koefude::inspectLayer3Mp3
 */
//------------------------------------------------------------------------------
Mp3InputInfo inspectLayer3Mp3( const std::uint64_t fileBytes, const Mp3Read &readAt, const std::stop_token stop)
{
	checkCancelled( stop) ;
	// callback未設定を音声形式の破損へ読み替えない
	if( !readAt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	// 最小frame headerがない入力を無音として成功させない
	if( fileBytes < 4) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	auto		 offset = inspectId3Prefix( fileBytes, readAt, stop) ; // 音声開始候補のbyte位置
	auto		 end = fileBytes ; // frame境界にID3v1があれば後から除外する終端
	Mp3InputInfo info ; // 成功時だけ外部へ返す所有metadata
	info.audioOffset = offset ;
	std::array<std::byte, 4> buffer{} ; // frame headerまたはTAGの固定read buffer
	// 実stream終端まで全frameを確認し、payloadは宣言長で読み飛ばす
	while( offset < end) {
		checkCancelled( stop) ;
		// frameの開始候補にある末尾128byteだけをID3v1候補にする
		if( end - offset == 128) {
			readExact( fileBytes, offset, std::span( buffer).first( 3), readAt, stop) ;
			// payload内のTAGを切り取らず、正確な境界で終端タグを除外する
			if( matches( std::span( buffer).first( 3), "TAG")) {
				end = offset ;
				break ;
			}
		}
		// 残りが最小header未満なら末尾junk/切断として拒否する
		if( end - offset < buffer.size()) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		readExact( fileBytes, offset, buffer, readAt, stop) ;
		const auto frame = inspectFrameHeader( buffer) ; // 今回の宣言属性を所有値へ復号
		// 初回に全frame共通属性を固定する
		if( info.frames == 0) {
			info.version = frame.version ;
			info.rate = frame.rate ;
			info.channels = frame.channels ;
		} else { // 確定済みの属性を後続frameへ照合する
			// 途中の属性変更は初回対応から除外する
			if( frame.version != info.version || frame.rate != info.rate || frame.channels != info.channels) {
				throw OperationError( ErrorKind::UNSUPPORTED_CODEC) ;
			}
		}
		// 最後のframeの欠落をreaderの部分成功へ渡さない
		if( frame.bytes > end - offset) {
			throw OperationError( ErrorKind::DECODE_FAILED) ;
		}
		// 元coded数/frame数の加算前にuint64の残りを確認する
		if( frame.samples > std::numeric_limits<std::uint64_t>::max() - info.codedSamples ||
		   info.frames == std::numeric_limits<std::uint64_t>::max()) {
			throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
		}
		info.codedSamples += frame.samples ;
		++info.frames ;
		info.nominalOutputSamples = mp3NominalOutputSamples( info.codedSamples, info.rate) ;
		offset += frame.bytes ;
	}
	// タグだけのstreamを有効な無音と呼ばない
	if( info.frames == 0) {
		throw OperationError( ErrorKind::DECODE_FAILED) ;
	}
	checkCancelled( stop) ;
	info.audioBytes = end - info.audioOffset ;
	return info ;
}

} // namespace koefude
