//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file wave_format.h
 * @brief 有界な部分読取りでPCM16 RIFF/WAVEを検証する契約を宣言します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品接続前のPCM16 container検証を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note COM・file・音声変換を直接扱わず、callbackを呼出し後に保持しません
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {

inline constexpr std::uint32_t K_MAXIMUM_WAVE_CHUNKS = 4096 ; // 上位chunk走査の件数上限
inline constexpr std::uint64_t K_MAXIMUM_RIFF_BYTES = 0xFFFFFFFFULL + 8 ; // RIFF sizeと先頭8byte

/** @brief 検証済みPCM16の元属性と有界処理用の所有metadataです */
struct WaveInputInfo {
	/// 元の1channel当たりのsample周波数（Hz）
	std::uint32_t rate = 0 ;
	/// 許可したmonoまたはstereoのchannel数
	std::uint32_t channels = 0 ;
	/// 全channelを一組にした原frame数
	std::uint64_t frames = 0 ;
	/// 原stream先頭からPCM payloadまでのbyte位置
	std::uint64_t dataOffset = 0 ;
	/// PCM payloadのbyte数。paddingを含まない
	std::uint64_t dataBytes = 0 ;
	/// 最上位で確認したfmt/data/metadataの件数
	std::uint32_t chunkCount = 0 ;
	/// 16kHz monoへの変換数の切上げ値
	std::uint64_t idealOutputSamples = 0 ;
} ;

/**
 * @brief 指定byte位置を出力spanへ読み、実読取り量を返す借用callbackです
 * @note offsetは原stream先頭基準、spanは最大18byteで呼出し中だけ有効です
 * 返却量はspan size以内とし、失敗時は元API例外を送出できます
 * 同じworkerで逐次呼び出します。parserは部分読取りをDecodeFailedとして拒否します
 */
using WaveRead = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] WaveInputInfo inspectPcm16Wave( std::uint64_t fileBytes, const WaveRead &readAt,
											 std::stop_token stop = {}) ;

} // namespace koefude
