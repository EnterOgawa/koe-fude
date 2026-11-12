//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file button_icons.h
 * @brief 標準buttonのDPI別アイコンを所有します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 テキスト付き操作アイコンを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <array>
#include <memory>
#include <windows.h>

// Win32基本型を定義した後に標準controlの宣言を読み込む
#include <commctrl.h>

namespace koefude {
/** @brief 所有image listを対応するWin32 APIで解放します */
struct ImageListDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief controlから外れたimage listを解放します
	 * @param[in] value unique_ptrが所有する非NULLのimage list
	 */
	//------------------------------------------------------------------------------
	void operator()( HIMAGELIST value) const noexcept
	{
		ImageList_Destroy( value) ;
	}
} ;
/** @brief LoadImageWで取得した非共有iconを解放します */
struct IconDeleter {
	//------------------------------------------------------------------------------
	/**
	 * @brief Windowから外れたiconを解放します
	 * @param[in] value unique_ptrが所有する非NULLのicon
	 */
	//------------------------------------------------------------------------------
	void operator()( HICON value) const noexcept
	{
		DestroyIcon( value) ;
	}
} ;
/** @brief buttonの寿命中保持する画像列です。Window破棄後に解放します */
class ButtonIcons final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 操作buttonへ現在のDPIの画像を設定します
	 * @param[in] window 操作buttonを所有する親Window。UI threadから呼び出すこと
	 * @note 同期messageで画像を差し替えた後に旧image listを解放します
	 * @throws OperationError 画像resource不足または標準control設定失敗
	 */
	//------------------------------------------------------------------------------
	void update( HWND window) ;

	private:
	/// buttonが借用する画像
	std::array<std::unique_ptr<_IMAGELIST, ImageListDeleter>, 6> images_ ;
	/// Windowが借用する大小icon
	std::array<std::unique_ptr<HICON__, IconDeleter>, 2>		 appIcons_ ;
} ;
} // namespace koefude
