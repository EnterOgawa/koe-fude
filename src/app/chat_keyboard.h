//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file chat_keyboard.h
 * @brief 本文の選択を保ちTab移動を標準dialog managerへ渡します
 * @version 1.0.0
 * @date 2026/09/27
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/27 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/27 読取り専用本文のTab要求だけを解除
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
 * @brief Rich EditのTab処理要求を外してfocus移動を許可します
 * @param[in] nativeCode DefSubclassProcが返したWM_GETDLGCODEのflag
 * @param[in] key WM_GETDLGCODEのwParam。0の全般照会は変更しません
 * @return Tabでは全key/Tab要求を外したflag、それ以外は元のflag
 * @note 矢印・選択・文字入力の要求は元のcontrolへ維持します
 */
//------------------------------------------------------------------------------
[[nodiscard]] constexpr LRESULT chatDialogCode( const LRESULT nativeCode, const WPARAM key) noexcept
{
	// Tabは本文入力ではなく、標準の次/前controlへの移動として処理する
	if( key == VK_TAB) {
		return nativeCode & ~static_cast<LRESULT>( DLGC_WANTTAB | DLGC_WANTALLKEYS) ;
	}
	return nativeCode ;
}
} // namespace koefude
