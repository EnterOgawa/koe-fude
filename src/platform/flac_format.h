//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file flac_format.h
 * @brief native FLACのmetadataを有界な同期読取りで検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品接続前のSTREAMINFOと範囲検証を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note frame/CRC/MD5とmetadata payloadの完全性を保証しません
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {

/** @brief STREAMINFOの所有属性とmetadata終端です。実decode結果とは区別します */
struct FlacInputInfo {
	/// 元channel当たりの周波数（Hz）
	std::uint32_t rate = 0 ;
	/// 対応するmono/stereoのchannel数
	std::uint32_t channels = 0 ;
	/// 元PCMの16/24bit深度
	std::uint32_t bits = 0 ;
	/// channel当たりの既知総sample数
	std::uint64_t totalSamples = 0 ;
	/// 宣言された最小block sample数
	std::uint32_t minimumBlockSamples = 0 ;
	/// 宣言された最大block sample数
	std::uint32_t maximumBlockSamples = 0 ;
	/// 宣言された最小frame byte数。0は不明
	std::uint32_t minimumFrameBytes = 0 ;
	/// 宣言された最大frame byte数。0は不明
	std::uint32_t maximumFrameBytes = 0 ;
	/// 元stream先頭からの音声開始byte位置
	std::uint64_t audioOffset = 0 ;
	/// metadata後の残りbyte数。frameは未検証
	std::uint64_t audioBytes = 0 ;
	/// STREAMINFOを含む検証済みblock数
	std::uint32_t metadataBlocks = 0 ;
	/// 数学的16kHz換算。実出力数ではない
	std::uint64_t idealOutputSamples = 0 ;
	/// MD5欄が非0かどうか。checksum検証成功ではない
	bool		  md5Present = false ;
} ;

/**
 * @brief 元streamのbyte位置から借用spanを満たす同期callbackです
 * @note spanは最大34byte、呼出し中だけ有効です。実read数を返してください
 * callbackの参照先は検証完了まで生存し、同じworkerから逐次使用します
 * parserはcallback/spanを保持せず、短readと過大返却を拒否します
 */
using FlacRead = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] FlacInputInfo inspectNativeFlac( std::uint64_t fileBytes, const FlacRead &readAt,
											  std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 既知総sample数を商/余りで16kHzへ数学的に切り上げ換算します
 * @param[in] totalSamples 元channel当たりの正の36bit総sample数
 * @param[in] rate 8～192kHzの元周波数（Hz）
 * @return 既存時刻表現内の理想sample数。実MF出力数ではありません
 * @throws OperationError 不正属性はDecodeFailed、時刻表現超過はInputTooLarge
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t flacIdealOutputSamples( std::uint64_t totalSamples, std::uint32_t rate) ;

} // namespace koefude
