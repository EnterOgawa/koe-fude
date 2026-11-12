//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file alac_format.cpp
 * @brief M4A内ALACの宣言とpacket byte範囲を逐次照合します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 固定参照器と同じ初回範囲のpure検証器を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include "platform/alac_format.h"

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
	Parser( const std::uint64_t fileBytes, const AlacRead &readAt, const std::stop_token stop)
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
	[[nodiscard]] AlacInputInfo run() ;

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
	 * @brief dref/stsd/cookieとsample entryの属性を照合します
	 * @param[in] minf 音声media情報の親番号
	 * @param[in] stbl 音声tableの親番号
	 */
	//------------------------------------------------------------------------------
	void						config( std::size_t minf, std::size_t stbl) ;
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
	 * @param[in] movieRate movieのtick/s
	 * @param[in] movieDuration mvhdの宣言tick数
	 * @param[in] trackDuration tkhdの宣言tick数
	 */
	//------------------------------------------------------------------------------
	void		  packets( std::size_t stbl, std::uint64_t frames, std::uint32_t movieRate, std::uint64_t movieDuration,
						  std::uint64_t trackDuration) ;

	/// 同じstreamの実byte数
	std::uint64_t fileBytes_ ;
	/// run終了まで生存する呼出元のcallback
	const AlacRead	&readAt_ ;
	/// 同じworkerの協調的取消し
	std::stop_token	 stop_ ;
	/// 最大4,096件の位置だけを所有する
	std::vector<Box> boxes_ ;
	/// 成功時にだけ返す属性
	AlacInputInfo	 info_ ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 最大32byteの要求だけを同期readします
 * @param[in] offset 元byte位置
 * @param[in] count 満たすbyte数
 * @return 先頭countだけが有効な固定buffer
 * @throws OperationError 範囲/短read/過大返却/取消し
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::read
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::field
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::number
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::boxAt
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::walk
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::one
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::full
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::table
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
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::placement
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
 * @brief dref/stsd/cookieとsample entryの属性を照合します
 * @param[in] minf 音声media情報の親番号
 * @param[in] stbl 音声tableの親番号
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::config
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
	require( boxes_[entry].kind == tag( "alac"), ErrorKind::UNSUPPORTED_CODEC) ;
	const auto header = field( entry, 0, 28) ; // version0の固定属性
	require( bigEndian( std::span( header).first<6>()) == 0 && bigEndian( std::span( header).subspan<10, 6>()) == 0 &&
			bigEndian( std::span( header).subspan<20, 4>()) == 0) ;
	require( bigEndian( std::span( header).subspan<6, 2>()) == 1 && bigEndian( std::span( header).subspan<8, 2>()) == 0,
			ErrorKind::UNSUPPORTED_CODEC) ;
	const auto cookie = boxAt( boxes_[entry].payload + 28, boxes_[entry].end, entry, 7) ; // config
	require( boxes_[cookie].end == boxes_[entry].end && boxes_[cookie].kind == tag( "alac") &&
			boxes_[cookie].end - boxes_[cookie].payload == 28) ;
	full( cookie) ;
	const auto data = field( cookie, 4, 24) ; // 必須ALACSpecificConfigのみ
	info_.frameLength = static_cast<std::uint32_t>( bigEndian( std::span( data).first<4>())) ;
	info_.compatibleVersion = std::to_integer<std::uint32_t>( data[4]) ;
	info_.bits = std::to_integer<std::uint32_t>( data[5]) ;
	info_.pb = std::to_integer<std::uint32_t>( data[6]) ;
	info_.mb = std::to_integer<std::uint32_t>( data[7]) ;
	info_.kb = std::to_integer<std::uint32_t>( data[8]) ;
	info_.channels = std::to_integer<std::uint32_t>( data[9]) ;
	info_.maxRun = static_cast<std::uint32_t>( bigEndian( std::span( data).subspan<10, 2>())) ;
	info_.maxFrameBytes = static_cast<std::uint32_t>( bigEndian( std::span( data).subspan<12, 4>())) ;
	info_.averageBitRate = static_cast<std::uint32_t>( bigEndian( std::span( data).subspan<16, 4>())) ;
	info_.rate = static_cast<std::uint32_t>( bigEndian( std::span( data).subspan<20, 4>())) ;
	require( info_.frameLength > 0 && info_.bits > 0 && info_.channels > 0 && info_.rate > 0) ;
	require( info_.compatibleVersion == 0 && info_.frameLength <= 4096 && (info_.bits == 16 || info_.bits == 24) &&
				info_.channels <= 2 && info_.rate >= 8000 && info_.rate <= 48000,
			ErrorKind::UNSUPPORTED_CODEC) ;
	require( bigEndian( std::span( header).subspan<16, 2>()) == info_.channels &&
			bigEndian( std::span( header).subspan<18, 2>()) == info_.bits &&
			bigEndian( std::span( header).subspan<24, 4>()) == (static_cast<std::uint64_t>( info_.rate) << 16)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief version0/1の時刻headerを確認します
 * @param[in] index mvhd/tkhd/mdhdのbox番号
 * @param[in] style movie/track/mediaの固定種別
 * @return timescale/duration。trackのtimescaleは0
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::timing
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
 * @param[in] movieRate movieのtick/s
 * @param[in] movieDuration mvhdの宣言tick数
 * @param[in] trackDuration tkhdの宣言tick数
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::packets
 */
//------------------------------------------------------------------------------
void Parser::packets( const std::size_t stbl, const std::uint64_t frames, const std::uint32_t movieRate,
					 const std::uint64_t movieDuration, const std::uint64_t trackDuration)
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
		require( count > 0 && delta > 0 && delta <= info_.frameLength) ;
		timePackets = add( timePackets, count) ;
		info_.totalSamples = add( info_.totalSamples, multiply( count, delta)) ;
	}
	require( timePackets == info_.packetCount && info_.totalSamples == frames) ;
	info_.idealOutputSamples = scale( frames, K_SAMPLE_RATE, info_.rate) ;
	info_.duration100ns = scale( frames, 10'000'000, info_.rate) ;
	require( info_.idealOutputSamples <= K_MAXIMUM_TIMELINE_SAMPLES &&
				info_.duration100ns <= static_cast<std::uint64_t>( std::numeric_limits<std::int64_t>::max() - 1'000'000),
			ErrorKind::INPUT_TOO_LARGE) ;
	const auto movieFloor = scale( frames, movieRate, info_.rate) ; // movie tickの下限
	const auto movieCeil =
		add( movieFloor, multiply( frames % info_.rate, movieRate) % info_.rate != 0 ? 1U : 0U) ; // 上限
	require( movieDuration >= movieFloor && movieDuration <= movieCeil && trackDuration >= movieFloor &&
			trackDuration <= movieCeil) ;
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
			require( info_.maxFrameBytes == 0 || uniform <= info_.maxFrameBytes) ;
			size = multiply( perChunk, uniform) ;
		} else { // variable表だけを一packetずつ読む
			// 有界な今回chunkのpacket数まで逐次sizeを集計する
			for( std::uint64_t index = 0; index < perChunk; ++index) { // chunk内packet番号
				const auto value = number( stsz, 12 + (assigned + index) * 4) ; // packet byte数
				require( value > 0 && (info_.maxFrameBytes == 0 || value <= info_.maxFrameBytes)) ;
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
 * @brief 元fileの外枠・属性と全packet範囲を確認します
 * @return 全検査後の所有属性
 * @throws OperationError 構造/属性/資源/取消し不成立
 * @see koefude::anonymous_namespace{alac_format.cpp}::Parser::run
 */
//------------------------------------------------------------------------------
AlacInputInfo Parser::run()
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
	// 対象外の編集/fragment/補助時刻表を推定で処理しない
	for( const auto &box : boxes_) { // 現在の有界位置
		require( box.kind != tag( "moof") && box.kind != tag( "mvex") && box.kind != tag( "edts") &&
					box.kind != tag( "ctts") && box.kind != tag( "stz2") && box.kind != tag( "stss"),
				ErrorKind::UNSUPPORTED_CODEC) ;
	}
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
	config( minf, stbl) ;
	const auto [movie_rate, movie_duration] = timing( one( moov, tag( "mvhd")), "movie") ; // movie時間
	const auto trackDuration = timing( one( track, tag( "tkhd")), "track").second ; // track時間
	const auto [rate, frames] = timing( one( mdia, tag( "mdhd")), "media") ; // 元frame数
	require( rate == info_.rate && movie_rate > 0, ErrorKind::UNSUPPORTED_CODEC) ;
	info_.timescale = rate ;
	packets( stbl, frames, movie_rate, movie_duration, trackDuration) ;
	checkCancelled( stop_) ;
	info_.metadataBoxes = static_cast<std::uint32_t>( boxes_.size()) ;
	return info_ ;
}
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 単一track M4A内ALACのbox/時間/packetの範囲と宣言整合を検証します
 * @param[in] fileBytes 同じ元streamの実byte数。宣言時間から求めないこと
 * @param[in] readAt 非空callback。入力を変更せず、元例外/codeを保持します
 * @param[in] stop read前後・終了前で確認する協調的取消しtoken
 * @return 全表の照合後の所有属性。失敗時は途中結果を返しません
 * @throws OperationError 不正入力、破損、初回範囲外、資源上限、取消し
 * @note 32byte/read、box4,096/階層8、表entry合計16,777,216で有界です
 * box位置だけを最大4,096件保持し、音声/opaque payload・全tableを確保しません
 * callbackの元OperationErrorを伝播し、確保失敗はInputTooLargeへ変換します
 * 実PCM、ALAC frame内部、checksum、native MF属性の照合は後続工程です
 * @see koefude::inspectM4aAlac
 */
//------------------------------------------------------------------------------
AlacInputInfo inspectM4aAlac( const std::uint64_t fileBytes, const AlacRead &readAt, const std::stop_token stop)
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
} // namespace koefude
