//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file app_update_native.h
 * @brief 実installerを起動せず検証するnative境界を公開します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の署名済み正例検証を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/app_update.h"
#include <filesystem>
#include <windows.h>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief Innoの固定長resourceの末尾ASCII space paddingだけを除いて完全照合します
 * @param[in] actual 終端NUL除去済み、最大255 UTF-16文字
 * @param[in] expected 固定期待文字列
 * @return 正確な一致。先頭/途中/Unicode空白/制御文字は正規化しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool matchesUpdateResource( std::wstring_view actual, std::wstring_view expected) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 開いたread lockとpathの同じinstallerを検証します
 * @param[in] file 書込/削除共有なしの非所有handle
 * @param[in] path 同じlocal通常file
 * @param[in] asset 期待版/size/SHA
 * @param[in] stop 取消し
 * @param[in] cacheOnly 内部正例試験だけtrue、製品は標準CA照会を許可
 * @throws UpdateFailure trust/hash/製品不一致。installer起動は行いません
 */
//------------------------------------------------------------------------------
void			   verifyUpdateInstaller( HANDLE file, const std::filesystem::path &path, const UpdateAsset &asset,
										 std::stop_token stop, bool cacheOnly = false) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示取得時に、未使用でmarkerのある自作ownerだけを有限回収します
 * @param[in] root 検証済みprivate更新root。未知file/使用中/非正規ownerは保持します
 * @return 回収した空owner数
 * @throws UpdateFailure 64件超過/列挙失敗。再帰削除なし
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::size_t cleanupUpdateStaging( const std::filesystem::path &root) ;
} // namespace koefude
