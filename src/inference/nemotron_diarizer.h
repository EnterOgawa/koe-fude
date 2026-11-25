//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file nemotron_diarizer.h
 * @brief Nemotron 3のローカル話者分離を宣言
 * @version 1.1.2
 * @date 2026/11/24
 * @author Shunji Ogawa
 *
 * @par 更新履歴
 * <pre>
 * 1.1.1 Shunji Ogawa 2026/11/24 新規作成
 * 1.1.2 Shunji Ogawa 2026/11/25 ADR1456の正式版へ試用時の変更を反映
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
 * @brief ファイル全体で話者を追跡し、元音声基準の区間を推定
 * @param[in] audio 同期呼出し中借用する16 kHz mono float PCM
 * @param[in] options 検証済みweightを探すモデルルートを含むジョブ設定
 * @param[in] stop 読込みとnative呼出しの間で確認する取消し状態
 * @param[in] progress 同期worker上の段階通知先。空を許可
 * @return ファイル内で最初に現れた順の話者IDを持つ区間列。検出なしは空
 * @throws OperationError 取消し、ライブラリー・モデル・推論の失敗、または区間上限超過
 * @note 固定CPU版の自動人数推定を使用し、native呼出し中は協調停止を待つ
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<SpeakerTurn> nemotronDiarize( const AudioData &audio, const EngineOptions &options,
													   std::stop_token stop, const ProgressCallback &progress) ;
} // namespace koefude
