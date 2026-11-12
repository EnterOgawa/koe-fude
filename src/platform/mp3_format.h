//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file mp3_format.h
 * @brief 有界な部分読取りでMPEG-1/2 Layer IIIの外形を検証します
 * @version 1.0.0
 * @date 2026/10/02
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/02 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/02 製品接続前のframe/ID3外枠検証を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 同期callbackを保持せず、音声payload/CRCやCOM/fileを直接扱いません
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>

namespace koefude {

/** @brief MPEG-1/2 Layer IIIの外形と実decode数とは別のnominal metadataです */
struct Mp3InputInfo {
	/// MPEG-1は1、MPEG-2は2
	std::uint32_t version = 0 ;
	/// 元channel当たりのsample周波数（Hz）
	std::uint32_t rate = 0 ;
	/// 全frameで一定のmono/stereo
	std::uint32_t channels = 0 ;
	/// 宣言範囲を検証した圧縮frame数
	std::uint64_t frames = 0 ;
	/// delay/paddingを差し引かない符号化sample数
	std::uint64_t codedSamples = 0 ;
	/// 原stream先頭から音声開始までのbyte位置
	std::uint64_t audioOffset = 0 ;
	/// 検証したframe列のbyte数。ID3を含まない
	std::uint64_t audioBytes = 0 ;
	/// 数学的16kHz換算。MF実出力数ではない
	std::uint64_t nominalOutputSamples = 0 ;
} ;

/**
 * @brief 元stream基準の位置から小さい借用spanを満たす同期callbackです
 * @note offsetはbyte、spanは最大10byteで呼出し中だけ有効です
 * 実読取り量を返し、元API例外を送出できます。parserは短read/超過返却を拒否します
 * 同じworkerから逐次呼び出し、callbackとspanを後から使用しません
 */
using Mp3Read = std::function<std::size_t( std::uint64_t, std::span<std::byte>)> ;

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
 */
//------------------------------------------------------------------------------
[[nodiscard]] Mp3InputInfo	inspectLayer3Mp3( std::uint64_t fileBytes, const Mp3Read &readAt, std::stop_token stop = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 符号化sample数を商/余り分解で16kHzへ数学的に切り上げ換算します
 * @param[in] codedSamples 元channel当たりの正の符号化sample数
 * @param[in] rate 対応する6種の元周波数（Hz）
 * @return 既存時刻表現内のnominal数。実MF出力数やdelay補正値ではありません
 * @throws OperationError 不正属性はDecodeFailed、時刻表現超過はInputTooLarge
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::uint64_t mp3NominalOutputSamples( std::uint64_t codedSamples, std::uint32_t rate) ;

} // namespace koefude
