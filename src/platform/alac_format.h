//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file alac_format.h
 * @brief M4A内ALACのboxとpacket表を有界な同期読込みで検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品接続前のcontainer/packet契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note ALAC frame内部や実decodeの完全性を保証しません
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {

/** @brief 検証済みの宣言属性です。実PCMやnative MF属性とは区別します */
struct AlacInputInfo {
	/// cookieの最大packet frame数
	std::uint32_t frameLength = 0 ;
	/// 初回対象の互換版0
	std::uint32_t compatibleVersion = 0 ;
	/// 元整数PCMの16/24bit深度
	std::uint32_t bits = 0 ;
	/// cookieのtuning値。推奨値だけに制限しない
	std::uint32_t pb = 0 ;
	/// cookieのtuning値
	std::uint32_t mb = 0 ;
	/// cookieのtuning値
	std::uint32_t kb = 0 ;
	/// mono/stereoのchannel数
	std::uint32_t channels = 0 ;
	/// cookieの参考run値
	std::uint32_t maxRun = 0 ;
	/// 既知最大packet byte数。0は不明
	std::uint32_t maxFrameBytes = 0 ;
	/// 参考bit/s。0は不明
	std::uint32_t averageBitRate = 0 ;
	/// 元channel当たりの周波数（Hz）
	std::uint32_t rate = 0 ;
	/// mdhdのtick/s。初回はrateと一致
	std::uint32_t timescale = 0 ;
	/// stts/mdhdを照合した元channel当たりのframe数
	std::uint64_t totalSamples = 0 ;
	/// stts/stsz/stscで一致したpacket数
	std::uint64_t packetCount = 0 ;
	/// offset表のchunk数
	std::uint32_t chunkCount = 0 ;
	/// 参照packetの全byte数。mdat paddingは含まない
	std::uint64_t audioBytes = 0 ;
	/// 元streamの最初のpacket byte位置
	std::uint64_t firstPacketOffset = 0 ;
	/// 元streamの最終packetの半開終端
	std::uint64_t lastPacketEnd = 0 ;
	/// opaque payload内部を除く検査box数
	std::uint32_t metadataBoxes = 0 ;
	/// 数学的16kHz換算のfloor。実PCM数ではない
	std::uint64_t idealOutputSamples = 0 ;
	/// 整数換算した宣言時間（100ns）
	std::uint64_t duration100ns = 0 ;
} ;

/**
 * @brief 同じ元streamへ同期アクセスする借用callbackです
 * @note spanは最大32byte、呼出し中だけ有効。実read数を返してください
 * callback/参照先は検証終了まで生存し、同じworkerで逐次使用します
 */
using AlacRead = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] AlacInputInfo inspectM4aAlac( std::uint64_t fileBytes, const AlacRead &readAt, std::stop_token stop = {}) ;

} // namespace koefude
