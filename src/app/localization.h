//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file localization.h
 * @brief UI言語と明示言語resourceの読取りを定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 日本語・英語のUIと言語設定を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <string>
#include <windows.h>

namespace koefude {
/** @brief OS言語と独立して選択する対応UI言語です */
enum class UiLanguage : WORD {
	/// 既定の日本語
	JAPANESE = 0x0411,
	/// 英語
	ENGLISH = 0x0409,
} ;
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語を返します
 * @return 初期値は日本語
 */
//------------------------------------------------------------------------------
[[nodiscard]] UiLanguage   uiLanguage() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語を変更します
 * @param[in] language 対応するUI言語。未知値は拒否
 * @return 適用した場合true。失敗時は現在値を保持
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool		   setUiLanguage( UiLanguage language) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 明示言語の文字列を読みます
 * @param[in] id STRINGTABLEの識別子
 * @param[in] language 読み込む対応言語
 * @return 所有UTF-16文字列。未定義・破損resourceは空
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring resource( UINT id, UiLanguage language) ;
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語で文字列を読みます
 * @param[in] id STRINGTABLEの識別子
 * @return 所有UTF-16文字列。未定義resourceは空
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring resource( UINT id) ;
//------------------------------------------------------------------------------
/**
 * @brief 明示言語の標準menuを作成します
 * @param[in] language 対応するUI言語
 * @return 成功時は呼出し側がDestroyMenuする所有menu。失敗時NULL
 */
//------------------------------------------------------------------------------
[[nodiscard]] HMENU		   loadUiMenu( UiLanguage language) noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 保存済みUI言語を読みます
 * @return 未設定・不正値は日本語
 */
//------------------------------------------------------------------------------
[[nodiscard]] UiLanguage   readUiLanguage() noexcept ;
//------------------------------------------------------------------------------
/**
 * @brief 明示されたUI言語だけをユーザー設定へ保存します
 * @param[in] language 保存する対応言語
 * @return 成功時ERROR_SUCCESS、失敗時Win32 code。認識データは保存しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] LSTATUS	   saveUiLanguage( UiLanguage language) noexcept ;
} // namespace koefude
