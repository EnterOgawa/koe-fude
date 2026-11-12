//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_store.h
 * @brief V7と独立したAPI sidecar schema1/2を定義します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 source key付きAPI sidecarを追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/history.h"
#include <span>
namespace koefude {
constexpr std::size_t					K_API_TEXT_STORE_LIMIT = 128 * 1024 * 1024 ; // SHA込みsidecar上限
//------------------------------------------------------------------------------
/**
 * @brief runtime API値だけを符号化します
 * @param[in] snapshot 正本
 * @return digest付きschema2
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeApiTextStore( const HistorySnapshot &snapshot) ;
//------------------------------------------------------------------------------
/**
 * @brief 同じ全source keyにだけ結合します
 * @param[in] bytes 検証前byte
 * @param[in,out] snapshot V7読取済み値
 * @throws HistoryError 不正形式、部分適用なし
 */
//------------------------------------------------------------------------------
void decodeApiTextStore( std::span<const std::uint8_t> bytes, HistorySnapshot &snapshot) ;
} // namespace koefude
