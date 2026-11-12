//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file m4a_reference_gate.cpp
 * @brief 有界metadata検査で外部参照をMFへ渡す前に拒否します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 M4Aの参照gateとALAC entry識別を追加
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
#include "platform/m4a_reference_gate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/types.h"

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief ASCII literalのbox型を定数へ変換します
 * @param[in] text 4文字の固定literal
 * @return big endian型識別子
 */
//------------------------------------------------------------------------------
[[nodiscard]] consteval std::uint32_t tag( const std::string_view text)
{
	return (static_cast<std::uint32_t>( text[0]) << 24) | (static_cast<std::uint32_t>( text[1]) << 16) |
		   (static_cast<std::uint32_t>( text[2]) << 8) | static_cast<std::uint32_t>( text[3]) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 不成立の構造を部分成功なしで拒否します
 * @param[in] condition 必須条件
 * @param[in] kind 失敗分類
 */
//------------------------------------------------------------------------------
void require( const bool condition, const ErrorKind kind = ErrorKind::DECODE_FAILED)
{
	// native変換前に不正を返す
	if( !condition) {
		throw OperationError( kind) ;
	}
}
/** @brief callbackを借用しmetadata位置だけを逐次検査する呼出し内objectです */
struct Gate {
	/// 本呼出しより長く生存する同期callback
	const AlacRead			   &readAt ;
	/// 協調停止を確認する借用token
	std::stop_token				stop ;
	/// 元streamの実byte数
	std::uint64_t				fileBytes ;
	/// 最大4,096のbox/entry件数
	std::size_t					boxes = 0 ;
	/// stsdのALAC entryを識別したか
	bool						alac = false ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 親/実長を照合し8byte以内の整数を同期readします
	 * @param[in] offset 元byte位置
	 * @param[in] width 4/8byteの固定幅
	 * @param[in] end 親の半開終端
	 * @return big endian整数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t number( const std::uint64_t offset, const std::size_t width,
									   const std::uint64_t end) const
	{
		checkCancelled( stop) ;
		require( end <= fileBytes && offset <= end && width <= end - offset) ;
		std::array<std::byte, 8> data{} ; // 最大8byteの呼出し内buffer
		const auto				 count = readAt( offset, std::span( data).first( width)) ; // 実充足byte数
		checkCancelled( stop) ;
		require( count == width) ;
		std::uint64_t value = 0 ; // 固定幅の復元値
		// 満たしたfield終端まで読んで数値を復元する
		for( std::size_t index = 0; index < width; ++index) { // field内位置
			value = (value << 8) | std::to_integer<std::uint64_t>( data[index]) ;
		}
		return value ;
	}

	//------------------------------------------------------------------------------
	/**
	 * @brief 必要なcontainer/entryだけを有界に走査します
	 * @param[in] start 元byte先頭
	 * @param[in] end 親の半開終端
	 * @param[in] depth 最大8の理解する階層
	 * @param[in] entries 0は通常box、stsd/drefでは親識別子
	 * @return この範囲の直下box件数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t walk( std::uint64_t start, const std::uint64_t end, const unsigned depth,
									 const std::uint32_t entries = 0)
	{
		std::uint64_t count = 0 ; // 現範囲の直下entry件数
		// 親終端まで進み、未知payloadへは降りない
		while( start < end) {
			require( depth <= 8 && boxes < 4096, ErrorKind::INPUT_TOO_LARGE) ;
			++boxes ;
			++count ;
			auto		  size = number( start, 4, end) ; // 元boxの宣言byte数
			const auto	  kind = static_cast<std::uint32_t>( number( start + 4, 4, end)) ; // 型
			std::uint64_t header = 8 ; // 通常boxのheader byte数
			// extended sizeを同じ親範囲で復元する
			if( size == 1) {
				size = number( start + 8, 8, end) ;
				header = 16 ;
			}
			// size0は終端のtop-level mdatだけを許可する
			if( size == 0) {
				require( depth == 0 && kind == tag( "mdat"), ErrorKind::UNSUPPORTED_CODEC) ;
				size = end - start ;
			}
			require( size >= header && size <= end - start) ;
			const auto payload = start + header ; // 検証済みpayload先頭
			const auto finish = start + size ; // overflowしない親内終端
			// drefの自己包含URLだけを許可し、外部URL本文を読まない
			if( entries == tag( "dref")) {
				require( kind == tag( "url ") && finish - payload == 4 && number( payload, 4, finish) == 1,
						ErrorKind::UNSUPPORTED_CODEC) ;
			}
			// codec型だけを識別する
			else if( entries == tag( "stsd")) {
				alac = alac || kind == tag( "alac") ;
			}
			// 参照やdescriptionを持つ既知containerだけを走査する
			else if( kind == tag( "moov") || kind == tag( "trak") || kind == tag( "mdia") || kind == tag( "minf") ||
					kind == tag( "stbl") || kind == tag( "dinf")) {
				static_cast<void>( walk( payload, finish, depth + 1)) ;
			}
			// 固定entry表
			else if( kind == tag( "dref") || kind == tag( "stsd")) {
				require( number( payload, 4, finish) == 0, ErrorKind::UNSUPPORTED_CODEC) ;
				const auto declared = number( payload + 4, 4, finish) ; // 宣言entry件数
				require( declared > 0) ;
				require( declared <= 4096, ErrorKind::INPUT_TOO_LARGE) ;
				require( walk( payload + 8, finish, depth + 1, kind) == declared) ;
			}
			start = finish ;
		}
		return count ;
	}
} ;
} // namespace

//------------------------------------------------------------------------------
/**
 * @brief M4Aの外部参照を拒否し、ALAC entryの有無を返します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 同じworkerの同期read。最大8byte/spanを満たし、保持しません
 * @param[in] stop 各read前後で確認する取消しtoken
 * @return stsdにALAC entryがあればtrue。明らかな非box入力はfalse
 * @throws OperationError 不正範囲/読込み、走査資源、外部参照、取消し。元例外は伝播
 * @note AACの時間表/track数/codec範囲を検査するAPIではありません
 * @see koefude::inspectM4aLocalReferences
 */
//------------------------------------------------------------------------------
bool inspectM4aLocalReferences( const std::uint64_t fileBytes, const AlacRead &readAt, const std::stop_token stop)
{
	require( static_cast<bool>( readAt), ErrorKind::INVALID_INPUT) ;
	require( fileBytes >= 8) ;
	Gate	   gate{readAt, stop, fileBytes} ; // 本同期呼出しだけで借用する状態
	const auto size = gate.number( 0, 4, fileBytes) ; // 明らかな非boxを識別する値
	const auto kind = gate.number( 4, 4, fileBytes) ; // 元入力の先頭型
	// 非boxの形式判定は従来MFへ委ね、未知の合法先頭boxは走査する
	if( kind != tag( "ftyp") && kind != tag( "moov") && kind != tag( "mdat") && size != 0 && size != 1 &&
	   (size < 8 || size > fileBytes)) {
		return false ;
	}
	static_cast<void>( gate.walk( 0, fileBytes, 0)) ;
	checkCancelled( stop) ;
	return gate.alac ;
}
} // namespace koefude
