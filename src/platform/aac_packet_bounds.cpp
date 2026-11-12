//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file aac_packet_bounds.cpp
 * @brief 配置表を逐次読み、音声chunkの実mdat範囲を検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 時間を解釈しない独立packet検証器を実装
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
#include "platform/aac_packet_bounds.h"

#include "core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>

namespace koefude {
namespace {
constexpr std::size_t				  K_MAXIMUM_BOXES = 4096 ; // boxとsample/reference entryの合計上限
constexpr std::uint64_t				  K_MAXIMUM_ENTRIES = 16'777'216 ; // packet/表の各走査予算
constexpr auto						  K_ROOT = std::numeric_limits<std::size_t>::max() ; // 元fileを表す親番号

//------------------------------------------------------------------------------
/**
 * @brief 固定4文字のbox型を識別値へ変換します
 * @param[in] text コンパイル時だけ借用する4文字のASCII literal
 * @return big endian識別子
 */
//------------------------------------------------------------------------------
[[nodiscard]] consteval std::uint32_t tag( const std::string_view text)
{
	return (static_cast<std::uint32_t>( text[0]) << 24) | (static_cast<std::uint32_t>( text[1]) << 16) |
		   (static_cast<std::uint32_t>( text[2]) << 8) | static_cast<std::uint32_t>( text[3]) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 条件違反を途中成功なしで拒否します
 * @param[in] condition 必須条件
 * @param[in] kind 不成立時の分類
 */
//------------------------------------------------------------------------------
void require( const bool condition, const ErrorKind kind = ErrorKind::DECODE_FAILED)
{
	// 違反を指定分類のまま呼出し元へ返す
	if( !condition) {
		throw OperationError( kind) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief uint64加算の表現境界を照合します
 * @param[in] left 元の非負値
 * @param[in] right 加える非負値
 * @return 検証済みの和
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t add( const std::uint64_t left, const std::uint64_t right)
{
	require( right <= std::numeric_limits<std::uint64_t>::max() - left, ErrorKind::INPUT_TOO_LARGE) ;
	return left + right ;
}

/** @brief payloadを保持しない検証済みbox位置です */
struct Box {
	/// boxの型
	std::uint32_t kind = 0 ;
	/// payloadの元byte位置
	std::uint64_t begin = 0 ;
	/// 半開の元byte終端
	std::uint64_t end = 0 ;
	/// 直上のbox番号
	std::size_t	  parent = K_ROOT ;
} ;

/** @brief 元file中のmdat payload範囲です */
struct Range {
	/// payload先頭
	std::uint64_t begin = 0 ;
	/// payload半開終端
	std::uint64_t end = 0 ;
} ;

/** @brief stackやpacket件数に依存せずRAII所有する固定metadata領域です */
struct Storage {
	/// 上限内のbox/entry位置
	std::array<Box, K_MAXIMUM_BOXES>   boxes{} ;
	/// 物理順のtop-level mdat位置
	std::array<Range, K_MAXIMUM_BOXES> media{} ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定metadata領域だけを割り当てます
 * @return 呼出し元へ移す唯一所有領域
 * @throws OperationError この割当てのbad_allocをInputTooLargeへ変換します
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<Storage> allocateStorage()
{
	try {
		return std::make_unique<Storage>() ;
	} catch( const std::bad_alloc &) { // 自分の固定領域の不足だけを変換する
		throw OperationError( ErrorKind::INPUT_TOO_LARGE) ;
	}
}

/** @brief 本同期呼出しだけでcallbackとstreamを借用する有界検査状態です */
class Parser {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定領域の所有権と同期read契約を受け取ります
	 * @param[in] fileBytes 元streamの実byte数
	 * @param[in] readAt 検査完了まで生存する非空callback
	 * @param[in] stop 協調取消しtoken
	 * @param[in] storage 非NULLの固定領域。所有権を移します
	 */
	//------------------------------------------------------------------------------
	Parser( const std::uint64_t fileBytes, const AacPacketRead &readAt, const std::stop_token stop,
		   std::unique_ptr<Storage> storage)
		: fileBytes_( fileBytes), readAt_( readAt), stop_( stop), storage_( std::move( storage))
	{
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 全containerと音声trackを照合します
	 * @return 全表が整合した整数metadata
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] AacPacketBounds run()
	{
		walk( 0, fileBytes_, K_ROOT, 0) ;
		// 初回範囲外のfragment/圧縮moovを検査成功にしない
		for( std::size_t index = 0; index < boxes_; ++index) { // 現box番号
			const auto &item = box( index) ; // 固定領域内の借用位置
			require( item.kind != tag( "moof") && item.kind != tag( "mvex") && item.kind != tag( "mfra") &&
						item.kind != tag( "cmov"),
					ErrorKind::UNSUPPORTED_CODEC) ;
			// top-level mdatだけを物理順へ保持する
			if( item.parent == K_ROOT && item.kind == tag( "mdat")) {
				storage_->media[mediaCount_++] = {item.begin, item.end} ;
			}
		}
		const auto movie = one( K_ROOT, tag( "moov")) ; // 唯一のmoov番号
		require( mediaCount_ > 0) ;
		const auto initialBoxes = boxes_ ; // entry追加前のcontainer件数
		bool	   foundTrack = false ; // moov直下trackが存在するか
		// entryの後付けで走査対象を増やさず、元trackだけを検査する
		for( std::size_t index = 0; index < initialBoxes; ++index) { // 元box番号
			// moov直下のtrackだけを音声handlerへ照合する
			if( box( index).parent == movie && box( index).kind == tag( "trak")) {
				foundTrack = true ;
				inspectTrack( index) ;
			}
		}
		require( foundTrack) ;
		require( info_.audioTracks > 0, ErrorKind::UNSUPPORTED_CODEC) ;
		checkCancelled( stop_) ;
		info_.metadataBoxes = static_cast<std::uint32_t>( boxes_) ;
		return info_ ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定領域の位置を同期呼出し中だけ借用します
	 * @param[in] index 既存のbox番号
	 * @return 固定領域内の参照
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const Box &box( const std::size_t index) const
	{
		return storage_->boxes[index] ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 実範囲内の最大8byte整数を同期readします
	 * @param[in] offset 元byte位置
	 * @param[in] width 1～8byteの固定幅
	 * @param[in] end 親の検証済み半開終端
	 * @return big endian整数。元callback例外は伝播します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t readNumber( const std::uint64_t offset, const std::size_t width,
										   const std::uint64_t end) const
	{
		checkCancelled( stop_) ;
		require( end <= fileBytes_ && offset <= end && width > 0 && width <= 8 && width <= end - offset) ;
		std::array<std::byte, 8> data{} ; // 呼出し中だけ有効な固定read buffer
		const auto				 count = readAt_( offset, std::span( data).first( width)) ; // 実充足数
		checkCancelled( stop_) ;
		require( count == width) ;
		std::uint64_t value = 0 ; // 復元する非負整数
		// 満たした固定fieldの終端まで復元する
		for( std::size_t index = 0; index < width; ++index) { // field内のbyte番号
			value = (value << 8) | std::to_integer<std::uint64_t>( data[index]) ;
		}
		return value ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief box外を読まない整数fieldを復元します
	 * @param[in] index box番号
	 * @param[in] offset payload相対byte位置
	 * @param[in] width 固定幅byte数
	 * @return unsigned field値
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t number( const std::size_t index, const std::uint64_t offset,
									   const std::size_t width = 4) const
	{
		const auto &item = box( index) ; // 同じ固定領域内の位置
		require( offset <= item.end - item.begin && width <= item.end - item.begin - offset) ;
		return readNumber( item.begin + offset, width, item.end) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 一つのheaderを実範囲とbox予算へ照合します
	 * @param[in] start 元byte先頭
	 * @param[in] end 親の半開終端
	 * @param[in] parent 親box番号、rootならK_ROOT
	 * @param[in] depth 初回最大8の理解する階層
	 * @return 固定領域へ追加したbox番号
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t atom( const std::uint64_t start, const std::uint64_t end, const std::size_t parent,
								   const unsigned depth)
	{
		checkCancelled( stop_) ;
		require( depth <= 8 && boxes_ < K_MAXIMUM_BOXES, ErrorKind::INPUT_TOO_LARGE) ;
		require( start <= end && end - start >= 8) ;
		auto		  size = readNumber( start, 4, end) ; // 元の宣言byte数
		const auto	  kind = static_cast<std::uint32_t>( readNumber( start + 4, 4, end)) ; // box型
		std::uint64_t header = 8 ; // 通常header長
		// extended sizeを同じ親範囲から読む
		if( size == 1) {
			require( end - start >= 16) ;
			size = readNumber( start + 8, 8, end) ;
			header = 16 ;
		}
		// size0は元file終端のmdatだけを許可する
		if( size == 0) {
			require( parent == K_ROOT && kind == tag( "mdat"), ErrorKind::UNSUPPORTED_CODEC) ;
			size = end - start ;
		}
		require( size >= header && size <= end - start) ;
		const auto index = boxes_++ ; // 追加する上限内の番号
		storage_->boxes[index] = {kind, start + header, start + size, parent} ;
		return index ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 配置を持つcontainerだけを逐次走査します
	 * @param[in] start 元範囲先頭
	 * @param[in] end 元範囲の半開終端
	 * @param[in] parent 親box番号
	 * @param[in] depth 最大8の理解する階層
	 */
	//------------------------------------------------------------------------------
	void walk( std::uint64_t start, const std::uint64_t end, const std::size_t parent, const unsigned depth)
	{
		// 親終端まで進み、音声/編集/未知payloadへ降りない
		while( start < end) {
			const auto	index = atom( start, end, parent, depth) ; // 今回のbox番号
			const auto &item = box( index) ; // 固定領域のため再帰後も生存する位置
			// 配置表と自己包含参照を持つcontainerだけを理解する
			if( item.kind == tag( "moov") || item.kind == tag( "trak") || item.kind == tag( "mdia") ||
			   item.kind == tag( "minf") || item.kind == tag( "stbl") || item.kind == tag( "dinf")) {
				walk( item.begin, item.end, index, depth + 1) ;
			}
			start = item.end ;
		}
		checkCancelled( stop_) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 排他的な必須boxを一つ要求します
	 * @param[in] parent 直上の親番号
	 * @param[in] first 必須型
	 * @param[in] second 0または代替型
	 * @return 唯一のbox番号
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::size_t one( const std::size_t parent, const std::uint32_t first,
								  const std::uint32_t second = 0) const
	{
		std::size_t found = K_ROOT ; // 未発見または唯一の番号
		// 上限内のmetadata位置だけを走査し、重複も拒否する
		for( std::size_t index = 0; index < boxes_; ++index) { // 現box番号
			checkCancelled( stop_) ;
			const auto &item = box( index) ; // 借用する位置
			// 対象親の必須型/代替型だけを候補にする
			if( item.parent == parent && (item.kind == first || (second != 0 && item.kind == second))) {
				require( found == K_ROOT) ;
				found = index ;
			}
		}
		require( found != K_ROOT) ;
		return found ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 初回FullBoxのversion0/flags0を確認します
	 * @param[in] index 検査するbox番号
	 */
	//------------------------------------------------------------------------------
	void full( const std::size_t index) const
	{
		require( number( index, 0) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 表entryの総走査予算を消費します
	 * @param[in] count 正の表entry数
	 */
	//------------------------------------------------------------------------------
	void charge( const std::uint64_t count)
	{
		require( count > 0 && count <= K_MAXIMUM_ENTRIES - tableTotal_, ErrorKind::INPUT_TOO_LARGE) ;
		tableTotal_ += count ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 固定幅表の宣言件数とpayload長を確認します
	 * @param[in] index 表box番号
	 * @param[in] width 各entryの固定byte数
	 * @return 検証済み件数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t table( const std::size_t index, const std::uint64_t width)
	{
		full( index) ;
		const auto count = number( index, 4) ; // 宣言entry件数
		require( count > 0) ;
		charge( count) ;
		require( box( index).end - box( index).begin == 8 + count * width) ;
		return count ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 一音声trackの自己包含参照/descriptionを検査します
	 * @param[in] minimum minf番号
	 * @param[in] sampleTable stbl番号
	 * @return sample description件数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t descriptions( const std::size_t minimum, const std::size_t sampleTable)
	{
		const auto references = one( one( minimum, tag( "dinf")), tag( "dref")) ; // 参照表
		full( references) ;
		const auto referenceCount = number( references, 4) ; // 自己包含参照件数
		require( referenceCount > 0) ;
		auto cursor = box( references).begin + 8 ; // 現在のentry先頭
		// 宣言件数まで進み、外部URL本文を読む前に拒否する
		for( std::uint64_t index = 0; index < referenceCount; ++index) { // 参照番号
			const auto	entry = atom( cursor, box( references).end, references, 6) ; // entry位置
			const auto &item = box( entry) ; // 同期呼出し中の借用
			require( item.kind == tag( "url ") && item.end - item.begin == 4 && number( entry, 0) == 1,
					ErrorKind::UNSUPPORTED_CODEC) ;
			cursor = item.end ;
		}
		require( cursor == box( references).end) ;
		const auto sample = one( sampleTable, tag( "stsd")) ; // 全description表
		full( sample) ;
		const auto count = number( sample, 4) ; // 宣言description数
		require( count > 0) ;
		cursor = box( sample).begin + 8 ;
		// 全descriptionを上限内で確認し、codec属性/ASCは解釈しない
		for( std::uint64_t index = 0; index < count; ++index) { // description番号
			const auto entry = atom( cursor, box( sample).end, sample, 6) ; // entry位置
			require( box( entry).kind == tag( "mp4a"), ErrorKind::UNSUPPORTED_CODEC) ;
			require( box( entry).end - box( entry).begin >= 28) ;
			const auto reference = number( entry, 6, 2) ; // data_reference_index
			require( reference >= 1 && reference <= referenceCount) ;
			require( number( entry, 8, 2) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
			cursor = box( entry).end ;
		}
		require( cursor == box( sample).end) ;
		return count ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 宣言chunk全体を一つの実mdat範囲へ照合します
	 * @param[in] position chunkの元byte先頭
	 * @param[in] length 論理chunk byte数
	 */
	//------------------------------------------------------------------------------
	void mediaBounds( const std::uint64_t position, const std::uint64_t length) const
	{
		const auto	finish = add( position, length) ; // checked半開終端
		std::size_t begin = 0 ; // 二分探索の下端
		auto		end = mediaCount_ ; // 二分探索の上端
		// 物理順のmdatから、位置を含む直前の開始境界を探す
		while( begin < end) {
			checkCancelled( stop_) ;
			const auto middle = begin + (end - begin) / 2 ; // 上限内の中点
			// chunk先頭以前のmdatを右側まで探す
			if( storage_->media[middle].begin <= position) {
				begin = middle + 1 ;
			} else { // chunk先頭より後のmdatは候補から外す
				end = middle ;
			}
		}
		require( begin > 0 && finish <= storage_->media[begin - 1].end) ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief soun trackのサイズ/時間件数/配置表を全件照合します
	 * @param[in] track 元trackのbox番号
	 */
	//------------------------------------------------------------------------------
	void inspectTrack( const std::size_t track)
	{
		checkCancelled( stop_) ;
		const auto media = one( track, tag( "mdia")) ; // 元media container
		const auto handler = one( media, tag( "hdlr")) ; // stream分類
		full( handler) ;
		// 非音声trackのsample table/payloadは初回検査対象に含めない
		if( number( handler, 8) != tag( "soun")) {
			return ;
		}
		++info_.audioTracks ;
		const auto minimum = one( media, tag( "minf")) ; // 音声media情報
		const auto sampleTable = one( minimum, tag( "stbl")) ; // 音声の表container
		const auto descriptionCount = descriptions( minimum, sampleTable) ; // 検証済み件数
		const auto sizes = one( sampleTable, tag( "stsz"), tag( "stz2")) ; // 排他的なサイズ表
		full( sizes) ;
		const auto count = number( sizes, 8) ; // 宣言packet数
		require( count > 0) ;
		require( count <= K_MAXIMUM_ENTRIES - info_.packetCount, ErrorKind::INPUT_TOO_LARGE) ;
		info_.packetCount += count ;
		const auto	  sizeField = number( sizes, 4) ; // uniformまたはstz2幅/予約値
		std::uint64_t uniform = 0 ; // 非zeroなら固定packet byte数
		std::size_t	  width = 32 ; // 個別サイズのbit幅
		std::uint64_t sizeBytes = 0 ; // 個別サイズ表のbyte数
		// stszの固定/個別32bitを扱う
		if( box( sizes).kind == tag( "stsz")) {
			uniform = sizeField ;
			sizeBytes = uniform != 0 ? 0 : count * 4 ;
		} else { // stz2の予約24bitと4/8/16bit幅を照合する
			require( (sizeField >> 8) == 0) ;
			width = static_cast<std::size_t>( sizeField & 255) ;
			require( width == 4 || width == 8 || width == 16, ErrorKind::UNSUPPORTED_CODEC) ;
			sizeBytes = (count * width + 7) / 8 ;
		}
		// 個別表だけを追加の走査予算へ計上する
		if( uniform == 0) {
			charge( count) ;
		}
		require( box( sizes).end - box( sizes).begin == 12 + sizeBytes) ;
		const auto	  timing = one( sampleTable, tag( "stts")) ; // 時間のpacket件数だけ確認する表
		const auto	  timeRows = table( timing, 8) ; // 有界なrun数
		std::uint64_t timeCount = 0 ; // deltaを解釈しないpacket合計
		// 全時間runの件数をサイズ表へ照合し、時間値は読まない
		for( std::uint64_t index = 0; index < timeRows; ++index) { // 時間run番号
			const auto value = number( timing, 8 + index * 8) ; // runのpacket数
			require( value > 0) ;
			timeCount = add( timeCount, value) ;
		}
		require( timeCount == count) ;
		const auto		  offsets = one( sampleTable, tag( "stco"), tag( "co64")) ; // 配置表
		const std::size_t offsetWidth = box( offsets).kind == tag( "co64") ? 8 : 4 ; // 元byte幅
		const auto		  chunks = table( offsets, offsetWidth) ; // 配置されたchunk件数
		const auto		  mapping = one( sampleTable, tag( "stsc")) ; // chunkへのpacket割当て
		const auto		  runs = table( mapping, 12) ; // 割当てrun件数
		std::uint64_t	  previousFirst = 0 ; // 前runのfirst_chunk
		// run順序とdescriptionを実chunk/entry数へ照合する
		for( std::uint64_t index = 0; index < runs; ++index) { // 割当てrun番号
			const auto first = number( mapping, 8 + index * 12) ; // run先頭chunk
			const auto perChunk = number( mapping, 12 + index * 12) ; // 各chunkのpacket数
			const auto description = number( mapping, 16 + index * 12) ; // 使用entry番号
			require( (index != 0 || first == 1) && previousFirst < first && first <= chunks) ;
			require( perChunk > 0 && description >= 1 && description <= descriptionCount) ;
			previousFirst = first ;
		}
		std::uint64_t run = 0 ; // 現在の割当てrun
		auto		  perChunk = number( mapping, 12) ; // 現runのpacket件数
		auto		  nextFirst = runs > 1 ? number( mapping, 20) : chunks + 1 ; // 次runの開始
		std::uint64_t assigned = 0 ; // 既に割り当てたpacket数
		// 全chunkを一回ずつ照合し、packet表全体を確保しない
		for( std::uint64_t chunk = 1; chunk <= chunks; ++chunk) { // 1始まりchunk番号
			checkCancelled( stop_) ;
			// run境界では次の割当てだけを読み直す
			if( chunk == nextFirst) {
				++run ;
				perChunk = number( mapping, 12 + run * 12) ;
				nextFirst = run + 1 < runs ? number( mapping, 20 + run * 12) : chunks + 1 ;
			}
			require( perChunk <= count - assigned) ;
			std::uint64_t length = 0 ; // 今回の連続chunk byte数
			// 固定サイズは表の全packetを読むことなくchecked換算する
			if( uniform != 0) {
				require( perChunk <= std::numeric_limits<std::uint64_t>::max() / uniform, ErrorKind::INPUT_TOO_LARGE) ;
				length = perChunk * uniform ;
			} else { // 個別サイズはこのchunk分だけ逐次readする
				// 割当て終端まで検査し、正のサイズだけをchecked加算する
				for( std::uint64_t index = assigned; index < assigned + perChunk; ++index) {
					// indexはサイズ表内のpacket番号
					std::uint64_t value = 0 ; // 一つのpacket byte数
					// 4bitは上位/下位nibbleを元の順序で復元する
					if( width == 4) {
						const auto packed = number( sizes, 12 + index / 2, 1) ; // 圧縮された2entry
						value = index % 2 == 0 ? packed >> 4 : packed & 15 ;
					} else { // 8/16/32bitの固定幅をそのまま読む
						value = number( sizes, 12 + index * (width / 8), width / 8) ;
					}
					require( value > 0) ;
					length = add( length, value) ;
				}
			}
			const auto position = number( offsets, 8 + (chunk - 1) * offsetWidth,
										 offsetWidth) ; // 元file中のchunk先頭
			mediaBounds( position, length) ;
			assigned += perChunk ;
			info_.audioBytes = add( info_.audioBytes, length) ;
			checkCancelled( stop_) ;
		}
		require( assigned == count) ;
	}

	/// 同じ元streamの実長
	std::uint64_t			 fileBytes_ ;
	/// 同期呼出し中だけ借用するcallback
	const AacPacketRead		&readAt_ ;
	/// 読込み/走査で確認する協調停止
	std::stop_token			 stop_ ;
	/// 固定容量の唯一所有metadata
	std::unique_ptr<Storage> storage_ ;
	/// 使用済みbox/entry件数
	std::size_t				 boxes_ = 0 ;
	/// 物理順mdat件数
	std::size_t				 mediaCount_ = 0 ;
	/// 消費済み表entry走査予算
	std::uint64_t			 tableTotal_ = 0 ;
	/// 全成功まで外部へ渡さない途中metadata
	AacPacketBounds			 info_ ;
} ;
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 自己包含の非fragmented M4Aの音声packet範囲を検査します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 非空の同期callback。借用を保存せず元I/O例外を伝播します
 * @param[in] stop read/走査の前後と返却前に確認する取消しtoken
 * @return 全照合後の所有metadata。失敗時は途中値を返しません
 * @throws OperationError 構造不正、範囲外、資源上限、空callback、取消し
 * @note 最大32byte/read・4,096box/entry・階層8・packet/table各16,777,216
 * 固定metadata領域だけをRAII所有し、その割当て失敗はInputTooLargeです
 * 音声/packet配列を確保せず、edit/時間・ASC・圧縮内部を検査しません
 * mp4aをAAC-LCと断定しません。native属性/PCMと製品接続は呼出し側の責務です
 * @see koefude::inspectM4aAacPacketBounds
 */
//------------------------------------------------------------------------------
AacPacketBounds inspectM4aAacPacketBounds( const std::uint64_t fileBytes, const AacPacketRead &readAt,
										  const std::stop_token stop)
{
	require( static_cast<bool>( readAt), ErrorKind::INVALID_INPUT) ;
	require( fileBytes >= 8) ;
	checkCancelled( stop) ;
	Parser parser( fileBytes, readAt, stop, allocateStorage()) ; // 呼出し内だけの唯一所有状態
	return parser.run() ;
}
} // namespace koefude
