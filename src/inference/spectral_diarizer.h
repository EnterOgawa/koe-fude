//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file spectral_diarizer.h
 * @brief clustering前の音声特徴をNME-SCへ渡すworker契約です
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 試用NME-SC経路を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <stop_token>
#include <vector>

#include "core/engines.h"
#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 検証済みモデルとNME-SCで人数と話者区間を推定します
 * @param[in] audio 16kHz mono PCMの所有元。部分読込みし固定時間上限を置きません
 * @param[in] options worker開始時に固定した設定
 * @param[in] stop 同期API境界で確認する取消し状態
 * @param[in] progress 数量だけをworkerへ通知するcallback
 * @return 元音声基準の所有区間。音声根拠がない場合は空
 * @throws OperationError 入力・モデル・数値計算の異常または取消し
 * @note 音声特徴は本呼出し中だけ保持し、UIやlogへ渡しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<SpeakerTurn> spectralDiarize( const AudioData &audio, const EngineOptions &options,
													   std::stop_token stop, const ProgressCallback &progress) ;

} // namespace koefude
