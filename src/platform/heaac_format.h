//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file heaac_format.h
 * @brief HE-AACの宣言とnative core属性を有界に照合します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品接続前のHE検証契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {

/** @brief ASCのcoreとSBR/PS後の所有属性です */
struct HeAacConfig {
	/// 元のaudio object type
	std::uint32_t aot = 0 ;
	/// 初回はLC core2
	std::uint32_t coreAot = 0 ;
	/// SBR前のHz
	std::uint32_t coreRate = 0 ;
	/// PS前のchannel数
	std::uint32_t coreChannels = 0 ;
	/// SBR後のHz
	std::uint32_t outputRate = 0 ;
	/// PS後のchannel数
	std::uint32_t outputChannels = 0 ;
	/// explicit SBRを持つ場合true
	bool		  sbr = false ;
	/// explicit PSを持つ場合true
	bool		  ps = false ;
	/// 初回HEの44.1/48kHzならtrue。LCはfalse
	bool		  initialHeCandidate = false ;
} ;

/** @brief 全packet表の検証後の宣言です。実PCMとは区別します */
struct HeAacInputInfo {
	/// 所有するASC属性
	HeAacConfig	  config ;
	/// mdhdの復号後tick/s
	std::uint32_t timescale = 0 ;
	/// 復号後のchannel当たり宣言frame数
	std::uint64_t totalSamples = 0 ;
	/// 全tableで一致したpacket数
	std::uint64_t packetCount = 0 ;
	/// offset表のchunk数
	std::uint32_t chunkCount = 0 ;
	/// 使用packetの合計byte数
	std::uint64_t audioBytes = 0 ;
	/// 元streamの最初のpacket位置
	std::uint64_t firstPacketOffset = 0 ;
	/// 最終packetの半開終端byte
	std::uint64_t lastPacketEnd = 0 ;
	/// opaque内部を除くbox件数
	std::uint32_t metadataBoxes = 0 ;
	/// 16kHz出力数の整数下限
	std::uint64_t outputFloor = 0 ;
	/// 16kHz出力数の整数上限
	std::uint64_t outputCeil = 0 ;
	/// 全mediaの整数100ns時間
	std::uint64_t duration100ns = 0 ;
} ;

/**
 * @brief 同じ元streamを同期readする借用callbackです
 * @note 要求は最大32byte、spanは呼出し中だけ有効。実read数を返します
 * callback/参照先は検査終了まで生存し、同じworkerで逐次使用します
 */
using HeAacRead = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

//------------------------------------------------------------------------------
/**
 * @brief sample entryのASCが明示HEか識別し、全検査を必要とする分岐を返します
 * @param[in] fileBytes 同じ元streamの実byte数
 * @param[in] readAt 同期呼出し中だけ生存する最大32byte/readのcallback
 * @param[in] stop read境界で確認する取消しtoken
 * @return mp4aのesdsに明示AOT 5/29が一つでもあればtrue
 * @throws OperationError 構造/資源/I/O/取消し。元codeを維持します
 * @note ASC全体やHE対応を認定せず、trueはinspectM4aHeaacでの全検査を必須にします
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool hasExplicitM4aHeaac( std::uint64_t fileBytes, const HeAacRead &readAt, std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 最大64byteのASCを構造と初回属性へ照合します
 * @param[in] asc 呼出し中だけ生存するASC借用byte。保存しません
 * @return 所有属性。LCは初回HE候補falseとして返します
 * @throws OperationError 構造不正、対象外、資源上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] HeAacConfig	 inspectHeaacAsc( std::span<const std::byte> asc) ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] HeAacInputInfo inspectM4aHeaac( std::uint64_t fileBytes, const HeAacRead &readAt,
											 std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief MFのcore属性とUserDataを検証済みASCへ照合します
 * @param[in] expected 同じstreamの検証済み初回HE属性
 * @param[in] coreRate MF native属性のSBR前Hz
 * @param[in] coreChannels MF native属性のPS前channel数
 * @param[in] userData 呼出し中だけ借用する最大76byteのMF UserData
 * @throws OperationError 不一致、構造不正、初回範囲外、資源上限
 * @note full AAC GUIDと実出力属性の判定は製品接続側の責務です
 */
//------------------------------------------------------------------------------
void verifyHeaacNative( const HeAacConfig &expected, std::uint32_t coreRate, std::uint32_t coreChannels,
					   std::span<const std::byte> userData) ;

} // namespace koefude
