//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file audio_data.h
 * @brief 長時間PCMを有界なASR入力へ分ける契約を定義します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 元sampleを欠落・重複させない部分読込みを追加
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
#include <vector>

#include "core/types.h"

namespace koefude {

constexpr std::size_t			 K_ASR_CHUNK_SAMPLES = 10 * 60 * K_SAMPLE_RATE ; // 一回のASR入力、約36.6MiB

//------------------------------------------------------------------------------
/**
 * @brief 静音を優先して区切った連続PCMを読み込みます
 * @param[in] audio workerが所有する音声。全量をRAMへ読み込みません
 * @param[in] offset 先頭sample番号。次回は返された要素数だけ進めます
 * @param[in] maximumSamples 処理単位の最大要素数。1～K_ASR_CHUNK_SAMPLES
 * @return 空でない所有PCM。失敗時は部分結果を返しません
 * @throws OperationError 範囲外、不正PCM、I/O異常
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> readAsrChunk( const AudioData &audio, std::uint64_t offset,
											  std::size_t maximumSamples = K_ASR_CHUNK_SAMPLES) ;

} // namespace koefude
