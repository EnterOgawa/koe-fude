//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file shared_asr_windows.h
 * @brief 元PCMの共有認識窓を有界に検査・読み込みます
 * @version 1.0.0
 * @date 2026/10/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/09 primary実窓をsecondaryへ渡す共通契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/engines.h"

namespace koefude {

constexpr std::uint64_t K_SHARED_ASR_WINDOW_SAMPLES = 26 * K_SAMPLE_RATE ; // 一窓の最大sample数

//------------------------------------------------------------------------------
/**
 * @brief 元sample窓の件数/byte・順序・全PCM範囲を検査します
 * @param[in] windows 最大K_MAXIMUM_SEGMENTSの借用窓。空可
 * @param[in] sampleCount 正の全PCM sample数。録音時間の製品固定上限ではありません
 * @throws OperationError 件数/byte超過、不正・逆順・重複・範囲外窓
 */
//------------------------------------------------------------------------------
void					checkSharedAsrWindows( std::span<const AsrSampleWindow> windows, std::uint64_t sampleCount) ;

//------------------------------------------------------------------------------
/**
 * @brief 検査済み窓の原PCMだけを有界bufferへ読み込みます
 * @param[in] audio 呼出し中有効な全PCM。内容や原点を変更しません
 * @param[in] window 元sampleの半開窓。最大26秒です
 * @return 元PCMと同じfloat値の所有copy。gain・正規化を加えません
 * @throws OperationError 不正範囲、不正PCM、I/O失敗
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> readSharedAsrWindow( const AudioData &audio, AsrSampleWindow window) ;

//------------------------------------------------------------------------------
/**
 * @brief 明示観測時だけ実窓を有限追加し、直前窓との境界を検査します
 * @param[in,out] recognition workerの所有部分結果。失敗前のOriginal/sidecarは保持します
 * @param[in] window nativeへ実際に渡した元sample窓
 * @param[in] sampleCount 同じ全原PCMのsample数
 * @param[in] observer 同期借用通知。空なら通常認識として窓追加/新cap検査を行いません
 * @throws OperationError 観測窓の件数/byte/境界超過。callback例外は所有RAII scopeへ戻ります
 */
//------------------------------------------------------------------------------
void captureAsrWindow( Recognition &recognition, AsrSampleWindow window, std::uint64_t sampleCount,
					  const RecognitionObserver &observer) ;

} // namespace koefude
