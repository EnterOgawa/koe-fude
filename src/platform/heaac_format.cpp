//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file heaac_format.cpp
 * @brief M4A内HE-AACの宣言とpacket byte範囲を逐次照合します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 HEのASCと全packet時間を有界に検査する独立検証器を追加
 * 0.1.0 Shunji Ogawa 2026/10/02 LCの全packet条件を変えない明示HE識別を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記。改行をLFへ統一
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
#include "platform/heaac_format.h"

#include "core/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace koefude {
namespace {
constexpr std::size_t				  K_MAXIMUM_BOXES = 4096 ; // metadata位置の件数上限
constexpr std::uint64_t				  K_MAXIMUM_TABLE_ENTRIES = 16'777'216 ; // 表とpacketの資源上限
constexpr auto						  K_ROOT = std::numeric_limits<std::size_t>::max() ; // 元fileを表す親番号

//------------------------------------------------------------------------------
/**
 * @brief 固定4文字のbox型をbig-endian識別子へ変換します
 * @param[in] text コンパイル時に生存する4文字のASCII literal
 * @return box型のuint32値
 * @pre textは4文字です
 */
//------------------------------------------------------------------------------
[[nodiscard]] consteval std::uint32_t tag( const std::string_view text)
{
	return (static_cast<std::uint32_t>( text[0]) << 24) | (static_cast<std::uint32_t>( text[1]) << 16) |
		   (static_cast<std::uint32_t>( text[2]) << 8) | static_cast<std::uint32_t>( text[3]) ;
}

//------------------------------------------------------------------------------
/**
 * @brief metadataの不成立を途中値なしで拒否します
 * @param[in] condition 必須の構造または属性条件
 * @param[in] kind 不成立時の既存error分類
 * @throws OperationError 条件不成立
 */
//------------------------------------------------------------------------------
void require( const bool condition, const ErrorKind kind = ErrorKind::DECODE_FAILED)
{
	// 構造/資源/初回範囲の指定分類を隠さず返す
	if( !condition) {
		throw OperationError( kind) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief uint64の表現範囲を確認して加算します
 * @param[in] a 非負の元値
 * @param[in] b 加える非負値
 * @return 表現可能な和
 * @throws OperationError 整数資源上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t add( const std::uint64_t a, const std::uint64_t b)
{
	require( b <= std::numeric_limits<std::uint64_t>::max() - a, ErrorKind::INPUT_TOO_LARGE) ;
	return a + b ;
}

//------------------------------------------------------------------------------
/**
 * @brief uint64の表現範囲を確認して乗算します
 * @param[in] a 非負の元値
 * @param[in] b 非負の倍率
 * @return 表現可能な積
 * @throws OperationError 整数資源上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t multiply( const std::uint64_t a, const std::uint64_t b)
{
	require( b == 0 || a <= std::numeric_limits<std::uint64_t>::max() / b, ErrorKind::INPUT_TOO_LARGE) ;
	return a * b ;
}

//------------------------------------------------------------------------------
/**
 * @brief 先に商/余りへ分けて宣言時間を整数換算します
 * @param[in] count 正の元frame数
 * @param[in] multiplier 出力単位の正の倍率
 * @param[in] rate 正の元tick/s
 * @return floorの換算値
 * @throws OperationError 整数資源上限
 * @pre count、multiplier、rateは呼出元で確認済みです
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t scale( const std::uint64_t count, const std::uint32_t multiplier, const std::uint32_t rate)
{
	return add( multiply( count / rate, multiplier), multiply( count % rate, multiplier) / rate) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 検証済み固定幅fieldのbig-endian値を復元します
 * @param[in] data 同期readで満たした1～8byteの借用field
 * @return fieldのunsigned値
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t bigEndian( const std::span<const std::byte> data)
{
	std::uint64_t value = 0 ; // 復元するunsigned整数
	// 固定field終端まで上位byteから読み取る
	for( const auto byte : data) { // 現在のfield byte
		value = (value << 8) | std::to_integer<std::uint64_t>( byte) ;
	}
	return value ;
}

/** @brief 最大64byteだけを同期で借用するMSB順bit readerです */
class AscBits {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief ASC長を先に検査します
	 * @param[in] data 呼出し中だけ生存するASC
	 */
	//------------------------------------------------------------------------------
	explicit AscBits( const std::span<const std::byte> data) : data_( data)
	{
		require( data.size() <= 64, ErrorKind::INPUT_TOO_LARGE) ;
		require( data.size() >= 2) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大24bitを範囲内から復元します
	 * @param[in] count 読み取るbit数
	 * @return 復元したunsigned値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t read( const std::size_t count)
	{
		require( count <= 24 && count <= data_.size() * 8 - position_) ;
		std::uint32_t value = 0 ; // 復元したbit値
		// 要求bit数だけ読み、借用ASCの終端を越えない
		for( std::size_t index = 0; index < count; ++index) { // 現在のbit番号
			const auto byte = std::to_integer<std::uint32_t>( data_[position_ / 8]) ; // 元byte
			value = value * 2 + ((byte >> (7 - position_ % 8)) & 1) ;
			++position_ ;
		}
		return value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief index/明示周波数を検査します
	 * @return 正の周波数（Hz）
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t rate()
	{
		constexpr std::array<std::uint32_t, 13> RATES{96000, 88200, 64000, 48000, 44100, 32000, 24000,
													  22050, 16000, 12000, 11025, 8000,	 7350} ; // 予約値を含まない表
		const auto								index = read( 4) ; // 周波数index
		// escapeの明示24bit値は0を拒否する
		if( index == 15) {
			const auto value = read( 24) ; // 明示Hz
			require( value > 0) ;
			return value ;
		}
		require( index < RATES.size()) ;
		return RATES[index] ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief escapeを含むAOTを読みます
	 * @return audio object type
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t aot()
	{
		const auto value = read( 5) ; // 最初のAOT値
		return value == 31 ? 32 + read( 6) : value ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 未解釈の非zero拡張を拒否します
	 */
	//------------------------------------------------------------------------------
	void finish()
	{
		// ASC終端までzero paddingだけを許可する
		while( position_ < data_.size() * 8) {
			require( read( 1) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
		}
	}

	private:
	/// 同期呼出し中だけ有効な借用
	std::span<const std::byte> data_ ;
	/// 検査済みbit原点からの位置
	std::size_t				   position_ = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 自作固定参照と同じASC範囲を解釈します
 * @param[in] data 呼出し中だけ借用する最大64byteのASC
 * @return core/復号後を分けた所有属性
 * @throws OperationError 構造不正、範囲外、資源限界
 */
//------------------------------------------------------------------------------
[[nodiscard]] HeAacConfig parseAsc( const std::span<const std::byte> data)
{
	AscBits		bits( data) ; // 有界bit reader
	HeAacConfig config ; // 成功時だけ返す所有属性
	config.aot = bits.aot() ;
	require( config.aot == 2 || config.aot == 5 || config.aot == 29, ErrorKind::UNSUPPORTED_CODEC) ;
	config.coreRate = bits.rate() ;
	config.coreChannels = bits.read( 4) ;
	require( config.coreChannels <= 7) ;
	require( config.coreChannels == 1 || config.coreChannels == 2, ErrorKind::UNSUPPORTED_CODEC) ;
	config.outputRate = config.coreRate ;
	config.coreAot = config.aot ;
	// explicit HEだけをSBR/PSとして解釈する
	if( config.aot == 5 || config.aot == 29) {
		config.outputRate = bits.rate() ;
		config.coreAot = bits.aot() ;
		require( config.coreAot == 2 && config.outputRate == config.coreRate * 2 &&
					(config.aot != 29 || config.coreChannels == 1),
				ErrorKind::UNSUPPORTED_CODEC) ;
	}
	const auto frameLength = bits.read( 1) ; // 960frame flag
	const auto depends = bits.read( 1) ; // core coder依存flag
	const auto extension = bits.read( 1) ; // GAS拡張flag
	require( frameLength == 0 && depends == 0 && extension == 0, ErrorKind::UNSUPPORTED_CODEC) ;
	bits.finish() ;
	config.outputChannels = config.aot == 29 ? 2U : config.coreChannels ;
	config.sbr = config.aot != 2 ;
	config.ps = config.aot == 29 ;
	config.initialHeCandidate = config.sbr && (config.outputRate == 44100 || config.outputRate == 48000) ;
	return config ;
}

/** @brief 親範囲を確認したdescriptor位置です。byteを所有しません */
struct Descriptor {
	/// descriptor tag
	std::uint32_t kind ;
	/// esds payload内先頭byte
	std::uint64_t payload ;
	/// esds payload内半開終端byte
	std::uint64_t end ;
} ;

/** @brief 内容を所有せず検査済みboxの元位置だけを保持します */
struct Box {
	/// big-endianの型識別子
	std::uint32_t kind ;
	/// 元stream内のpayload先頭byte
	std::uint64_t payload ;
	/// 元stream内の半開終端byte
	std::uint64_t end ;
	/// 有界配列の親番号、kRootはfile
	std::size_t	  parent ;
} ;

/** @brief packetを包含するtop-level mdatの位置です */
struct Range {
	/// audio payload先頭byte
	std::uint64_t start ;
	/// 半開終端byte
	std::uint64_t end ;
} ;

/** @brief 同じworkerだけで使用し、metadataの借用を外へ残さない検証器です */
class Parser {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ元streamの長さ/callbackとstopを借用します
	 * @param[in] fileBytes 同じ元streamの実byte数
	 * @param[in] readAt run終了まで生存する同期callback
	 * @param[in] stop read境界で確認する取消しtoken
	 */
	//------------------------------------------------------------------------------
	Parser( const std::uint64_t fileBytes, const HeAacRead &readAt, const std::stop_token stop)
		: fileBytes_( fileBytes), readAt_( readAt), stop_( stop)
	{
		boxes_.reserve( K_MAXIMUM_BOXES) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 元fileの外枠・属性と全packet範囲を確認します
	 * @return 全検査後の所有属性
	 * @throws OperationError 構造/属性/資源/取消し不成立
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HeAacInputInfo run() ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 全packet検査を適用すべき明示HE entryだけを識別します
	 * @return ASC先頭AOT 5/29があればtrue。属性適合は認定しません
	 * @throws OperationError 有界metadataの構造/資源/I/O/取消し不成立
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool			 explicitHe() ;

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 最大32byteの要求だけを同期readします
	 * @param[in] offset 元byte位置
	 * @param[in] count 満たすbyte数
	 * @return 先頭countだけが有効な固定buffer
	 * @throws OperationError 範囲/短read/過大返却/取消し
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::array<std::byte, 32> read( std::uint64_t offset, std::size_t count) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief box内の固定fieldを同期readします
	 * @param[in] index 検査済みbox番号
	 * @param[in] offset payload内のbyte位置
	 * @param[in] count fieldのbyte数
	 * @return 先頭countだけが有効な固定buffer
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::array<std::byte, 32> field( std::size_t index, std::uint64_t offset, std::size_t count) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief box内のunsigned fieldを復元します
	 * @param[in] index 検査済みbox番号
	 * @param[in] offset payload内のbyte位置
	 * @param[in] width 固定fieldの1～8byte幅
	 * @return unsigned field値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t number( std::size_t index, std::uint64_t offset, std::size_t width = 4) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 親残量でheaderを検証して位置だけを登録します
	 * @param[in] offset headerの元byte位置
	 * @param[in] end 親の半開終端byte
	 * @param[in] parent 親番号またはK_ROOT
	 * @param[in] depth 理解するboxの階層番号
	 * @return 追加したbox番号
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t	boxAt( std::uint64_t offset, std::uint64_t end, std::size_t parent, unsigned depth) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 既知containerだけを有限の深さで走査します
	 * @param[in] start 先頭byte位置
	 * @param[in] end 親の半開終端byte
	 * @param[in] parent 親番号またはK_ROOT
	 * @param[in] depth 現階層番号
	 */
	//------------------------------------------------------------------------------
	void						walk( std::uint64_t start, std::uint64_t end, std::size_t parent, unsigned depth) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ親の必須boxを一つだけ要求します
	 * @param[in] parent 親番号またはK_ROOT
	 * @param[in] kind 型識別子
	 * @return 唯一のbox番号
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t	one( std::size_t parent, std::uint32_t kind) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief tableのversion/flags0を要求します
	 * @param[in] index 対象box番号
	 */
	//------------------------------------------------------------------------------
	void						full( std::size_t index) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅tableの宣言数とpayload長を照合します
	 * @param[in] index 対象box番号
	 * @param[in] width 一行のbyte数
	 * @return 正のentry数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint32_t table( std::size_t index, unsigned width) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 既知boxの親と初回対象外の構造を確認します
	 */
	//------------------------------------------------------------------------------
	void						placement() const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief dref/stsd/esdsとsample entryの属性を照合します
	 * @param[in] minf 音声media情報の親番号
	 * @param[in] stbl 音声tableの親番号
	 */
	//------------------------------------------------------------------------------
	void						config( std::size_t minf, std::size_t stbl) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief esds内の一descriptorの長さを親範囲で確認します
	 * @param[in] index esdsのbox番号
	 * @param[in] offset payload内のdescriptor先頭byte
	 * @param[in] end 親descriptorのpayload内半開終端
	 * @return 型・payload先頭・半開終端の所有位置
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] Descriptor	descriptor( std::size_t index, std::uint64_t offset, std::uint64_t end) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief version0/1の時刻headerを確認します
	 * @param[in] index mvhd/tkhd/mdhdのbox番号
	 * @param[in] style movie/track/mediaの固定種別
	 * @return timescale/duration。trackのtimescaleは0
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::pair<std::uint32_t, std::uint64_t> timing( std::size_t index, std::string_view style) const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 時間・size・chunk・offset表を逐次照合します
	 * @param[in] stbl tableの親番号
	 * @param[in] frames mdhdの宣言総frame数
	 */
	//------------------------------------------------------------------------------
	void												  packets( std::size_t stbl, std::uint64_t frames) ;

	/// 同じstreamの実byte数
	std::uint64_t										  fileBytes_ ;
	/// run終了まで生存する呼出元のcallback
	const HeAacRead										 &readAt_ ;
	/// 同じworkerの協調的取消し
	std::stop_token										  stop_ ;
	/// 最大4,096件の位置だけを所有する
	std::vector<Box>									  boxes_ ;
	/// 全packet deltaがHEの2,048ならtrue
	bool												  fixedDelta_ = true ;
	/// 成功時にだけ返す属性
	HeAacInputInfo										  info_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 最大32byteの要求だけを同期readします
 * @param[in] offset 元byte位置
 * @param[in] count 満たすbyte数
 * @return 先頭countだけが有効な固定buffer
 * @throws OperationError 範囲/短read/過大返却/取消し
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::read
 */
//------------------------------------------------------------------------------
std::array<std::byte, 32> Parser::read( const std::uint64_t offset, const std::size_t count) const
{
	checkCancelled( stop_) ;
	require( offset <= fileBytes_ && count > 0 && count <= 32 && count <= fileBytes_ - offset) ;
	std::array<std::byte, 32> data{} ; // 一回の固定read領域
	const auto				  output = std::span( data).first( count) ; // callback中だけ有効な借用
	const auto				  actual = readAt_( offset, output) ; // 元例外/codeは変更せず伝播する
	checkCancelled( stop_) ;
	require( actual == count) ;
	return data ;
}

//------------------------------------------------------------------------------
/**
 * @brief box内の固定fieldを同期readします
 * @param[in] index 検査済みbox番号
 * @param[in] offset payload内のbyte位置
 * @param[in] count fieldのbyte数
 * @return 先頭countだけが有効な固定buffer
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::field
 */
//------------------------------------------------------------------------------
std::array<std::byte, 32> Parser::field( const std::size_t index, const std::uint64_t offset,
										const std::size_t count) const
{
	const auto &box = boxes_[index] ; // 検査済み親範囲を借用する
	require( offset <= box.end - box.payload && count <= box.end - box.payload - offset) ;
	return read( box.payload + offset, count) ;
}

//------------------------------------------------------------------------------
/**
 * @brief box内のunsigned fieldを復元します
 * @param[in] index 検査済みbox番号
 * @param[in] offset payload内のbyte位置
 * @param[in] width 固定fieldの1～8byte幅
 * @return unsigned field値
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::number
 */
//------------------------------------------------------------------------------
std::uint64_t Parser::number( const std::size_t index, const std::uint64_t offset, const std::size_t width) const
{
	const auto data = field( index, offset, width) ; // 指定幅を満たした固定field
	return bigEndian( std::span( data).first( width)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 親残量でheaderを検証して位置だけを登録します
 * @param[in] offset headerの元byte位置
 * @param[in] end 親の半開終端byte
 * @param[in] parent 親番号またはK_ROOT
 * @param[in] depth 理解するboxの階層番号
 * @return 追加したbox番号
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::boxAt
 */
//------------------------------------------------------------------------------
std::size_t Parser::boxAt( const std::uint64_t offset, const std::uint64_t end, const std::size_t parent,
						  const unsigned depth)
{
	require( depth <= 8 && boxes_.size() < K_MAXIMUM_BOXES, ErrorKind::INPUT_TOO_LARGE) ;
	require( offset <= end && end - offset >= 8) ;
	const auto	  data = read( offset, 8) ; // 一般box headerだけを読む
	auto		  size = bigEndian( std::span( data).first<4>()) ; // 32/64bit宣言box長
	const auto	  kind = static_cast<std::uint32_t>( bigEndian( std::span( data).subspan<4, 4>())) ; // 型
	std::uint64_t header = 8 ; // 一般headerのbyte数
	// extended sizeは親の残量16byteを確認してから読む
	if( size == 1) {
		require( end - offset >= 16) ;
		const auto extended = read( offset + 8, 8) ; // 64bitの宣言長
		size = bigEndian( std::span( extended).first<8>()) ;
		header = 16 ;
	}
	// EOFまでのsize0は最終top-level mdatだけを初回範囲にする
	if( size == 0) {
		require( parent == K_ROOT && kind == tag( "mdat"), ErrorKind::UNSUPPORTED_CODEC) ;
		size = end - offset ;
	}
	require( size >= header && size <= end - offset) ;
	const auto index = boxes_.size() ; // 登録する有界番号
	boxes_.push_back( {kind, offset + header, offset + size, parent}) ;
	return index ;
}

//------------------------------------------------------------------------------
/**
 * @brief 既知containerだけを有限の深さで走査します
 * @param[in] start 先頭byte位置
 * @param[in] end 親の半開終端byte
 * @param[in] parent 親番号またはK_ROOT
 * @param[in] depth 現階層番号
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::walk
 */
//------------------------------------------------------------------------------
void Parser::walk( std::uint64_t start, const std::uint64_t end, const std::size_t parent, const unsigned depth)
{
	// 同じ親の終端までだけ走査し、opaque payloadへ降りない
	while( start < end) {
		const auto index = boxAt( start, end, parent, depth) ; // 今回のbox番号
		const auto box = boxes_[index] ; // 子登録中にも独立して生存する位置値
		// 既知containerだけを有限の深さで再帰走査する
		if( box.kind == tag( "moov") || box.kind == tag( "trak") || box.kind == tag( "mdia") || box.kind == tag( "minf") ||
		   box.kind == tag( "stbl") || box.kind == tag( "dinf")) {
			walk( box.payload, box.end, index, depth + 1) ;
		}
		start = box.end ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じ親の必須boxを一つだけ要求します
 * @param[in] parent 親番号またはK_ROOT
 * @param[in] kind 型識別子
 * @return 唯一のbox番号
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::one
 */
//------------------------------------------------------------------------------
std::size_t Parser::one( const std::size_t parent, const std::uint32_t kind) const
{
	std::size_t found = K_ROOT ; // 未発見を表す番号
	// 最大4,096位置から同じ親の必須boxだけを検索する
	for( std::size_t index = 0; index < boxes_.size(); ++index) { // 現在の有界box番号
		// 同じ型でも別親のmetadataを流用しない
		if( boxes_[index].parent == parent && boxes_[index].kind == kind) {
			require( found == K_ROOT) ;
			found = index ;
		}
	}
	require( found != K_ROOT) ;
	return found ;
}

//------------------------------------------------------------------------------
/**
 * @brief tableのversion/flags0を要求します
 * @param[in] index 対象box番号
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::full
 */
//------------------------------------------------------------------------------
void Parser::full( const std::size_t index) const
{
	require( number( index, 0) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 固定幅tableの宣言数とpayload長を照合します
 * @param[in] index 対象box番号
 * @param[in] width 一行のbyte数
 * @return 正のentry数
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::table
 */
//------------------------------------------------------------------------------
std::uint32_t Parser::table( const std::size_t index, const unsigned width) const
{
	full( index) ;
	const auto count = static_cast<std::uint32_t>( number( index, 4)) ; // 宣言entry数
	require( count > 0 && boxes_[index].end - boxes_[index].payload == 8 + static_cast<std::uint64_t>( count) * width) ;
	return count ;
}

//------------------------------------------------------------------------------
/**
 * @brief 既知boxの親と初回対象外の構造を確認します
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::placement
 */
//------------------------------------------------------------------------------
void Parser::placement() const
{
	constexpr std::array PARENTS{
		// 既知型と唯一許可する親型。0はfile
		std::pair{tag( "ftyp"), 0U},			 std::pair{tag( "moov"), 0U},		  std::pair{tag( "mdat"), 0U},
		std::pair{tag( "mvhd"), tag( "moov")}, std::pair{tag( "trak"), tag( "moov")}, std::pair{tag( "tkhd"), tag( "trak")},
		std::pair{tag( "mdia"), tag( "trak")}, std::pair{tag( "mdhd"), tag( "mdia")}, std::pair{tag( "hdlr"), tag( "mdia")},
		std::pair{tag( "minf"), tag( "mdia")}, std::pair{tag( "smhd"), tag( "minf")}, std::pair{tag( "dinf"), tag( "minf")},
		std::pair{tag( "dref"), tag( "dinf")}, std::pair{tag( "stbl"), tag( "minf")}, std::pair{tag( "stsd"), tag( "stbl")},
		std::pair{tag( "stts"), tag( "stbl")}, std::pair{tag( "stsz"), tag( "stbl")}, std::pair{tag( "stsc"), tag( "stbl")},
		std::pair{tag( "stco"), tag( "stbl")}, std::pair{tag( "co64"), tag( "stbl")}} ;
	// 最大4,096位置の既知型について親を照合する
	for( const auto &box : boxes_) { // 現在の検査済み位置
		// 既知型と親型の固定組だけを順に確認する
		for( const auto &[child, parent] : PARENTS) { // 固定組の型識別子
			// 未知payloadを解釈せず、既知型の誤配置だけを拒否する
			if( box.kind == child) {
				require( (box.parent == K_ROOT ? 0U : boxes_[box.parent].kind) == parent) ;
			}
		}
	}
}

//------------------------------------------------------------------------------
/**
 * @brief esds内の一descriptorの長さを親範囲で確認します
 * @param[in] index esdsのbox番号
 * @param[in] offset payload内のdescriptor先頭byte
 * @param[in] end 親descriptorのpayload内半開終端
 * @return 型・payload先頭・半開終端の所有位置
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::descriptor
 */
//------------------------------------------------------------------------------
Descriptor Parser::descriptor( const std::size_t index, std::uint64_t offset, const std::uint64_t end) const
{
	require( offset < end) ;
	const auto	  kind = static_cast<std::uint32_t>( number( index, offset++, 1)) ; // tag
	std::uint64_t length = 0 ; // 最大28bitのpayload長
	// 最大4byteのcontinuation長を親終端内で読み取る
	for( unsigned count = 0; count < 4; ++count) { // 現在のlength byte番号
		require( offset < end) ;
		const auto value = number( index, offset++, 1) ; // 7bit長とcontinuation
		length = length * 128 + (value & 127) ;
		// 終端byteを見つけた時だけ確定位置を返す
		if( (value & 128) == 0) {
			require( length <= end - offset) ;
			return {kind, offset, offset + length} ;
		}
	}
	throw OperationError( ErrorKind::DECODE_FAILED) ;
}

//------------------------------------------------------------------------------
/**
 * @brief dref/stsd/esdsとsample entryの属性を照合します
 * @param[in] minf 音声media情報の親番号
 * @param[in] stbl 音声tableの親番号
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::config
 */
//------------------------------------------------------------------------------
void Parser::config( const std::size_t minf, const std::size_t stbl)
{
	const auto dinf = one( minf, tag( "dinf")) ; // data reference親
	const auto dref = one( dinf, tag( "dref")) ; // 唯一の参照表
	full( dref) ;
	require( number( dref, 4) == 1, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto url = boxAt( boxes_[dref].payload + 8, boxes_[dref].end, dref, 6) ; // 自己包含参照
	require( boxes_[url].end == boxes_[dref].end) ;
	require( boxes_[url].kind == tag( "url ") && boxes_[url].end - boxes_[url].payload == 4 && number( url, 0) == 1,
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto stsd = one( stbl, tag( "stsd")) ; // 唯一のsample description
	full( stsd) ;
	require( number( stsd, 4) == 1, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto entry = boxAt( boxes_[stsd].payload + 8, boxes_[stsd].end, stsd, 6) ; // 圧縮entry
	require( boxes_[entry].end == boxes_[stsd].end) ;
	require( boxes_[entry].kind == tag( "mp4a"), ErrorKind::UNSUPPORTED_CODEC) ;
	const auto header = field( entry, 0, 28) ; // version0固定sample entry
	require( bigEndian( std::span( header).first<6>()) == 0 && bigEndian( std::span( header).subspan<10, 6>()) == 0 &&
			bigEndian( std::span( header).subspan<20, 4>()) == 0) ;
	require( bigEndian( std::span( header).subspan<6, 2>()) == 1 && bigEndian( std::span( header).subspan<8, 2>()) == 0,
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto esds = boxAt( boxes_[entry].payload + 28, boxes_[entry].end, entry, 7) ; // 設定box
	require( boxes_[esds].kind == tag( "esds") && boxes_[esds].end == boxes_[entry].end, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto length = boxes_[esds].end - boxes_[esds].payload ; // 設定payload長
	require( length <= 256, ErrorKind::INPUT_TOO_LARGE) ;
	full( esds) ;
	const auto es = descriptor( esds, 4, length) ; // ES descriptor
	require( es.end == length) ;
	require( es.kind == 3, ErrorKind::UNSUPPORTED_CODEC) ;
	require( es.end - es.payload >= 3) ;
	const auto flags = number( esds, es.payload + 2, 1) ; // ES optional field/priority
	auto	   next = es.payload + 3 ; // decoder設定の開始候補
	// 依存ES IDは内容を使わず必須2byteの残量を確認する
	if( (flags & 0x80) != 0) {
		require( es.end - next >= 2) ;
		next += 2 ;
	}
	// URLは外部へ送らず、長さと親範囲だけを確認する
	if( (flags & 0x40) != 0) {
		require( next < es.end) ;
		const auto bytes = number( esds, next++, 1) ; // URLのbyte数
		require( bytes <= es.end - next) ;
		next += bytes ;
	}
	// OCR ES IDの必須2byteも未対応判定の前に検査する
	if( (flags & 0x20) != 0) {
		require( es.end - next >= 2) ;
		next += 2 ;
	}
	const auto decoder = descriptor( esds, next, es.end) ; // decoder設定
	require( decoder.kind == 4, ErrorKind::UNSUPPORTED_CODEC) ;
	require( decoder.end - decoder.payload >= 13) ;
	const auto asc = descriptor( esds, decoder.payload + 13, decoder.end) ; // ASC設定
	require( asc.end == decoder.end) ;
	require( asc.kind == 5, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto sl = descriptor( esds, decoder.end, es.end) ; // SL設定
	require( sl.end == es.end) ;
	require( sl.end > sl.payload) ;
	require( flags == 0 && sl.kind == 6 && sl.end - sl.payload == 1 && number( esds, sl.payload, 1) == 2,
			ErrorKind::UNSUPPORTED_CODEC) ;
	require( number( esds, decoder.payload, 1) == 0x40 && number( esds, decoder.payload + 1, 1) == 0x15,
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto ascBytes = asc.end - asc.payload ; // 有界ASC長
	require( ascBytes <= 64, ErrorKind::INPUT_TOO_LARGE) ;
	require( ascBytes >= 2) ;
	std::array<std::byte, 64> data{} ; // ASCだけの固定領域
	// 最大32byte/readでASCの必要byteだけをcopyする
	for( std::uint64_t offset = 0; offset < ascBytes; offset += 32) { // ASC内byte位置
		const auto count = static_cast<std::size_t>( std::min<std::uint64_t>( 32, ascBytes - offset)) ; // read幅
		const auto part = field( esds, asc.payload + offset, count) ; // 一回分の固定領域
		std::copy_n( part.begin(), count, data.begin() + static_cast<std::ptrdiff_t>( offset)) ;
	}
	info_.config = parseAsc( std::span( data).first( static_cast<std::size_t>( ascBytes))) ;
	require( bigEndian( std::span( header).subspan<16, 2>()) == info_.config.outputChannels &&
			bigEndian( std::span( header).subspan<18, 2>()) == 16 &&
			bigEndian( std::span( header).subspan<24, 4>()) ==
				(static_cast<std::uint64_t>( info_.config.outputRate) << 16)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief version0/1の時刻headerを確認します
 * @param[in] index mvhd/tkhd/mdhdのbox番号
 * @param[in] style movie/track/mediaの固定種別
 * @return timescale/duration。trackのtimescaleは0
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::timing
 */
//------------------------------------------------------------------------------
std::pair<std::uint32_t, std::uint64_t> Parser::timing( const std::size_t index, const std::string_view style) const
{
	const auto flags = static_cast<std::uint32_t>( number( index, 0)) ; // FullBoxのversion/flags
	const auto version = flags >> 24 ; // 64bit時刻への切替
	const auto bits = flags & 0xFFFFFF ; // 初回対象のflags
	require( version <= 1, ErrorKind::UNSUPPORTED_CODEC) ;
	require( style == "track" ? (bits == 1 || bits == 3 || bits == 7) : bits == 0, ErrorKind::UNSUPPORTED_CODEC) ;
	// boxの種別とversionに対応する固定headerの期待byte長
	const auto length = (style == "track" ? 84U : style == "movie" ? 100U : 24U) + version * 12 ;
	// 固定headerの長さを確認し、別boxへfieldを越境させない
	require( boxes_[index].end - boxes_[index].payload == length) ;
	// trackはmovie timescaleを別に受け取るため、ここではdurationだけを返す
	if( style == "track") {
		require( number( index, 12 + version * 8) != 0) ;
		return {0, number( index, 20 + version * 8, 4 + version * 4)} ;
	}
	return {static_cast<std::uint32_t>( number( index, 12 + version * 8)),
			number( index, 16 + version * 8, 4 + version * 4)} ;
}

//------------------------------------------------------------------------------
/**
 * @brief 時間・size・chunk・offset表を逐次照合します
 * @param[in] stbl tableの親番号
 * @param[in] frames mdhdの宣言総frame数
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::packets
 */
//------------------------------------------------------------------------------
void Parser::packets( const std::size_t stbl, const std::uint64_t frames)
{
	const auto	stts = one( stbl, tag( "stts")) ; // 時間runの位置
	const auto	stsc = one( stbl, tag( "stsc")) ; // chunk所属runの位置
	const auto	stsz = one( stbl, tag( "stsz")) ; // packet byte数の位置
	std::size_t offsets = K_ROOT ; // 唯一のoffset表の位置
	// stco/co64を同じ親から一つだけ要求する
	for( std::size_t index = 0; index < boxes_.size(); ++index) { // 現在の有界番号
		// 両offset型の同時存在や重複を成功にしない
		if( boxes_[index].parent == stbl && (boxes_[index].kind == tag( "stco") || boxes_[index].kind == tag( "co64"))) {
			require( offsets == K_ROOT) ;
			offsets = index ;
		}
	}
	require( offsets != K_ROOT) ;
	const unsigned width = boxes_[offsets].kind == tag( "co64") ? 8U : 4U ; // offsetのbyte幅
	const auto	   timeEntries = table( stts, 8) ; // 時間run数
	const auto	   mapEntries = table( stsc, 12) ; // chunk run数
	info_.chunkCount = table( offsets, width) ;
	full( stsz) ;
	const auto uniform = number( stsz, 4) ; // 同一packet size、0はvariable表
	info_.packetCount = number( stsz, 8) ;
	require( info_.packetCount > 0 &&
			boxes_[stsz].end - boxes_[stsz].payload == 12 + (uniform == 0 ? info_.packetCount * 4 : 0)) ;
	require( static_cast<std::uint64_t>( timeEntries) + mapEntries + info_.chunkCount + info_.packetCount <=
				K_MAXIMUM_TABLE_ENTRIES,
			ErrorKind::INPUT_TOO_LARGE) ;
	std::uint64_t timePackets = 0 ; // sttsのpacket総数
	// 有界な時間runを逐次集計し、PCM frame数もcheckedにする
	for( std::uint64_t index = 0; index < timeEntries; ++index) { // 現在の時間run
		const auto data = field( stts, 8 + index * 8, 8) ; // 一行だけの借用値
		const auto count = bigEndian( std::span( data).first<4>()) ; // runのpacket数
		const auto delta = bigEndian( std::span( data).subspan<4, 4>()) ; // packet frame数
		require( count > 0 && delta > 0) ;
		fixedDelta_ = fixedDelta_ && delta == 2048 ;
		timePackets = add( timePackets, count) ;
		info_.totalSamples = add( info_.totalSamples, multiply( count, delta)) ;
	}
	require( timePackets == info_.packetCount && info_.totalSamples == frames) ;
	std::uint64_t previousChunk = 0 ; // stscの前runの先頭chunk番号
	// run順序を先に検証し、未使用の末尾runも見逃さない
	for( std::uint64_t index = 0; index < mapEntries; ++index) { // 現在のchunk run
		const auto first = number( stsc, 8 + index * 12) ; // runの先頭chunk
		const auto count = number( stsc, 12 + index * 12) ; // 一chunkのpacket数
		const auto description = number( stsc, 16 + index * 12) ; // description番号
		require( first > previousChunk && first <= info_.chunkCount && (index != 0 || first == 1) && count > 0 &&
				description == 1) ;
		previousChunk = first ;
	}
	std::vector<Range> mdats ; // 最大4,096位置だけを保持する音声領域
	mdats.reserve( boxes_.size()) ;
	// top-level mdatを元byte順で登録し、全packet vectorを作らない
	for( const auto &box : boxes_) { // 現在の有界box位置
		// 音声領域だけを二分探索用へ保持する
		if( box.kind == tag( "mdat") && box.parent == K_ROOT) {
			mdats.push_back( {box.payload, box.end}) ;
		}
	}
	std::uint64_t assigned = 0 ; // chunkへ割当て済みのpacket数
	std::uint64_t previousStart = 0 ; // 前chunkの元先頭byte位置
	std::uint64_t previousEnd = 0 ; // 前chunkの半開終端byte位置
	std::uint64_t run = 0 ; // 現在のstsc run番号
	auto		  perChunk = number( stsc, 12) ; // 現runの一chunk当たりpacket数
	auto		  nextFirst = mapEntries > 1 ? number( stsc, 20) : 0 ; // 次runの先頭、0はなし
	// 宣言chunkを一回ずつ照合し、全packetの所属とbyte範囲を確認する
	for( std::uint64_t chunk = 1; chunk <= info_.chunkCount; ++chunk) { // 現在のchunk番号
		// 次runの先頭でだけ固定一行分を更新する
		if( nextFirst != 0 && chunk == nextFirst) {
			++run ;
			perChunk = number( stsc, 12 + run * 12) ;
			nextFirst = run + 1 < mapEntries ? number( stsc, 8 + (run + 1) * 12) : 0 ;
		}
		require( perChunk <= info_.packetCount - assigned) ;
		std::uint64_t size = 0 ; // 今回chunkの全packet byte数
		// 同一sizeの全packetはchecked乗算で同等の範囲を検証する
		if( uniform != 0) {
			size = multiply( perChunk, uniform) ;
		} else { // variable表だけを一packetずつ読む
			// 有界な今回chunkのpacket数まで逐次sizeを集計する
			for( std::uint64_t index = 0; index < perChunk; ++index) { // chunk内packet番号
				const auto value = number( stsz, 12 + (assigned + index) * 4) ; // packet byte数
				require( value > 0) ;
				size = add( size, value) ;
			}
		}
		const auto start = number( offsets, 8 + (chunk - 1) * width, width) ; // 元先頭byte位置
		const auto end = add( start, size) ; // checkedの半開終端
		const auto region = std::upper_bound( mdats.begin(), mdats.end(), start,
			//------------------------------------------------------------------------------
			/**
			 * @brief offsetより後で始まるmdatを二分探索します
			 * @param[in] value 元packet先頭byte
			 * @param[in] range 借用音声領域
			 * @return 音声領域が後方で始まるならtrue
			 */
			//------------------------------------------------------------------------------
			[]( const std::uint64_t value, const Range &range)
			{
				return value < range.start ;
			}) ;
		require( region != mdats.begin()) ;
		require( end <= (region - 1)->end) ;
		// 初回範囲の昇順を保ち、同じ/前領域の再参照を成功にしない
		if( chunk > 1 && start < previousEnd) {
			throw OperationError( start < previousStart ? ErrorKind::UNSUPPORTED_CODEC : ErrorKind::DECODE_FAILED) ;
		}
		// 初出の位置だけを結果へ保持する
		if( chunk == 1) {
			info_.firstPacketOffset = start ;
		}
		assigned += perChunk ;
		info_.audioBytes = add( info_.audioBytes, size) ;
		previousStart = start ;
		previousEnd = end ;
	}
	require( assigned == info_.packetCount) ;
	info_.lastPacketEnd = previousEnd ;
}

//------------------------------------------------------------------------------
/**
 * @brief 全packet検査を適用すべき明示HE entryだけを識別します
 * @return ASC先頭AOT 5/29があればtrue。属性適合は認定しません
 * @throws OperationError 有界metadataの構造/資源/I/O/取消し不成立
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::explicitHe
 */
//------------------------------------------------------------------------------
bool Parser::explicitHe()
{
	walk( 0, fileBytes_, K_ROOT, 0) ;
	const auto containers = boxes_.size() ; // 子entry登録前の固定走査件数
	bool	   found = false ; // 全entryの構造確認を済ませてから返す識別結果
	// 全stsdを確認し、LCとHEの混在を先頭entryだけで見逃さない
	for( std::size_t index = 0; index < containers; ++index) { // 現metadata番号
		// 他型のopaque payloadをsample descriptionとして解釈しない
		if( boxes_[index].kind != tag( "stsd")) {
			continue ;
		}
		full( index) ;
		const auto entries = number( index, 4) ; // sample descriptionの件数
		require( entries > 0 && entries <= K_MAXIMUM_BOXES, ErrorKind::INPUT_TOO_LARGE) ;
		auto position = boxes_[index].payload + 8 ; // 最初のentry byte位置
		// 有界entry件数まで順に調べ、全entryの親範囲を確認する
		for( std::uint64_t entryNumber = 0; entryNumber < entries; ++entryNumber) {
			const auto entry = boxAt( position, boxes_[index].end, index, 6) ; // 今回entry
			position = boxes_[entry].end ;
			// mp4a以外は別codecの入口へ委ねる
			if( boxes_[entry].kind != tag( "mp4a")) {
				continue ;
			}
			const auto version = number( entry, 8, 2) ; // audio sample entry layout番号
			require( version <= 2, ErrorKind::UNSUPPORTED_CODEC) ;
			const std::uint64_t header = version == 0 ? 28 : version == 1 ? 44 : 64 ; // 固定幅
			require( boxes_[entry].end - boxes_[entry].payload >= header) ;
			auto childPosition = boxes_[entry].payload + header ; // codec childの開始
			// 既知entry内の子box終端まで進み、設定位置だけを確認する
			while( childPosition < boxes_[entry].end) {
				const auto child = boxAt( childPosition, boxes_[entry].end, entry, 7) ; // 子
				childPosition = boxes_[child].end ;
				// esds以外のmetadata内部やaudioを読まない
				if( boxes_[child].kind != tag( "esds")) {
					continue ;
				}
				full( child) ;
				const auto end = boxes_[child].end - boxes_[child].payload ; // 親残量
				const auto es = descriptor( child, 4, end) ; // 最初のES descriptor
				require( es.kind == 3 && es.end - es.payload >= 3) ;
				const auto flags = number( child, es.payload + 2, 1) ; // optional field flags
				auto	   next = es.payload + 3 ; // decoder設定の先頭候補
				// 依存fieldの残量を確認して読み飛ばす
				if( (flags & 0x80) != 0) {
					require( es.end - next >= 2) ;
					next += 2 ;
				}
				// URLの位置だけを検査し、外部へ開かない
				if( (flags & 0x40) != 0) {
					require( next < es.end) ;
					const auto length = number( child, next++, 1) ; // URLのbyte長
					require( length <= es.end - next) ;
					next += length ;
				}
				// OCR fieldの残量もcheckedにする
				if( (flags & 0x20) != 0) {
					require( es.end - next >= 2) ;
					next += 2 ;
				}
				const auto decoder = descriptor( child, next, es.end) ; // decoder情報
				require( decoder.kind == 4 && decoder.end - decoder.payload >= 13) ;
				const auto asc = descriptor( child, decoder.payload + 13, decoder.end) ; // ASC
				require( asc.kind == 5 && asc.payload < asc.end) ;
				const auto aot = number( child, asc.payload, 1) >> 3 ; // 明示AOTの先頭5bit
				found = found || aot == 5 || aot == 29 ;
			}
		}
		require( position == boxes_[index].end) ;
	}
	checkCancelled( stop_) ;
	return found ;
}

//------------------------------------------------------------------------------
/**
 * @brief 元fileの外枠・属性と全packet範囲を確認します
 * @return 全検査後の所有属性
 * @throws OperationError 構造/属性/資源/取消し不成立
 * @see koefude::anonymous_namespace{heaac_format.cpp}::Parser::run
 */
//------------------------------------------------------------------------------
HeAacInputInfo Parser::run()
{
	walk( 0, fileBytes_, K_ROOT, 0) ;
	placement() ;
	const auto ftyp = one( K_ROOT, tag( "ftyp")) ; // 唯一のfile type
	require( boxes_[ftyp].end - boxes_[ftyp].payload >= 8 && (boxes_[ftyp].end - boxes_[ftyp].payload) % 4 == 0) ;
	const auto brand = static_cast<std::uint32_t>( number( ftyp, 0)) ; // 初回のmajor brand
	require( brand == tag( "M4A ") || brand == tag( "isom") || brand == tag( "iso2") || brand == tag( "mp41") ||
				brand == tag( "mp42"),
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto	moov = one( K_ROOT, tag( "moov")) ; // 唯一のmovie
	std::size_t track = K_ROOT ; // 初回対象の単一track番号
	std::size_t tracks = 0 ; // このmovieのtrack件数
	// 全位置からtrack件数と対象外の構造を確認する
	for( std::size_t index = 0; index < boxes_.size(); ++index) { // 有界box番号
		const auto &box = boxes_[index] ; // 今回の位置を借用する
		// このmovieのtrackだけを数える
		if( box.parent == moov && box.kind == tag( "trak")) {
			++tracks ;
			track = index ;
		}
	}
	require( tracks > 0) ;
	require( tracks == 1, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto mdia = one( track, tag( "mdia")) ; // 唯一のmedia
	const auto minf = one( mdia, tag( "minf")) ; // 唯一のmedia情報
	const auto stbl = one( minf, tag( "stbl")) ; // 唯一のsample表
	const auto hdlr = one( mdia, tag( "hdlr")) ; // media handler
	full( hdlr) ;
	static_cast<void>( field( hdlr, 0, 24)) ; // 固定handler headerの範囲を確認する
	require( number( hdlr, 8) == tag( "soun"), ErrorKind::UNSUPPORTED_CODEC) ;
	const auto smhd = one( minf, tag( "smhd")) ; // 音声header
	full( smhd) ;
	require( boxes_[smhd].end - boxes_[smhd].payload == 8) ;
	require( number( smhd, 4) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
	const auto [movie_rate, movie_duration] = timing( one( moov, tag( "mvhd")), "movie") ; // movie時間
	const auto trackDuration = timing( one( track, tag( "tkhd")), "track").second ; // track時間
	const auto [rate, frames] = timing( one( mdia, tag( "mdhd")), "media") ; // 元frame数
	require( rate > 0 && movie_rate > 0) ;
	info_.timescale = rate ;
	packets( stbl, frames) ; // edit拒否より先に全packet範囲を検査する
	config( minf, stbl) ;
	require( rate == info_.config.outputRate) ;
	// 対象外の編集/fragment/補助時刻表を推定で処理しない
	for( const auto &box : boxes_) { // 現在の有界位置
		require( box.kind != tag( "moof") && box.kind != tag( "mvex") && box.kind != tag( "edts") &&
					box.kind != tag( "elst") && box.kind != tag( "ctts") && box.kind != tag( "stz2") &&
					box.kind != tag( "stss"),
				ErrorKind::UNSUPPORTED_CODEC) ;
	}
	info_.outputFloor = scale( frames, K_SAMPLE_RATE, rate) ;
	info_.outputCeil = add( info_.outputFloor, multiply( frames % rate, K_SAMPLE_RATE) % rate != 0 ? 1U : 0U) ;
	info_.duration100ns = scale( frames, 10'000'000, rate) ;
	require( info_.outputCeil <= K_MAXIMUM_TIMELINE_SAMPLES &&
				info_.duration100ns <= static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max() - 1'000'000),
			ErrorKind::INPUT_TOO_LARGE) ;
	const auto movieFloor = scale( frames, movie_rate, rate) ; // movie tick下限
	const auto movieCeil =
		add( movieFloor, multiply( frames % rate, movie_rate) % rate != 0 ? 1U : 0U) ; // movie tick上限
	require( movie_duration == trackDuration && movieFloor <= movie_duration && movie_duration <= movieCeil) ;
	require( info_.config.initialHeCandidate && fixedDelta_, ErrorKind::UNSUPPORTED_CODEC) ;
	checkCancelled( stop_) ;
	info_.metadataBoxes = static_cast<std::uint32_t>( boxes_.size()) ;
	return info_ ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief sample entryのASCが明示HEか識別し、全検査を必要とする分岐を返します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 同期呼出し中だけ生存する最大32byte/readのcallback
 * @param[in] stop read境界で確認する取消しtoken
 * @return mp4aのesdsに明示AOT 5/29が一つでもあればtrue
 * @throws OperationError 構造/資源/I/O/取消し。元codeを維持します
 * @note ASC全体やHE対応を認定せず、trueはinspectM4aHeaacでの全検査を必須にします
 * @see koefude::hasExplicitM4aHeaac
 */
//------------------------------------------------------------------------------
bool hasExplicitM4aHeaac( const std::uint64_t fileBytes, const HeAacRead &readAt, const std::stop_token stop)
{
	// 空callbackを任意例外へ変換せず共通の入力拒否とする
	if( !readAt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	require( fileBytes >= 8) ;
	try {
		return Parser( fileBytes, readAt, stop).explicitHe() ;
	} catch( const std::bad_alloc &) { // 有界位置vectorの実資源不足を保存する
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 同じstreamのHE-AAC宣言・時間・全packet範囲を検証します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 非空の同期callback。元例外/codeを伝播します
 * @param[in] stop read前後と完了前で確認する取消しtoken
 * @return 全照合後の所有属性。失敗時は途中DTOを返しません
 * @throws OperationError 構造不正、対象外、資源不足、取消し、空callback
 * @note 最大32byte/read・4,096box・階層8・表/packet総16,777,216
 * 音声/opaque/全packet表を確保しません。bad_allocはInputTooLarge
 * editなしのexplicit HEだけが対象。LCの製品条件は変更しません
 * 圧縮frame内部/CRC・実PCM/時刻・full native GUID判定は保証外です
 * @see koefude::inspectM4aHeaac
 */
//------------------------------------------------------------------------------
HeAacInputInfo inspectM4aHeaac( const std::uint64_t fileBytes, const HeAacRead &readAt, const std::stop_token stop)
{
	// 空callbackをbad_function_callへ転化させない
	if( !readAt) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	require( fileBytes >= 8) ;
	try {
		return Parser( fileBytes, readAt, stop).run() ;
	} catch( const std::bad_alloc &) { // 有界metadata領域の実確保不足を区別する
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 最大64byteのASCを構造と初回属性へ照合します
 * @param[in] asc 呼出し中だけ生存するASC借用byte。保存しません
 * @return 所有属性。LCは初回HE候補falseとして返します
 * @throws OperationError 構造不正、対象外、資源上限
 * @see koefude::inspectHeaacAsc
 */
//------------------------------------------------------------------------------
HeAacConfig inspectHeaacAsc( const std::span<const std::byte> asc)
{
	return parseAsc( asc) ;
}

//------------------------------------------------------------------------------
/**
 * @brief MFのcore属性とUserDataを検証済みASCへ照合します
 * @param[in] expected 同じstreamの検証済み初回HE属性
 * @param[in] coreRate MF native属性のSBR前Hz
 * @param[in] coreChannels MF native属性のPS前channel数
 * @param[in] userData 呼出し中だけ借用する最大76byteのMF UserData
 * @throws OperationError 不一致、構造不正、初回範囲外、資源上限
 * @note full AAC GUIDと実出力属性の判定は製品接続側の責務です
 * @see koefude::verifyHeaacNative
 */
//------------------------------------------------------------------------------
void verifyHeaacNative( const HeAacConfig &expected, const std::uint32_t coreRate, const std::uint32_t coreChannels,
					   const std::span<const std::byte> userData)
{
	require( expected.initialHeCandidate, ErrorKind::UNSUPPORTED_CODEC) ;
	require( userData.size() <= 76, ErrorKind::INPUT_TOO_LARGE) ;
	require( userData.size() >= 14) ;
	require( bigEndian( userData.first<2>()) == 0 && bigEndian( userData.subspan<4, 8>()) == 0,
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto actual = parseAsc( userData.subspan( 12)) ; // MFが保持した実ASC
	require( coreRate == expected.coreRate && coreChannels == expected.coreChannels && actual.aot == expected.aot &&
			actual.coreAot == expected.coreAot && actual.coreRate == expected.coreRate &&
			actual.coreChannels == expected.coreChannels && actual.outputRate == expected.outputRate &&
			actual.outputChannels == expected.outputChannels && actual.sbr == expected.sbr &&
			actual.ps == expected.ps && actual.initialHeCandidate == expected.initialHeCandidate) ;
}

} // namespace koefude
