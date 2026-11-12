//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file aac_packet_bounds.h
 * @brief AAC経路の配置表と実mdat範囲を同期検査する契約です
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品未接続の有界packet検証器を追加
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

/** @brief 全表の照合完了後に返す所有metadataです。PCM/時刻は含みません */
struct AacPacketBounds {
	/// 検査した全soun track数
	std::uint32_t audioTracks = 0 ;
	/// 全音声trackのpacket件数合計
	std::uint64_t packetCount = 0 ;
	/// 論理圧縮byte数。重複する領域も各track分を数える
	std::uint64_t audioBytes = 0 ;
	/// 検査したbox/entry件数
	std::uint32_t metadataBoxes = 0 ;
} ;

/**
 * @brief 同じ元streamを同じworkerで同期readする借用callbackです
 * @note offsetは元byte位置、spanは最大32byteで呼出し中だけ有効です
 * 実read数を返し、callback/参照先は検査完了まで生存させます
 */
using AacPacketRead = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] AacPacketBounds inspectM4aAacPacketBounds( std::uint64_t fileBytes, const AacPacketRead &readAt,
														std::stop_token stop = {}) ;

} // namespace koefude
