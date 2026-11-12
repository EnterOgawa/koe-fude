//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file history_table.h
 * @brief native音声テーブルの実選択とhit位置を安定IDへ変換します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347のcontext対象を固定
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354のtileを含む標準hit testへ変更
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <commctrl.h>
#include <cstdint>
#include <windows.h>
namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 余白/未選択を拒否して実rowの履歴IDを返します
 * @param[in] table UI所有のListView HWND、NULL時0
 * @param[in] client マウスのclient座標、keyboard時未使用
 * @param[in] keyboard 実際の選択行を使うか
 * @return item dataの非0履歴ID、対象なしは0
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline std::uint64_t historyTableTarget( HWND table, POINT client, bool keyboard)
{
	int row = -1 ; // 実rowだけを採用します
	// keyboardは未選択時に先頭行へ補完しません
	if( keyboard) {
		row = ListView_GetNextItem( table, -1, LVNI_SELECTED) ;
	} else {
		LVHITTESTINFO hit{} ; // 同期hit test
		hit.pt = client ;
		row = ListView_HitTest( table, &hit) ;
		// 余白/headerは直前選択へ読み替えません
		if( !(hit.flags & LVHT_ONITEM)) {
			return 0 ;
		}
	}
	// 未選択と削除済み位置を拒否します
	if( row < 0 || row >= ListView_GetItemCount( table)) {
		return 0 ;
	}
	LVITEMW item{} ; // native item dataだけを取得
	item.mask = LVIF_PARAM ;
	item.iItem = row ;
	// 取得失敗や不正IDを0として返します
	if( !ListView_GetItem( table, &item) || item.lParam == 0) {
		return 0 ;
	}
	return static_cast<std::uint64_t>( item.lParam) ;
}
} // namespace koefude
