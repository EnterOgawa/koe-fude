//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_text_store.h
 * @brief 専用version1 sidecarをV8原文から独立して符号化します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の有界sidecarとfloorを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/history.h"
#include <cstdint>
#include <span>
#include <vector>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 最大32行とglobal floorをSHA付きで符号化します
 * @param[in] snapshot 同writerの正本
 * @return 最大64MiB
 * @throws HistoryError 不正/上限
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeSpeakerTextStore( const HistorySnapshot &snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief rawを変えず、同target採用と最大opだけを復元します
 * @param[in] bytes SHA付き有界sidecar
 * @param[in,out] snapshot V8検証済み正本
 * @throws HistoryError 不正/未知版/上限。原文は保持し、部分採用を行いません
 */
//------------------------------------------------------------------------------
void decodeSpeakerTextStore( std::span<const std::uint8_t> bytes, HistorySnapshot &snapshot) ;
} // namespace koefude
