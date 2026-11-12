//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history_store.h
 * @brief ユーザー専用folderへ履歴を検証付きで保存するstorageを宣言します
 * @version 1.0.0
 * @date 2026/09/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/10 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/10 LocalAppData・排他・SHA-256・安全な置換を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1286の保存確定・復元・表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1375の適用済み選好の保存と復元を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新境界を追加
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
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "core/history.h"
#include "core/transcription_settings.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 専用workerへ所有権を渡す履歴storageを作成します
 * @param[in] directory 明示したlocal専用folder。空はLocalAppData/koefude/history
 * @return 所有storage。生成時にはI/Oせず、load時に検証・排他・読込みを行います
 * @note folder内のhistory.bin、history.lock、history.partial-*はアプリ専用です
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<IHistoryStorage> makeFileHistoryStorage( std::filesystem::path directory = {}) ;

//------------------------------------------------------------------------------
/**
 * @brief 文字起こし選好だけのprivate storageを生成します。生成時I/Oなし
 * @param[in] directory 空はLocalAppData/koefude/settings、明示値は隔離用
 * @return 専用workerへ移す所有storage
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::unique_ptr<ITranscriptionSettingsStorage>
										makeFileTranscriptionSettingsStorage( std::filesystem::path directory = {}) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定schemaへ選好を符号化します
 * @param[in] value 選好
 * @return v2固定49byte、SHA付き
 * @throws HistoryError 不正選好
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encodeTranscriptionSettings( const TranscriptionPreferences &value) ;
//------------------------------------------------------------------------------
/**
 * @brief 固定v1/v2 schema/範囲/registry/SHAを検証します
 * @param[in] bytes 外部file byte
 * @return 正常時だけ全選好
 * @throws HistoryError 未知版/破損/不正値
 */
//------------------------------------------------------------------------------
[[nodiscard]] TranscriptionPreferences	decodeTranscriptionSettings( std::span<const std::uint8_t> bytes) ;

} // namespace koefude
