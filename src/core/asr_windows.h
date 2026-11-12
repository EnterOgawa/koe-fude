//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file asr_windows.h
 * @brief 検出した発話を元sampleの有界な認識区間へ分けます
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 自動発話境界と連続発話の分割契約を追加
 * 0.1.0 Shunji Ogawa 2026/09/14 固定VADの窓とcentisecondの二段階丸めを共通検証
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
#include <span>
#include <vector>

namespace koefude {

/** @brief 一つの有界PCM内にある半開区間です。要素を所有しません */
struct AsrWindow {
	/// 先頭sample番号
	std::size_t begin = 0 ;
	/// 最後のsampleの次。beginより大きい値
	std::size_t end = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 固定Sileroの窓・時刻丸めを検証し、元PCM内の発話範囲へ戻します
 * @param[in] startCs 検出開始時刻（centisecond）。有限・非負で元PCM終端より前
 * @param[in] endCs 検出終了時刻（centisecond）。開始より後で固定engineの丸め上限内
 * @param[in] sampleCount 元16kHz mono PCMのsample数。1 sample～10分
 * @return 所有権を持たないsample番号の半開区間。許容する終了超過だけ元終端へ制限します
 * @throws OperationError PCM長の範囲外、不正時刻、丸め後の空区間
 * @note 固定Sileroの512 sample窓とwhisper.cppの10ms四捨五入を前提とします
 * 区間同士の順序・重複はsplitAsrWindowsで別に検証します
 */
//------------------------------------------------------------------------------
[[nodiscard]] AsrWindow				 checkedVadWindow( double startCs, double endCs, std::size_t sampleCount) ;

//------------------------------------------------------------------------------
/**
 * @brief 長い発話を静かな位置で分け、検出範囲の欠落・重複を防ぎます
 * @param[in] samples 検証済み16kHz mono PCM。呼出し中だけ借用します
 * @param[in] speech 開始順の非重複発話範囲。空なら空結果です
 * @param[in] maximumSamples 1 sample～10分の分割上限sample数
 * @return 元PCMに対する所有区間列。失敗時に部分結果を返しません
 * @throws OperationError 範囲外、重複、逆順、不正PCM、上限外
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<AsrWindow> splitAsrWindows( std::span<const float> samples, std::span<const AsrWindow> speech,
													 std::size_t maximumSamples) ;

//------------------------------------------------------------------------------
/**
 * @brief 小さい声の検出を補助するため、VADへ渡すcopyだけを有界なgainで増幅します
 * @param[in] samples 16kHz mono PCM。空を許可し、内容は変更しません
 * @return 所有する検出用PCM。ASRの元音声として置き換えないでください
 * @throws OperationError 10分超、非有限値、正規化範囲外
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float>	 prepareVadAudio( std::span<const float> samples) ;

} // namespace koefude
