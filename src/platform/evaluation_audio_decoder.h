//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file evaluation_audio_decoder.h
 * @brief 公開corpusの短い音声を検証する開発用decoderを宣言します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 Common Voice取込専用の有界decode契約を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 * @note 製品のM4A入口から呼び出さず、一般入力の対応形式を増やしません
 */
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <filesystem>
#include <stop_token>

#include "core/types.h"

namespace koefude {

// 評価用の1clipを実decode長60秒に制限する16kHzのsample数
inline constexpr std::uint64_t K_EVALUATION_MAXIMUM_SAMPLES = 60 * 16'000 ;
// 開発用clipの原fileを最大64MiBへ制限するbyte数
inline constexpr std::uint64_t K_EVALUATION_MAXIMUM_BYTES = 64 * 1024 * 1024 ;

//------------------------------------------------------------------------------
/**
 * @brief localのMP3またはPCM WAVEを評価用の16kHz mono float PCMへ変換します
 * @param[in] path 呼出し中に参照可能な通常local file。拡張子と実codecを検証します
 * @param[in] stop workerからの協調的な取消しtoken
 * @return 原file属性と所有PCM。原fileは変更しません
 * @throws OperationError 不正入力、60秒/64MiB超過、未対応codec、decode失敗、取消し
 * @note COM MTAを使用できるworker向けのblocking APIです。mono/stereoのみを許可し、
 * 失敗時は途中PCMを返さず専用一時領域を解放します。製品入力から使用しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] AudioData		   decodeEvaluationClip( const std::filesystem::path &path, std::stop_token stop = {}) ;

} // namespace koefude
