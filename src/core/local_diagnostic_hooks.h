//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file local_diagnostic_hooks.h
 * @brief 通常workerの所有DTOから安全な診断metadataだけを投影します
 * @version 1.0.0
 * @date 2026/11/11
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/11 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1397のworker/終端投影を追加
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

#include "core/local_diagnostics.h"

namespace koefude {
struct JobRequest ; // 本文をloggerへ渡さず専用projectionで読む要求
struct JobEvent ; // 終端の小metadataだけを読み取る所有通知
struct ModelPreparationRequest ; // pathを出力しない固定準備要求
struct ModelPreparationEvent ; // planから件数だけ読む準備通知

//------------------------------------------------------------------------------
/**
 * @brief 設定の固定enumだけを投影します
 * @param[in] options 同期borrow、pathは読まない
 * @return 小さい固定値
 */
//------------------------------------------------------------------------------
[[nodiscard]] LocalDiagnosticRecord localConfiguration( const EngineOptions &options) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 要求の用途/モデル/IDだけを投影します
 * @param[in] id 操作ID
 * @param[in] request 同期borrow、秘密/本文を出力しない
 * @return 本文を所有しない小metadata。例外文字列は破棄
 */
//------------------------------------------------------------------------------
[[nodiscard]] LocalDiagnosticRecord localJobStarted( std::uint64_t id, const JobRequest &request) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief controllerが確定した終端だけを投影します
 * @param[in] event 同期borrow。本文/話者名/pathは読まない
 * @return 小さい固定値。未観測backendはnull
 */
//------------------------------------------------------------------------------
[[nodiscard]] LocalDiagnosticRecord localJobTerminal( const JobEvent &event) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 同じstage/phaseのtickを省きます
 * @param[in] left 前通知
 * @param[in] right 現通知
 * @return 同じ実段階ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool					sameLocalDiagnosticStage( const Progress &left, const Progress &right) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 準備workerの開始を投影します
 * @param[in] id 操作ID
 * @param[in] request 固定要求
 * @return pathなしmetadata
 */
//------------------------------------------------------------------------------
[[nodiscard]] LocalDiagnosticRecord localModelStarted( std::uint64_t					 id,
													  const ModelPreparationRequest &request) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 準備終端と領域別の計画を有限件数で記録します
 * @param[in] log null可
 * @param[in] request 固定要求
 * @param[in] event 同op終端
 */
//------------------------------------------------------------------------------
void logLocalModelTerminal( LocalDiagnosticLog *log, const ModelPreparationRequest &request,
						   const ModelPreparationEvent &event) noexcept ;
} // namespace koefude
