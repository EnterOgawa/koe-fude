//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file win_helpers.h
 * @brief Win32 handleとUnicode変換の共通契約を定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 OS resourceのRAIIと文字コード変換を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <windows.h>

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 通信を伴うpathやreparse/cloud属性を読込み前に拒否します
 * @param[in] path 通常のlocal fileを指定するpath
 * @return 正規化した絶対path
 * @throws OperationError 不正path、remote drive、非通常file、同期属性
 * @note 各祖先directoryも確認します。OSのnamespace変更をlockするAPIではありません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path validateLocalFile( const std::filesystem::path &path) ;

/** @brief CloseHandleを必要とするkernel objectの解放責任を表します */
struct HandleDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効なhandleだけを閉じます
	 * @param[in] handle 所有handle。nullptrとINVALID_HANDLE_VALUEを許可
	 */
	//------------------------------------------------------------------------------
	void operator()( HANDLE handle) const noexcept
	{
		// 作成に失敗したsentinelをCloseHandleへ渡さない
		if( handle && handle != INVALID_HANDLE_VALUE) {
			CloseHandle( handle) ;
		}
	}
} ;

using UniqueHandle = std::unique_ptr<void, HandleDeleter> ; // kernel handleの単独所有型

//------------------------------------------------------------------------------
/**
 * @brief UTF-16を不正surrogateを許可せずUTF-8へ変換します
 * @param[in] value 呼出し中に有効なUTF-16 view
 * @return 所有UTF-8文字列。空入力は空文字列
 * @throws OperationError 不正文字列またはWindows APIの上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::string  toUtf8( std::wstring_view value) ;

//------------------------------------------------------------------------------
/**
 * @brief UTF-8を不正sequenceを許可せずUTF-16へ変換します
 * @param[in] value 呼出し中に有効なUTF-8 view
 * @return 所有UTF-16文字列。空入力は空文字列
 * @throws OperationError 不正文字列またはWindows APIの上限超過
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring toWide( std::string_view value) ;

} // namespace koefude
