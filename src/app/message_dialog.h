//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file message_dialog.h
 * @brief アプリ所有の案内・確認とMS ゴシックの契約を定義します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1382の共通案内と明示二択を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <windows.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 既存アプリのOK/YESNO/YESNOCANCEL案内をMS ゴシックで表示します
 * @param[in] owner 同じUI threadの非所有親HWND。起動失敗時はNULL可
 * @param[in] text 呼出し終了まで有効なUTF-16本文。NULL不可
 * @param[in] title 呼出し終了まで有効なUTF-16題名。NULL不可
 * @param[in] flags 既存呼出しのbutton種別・既定番号・標準icon
 * @return IDOK/IDYES/IDNO/IDCANCEL。生成失敗/未対応flagsは0、GetLastErrorに診断値
 * @note 自UI thread専用のowner modal。設定・通信・保存を行いません
 */
//------------------------------------------------------------------------------
int				  showMessageDialog( HWND owner, const wchar_t *text, const wchar_t *title, UINT flags) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 既定を辞退にしたParakeetの二択を同じfontで表示します
 * @param[in] owner 同じUI threadの非所有親HWND。NULL不可
 * @param[in] text 呼出し中だけ借用するUTF-16本文。NULL不可
 * @param[in] title 借用するUTF-16題名。NULL不可
 * @param[in] improve 借用する明示同意button。NULL不可
 * @param[in] later 借用する辞退button。NULL不可
 * @return IDYES/IDNO。close/EscapeはIDCANCEL、生成失敗は0
 */
//------------------------------------------------------------------------------
[[nodiscard]] int showImproveDialog( HWND owner, const wchar_t *text, const wchar_t *title, const wchar_t *improve,
									const wchar_t *later) noexcept ;
} // namespace koefude
