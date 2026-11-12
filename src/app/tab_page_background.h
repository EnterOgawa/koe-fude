//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file tab_page_background.h
 * @brief 所有タブの背景をpageとラベルの同じ座標系で描画します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1355の背景と透明ラベル描画を共通化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <commctrl.h>
#include <iterator>
#include <uxtheme.h>
#include <vssym32.h>
#include <windows.h>

namespace koefude {
/** @brief UI thread内でOS所有brushを借用し、theme handleを描画内だけ所有します */
struct TabPageBackground final {
	//------------------------------------------------------------------------------
	/**
	 * @brief タブ背景を対象controlのclient原点へ写して描画します
	 * @param[in] tab 同じ設定Windowの標準tab。NULL不可
	 * @param[in] target 背景を描く自所有pageまたはlabel。NULL不可
	 * @param[in] dc 呼出中だけ借用する対象client座標のDC。NULL不可
	 * @return 処理済みならtrue。不正な所有HWND/DCならfalse
	 * @note 画像を取得せず、theme利用不可時は標準dialog色を使用します
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static bool paint( HWND tab, HWND target, HDC dc) noexcept
	{
		// 破棄後や初期化前の通知で別Windowへ描画しません
		if( !IsWindow( tab) || !IsWindow( target) || !dc) {
			return false ;
		}
		RECT bounds{} ; // 対象client内のfallback背景
		GetClientRect( target, &bounds) ;
		FillRect( dc, &bounds, GetSysColorBrush( COLOR_BTNFACE)) ;
		const auto theme = OpenThemeData( tab, L"TAB") ; // 同期描画内だけ所有
		// テーマがない標準tabはdialogと同じsystem背景を使います
		if( theme) {
			RECT page{} ; // 標準tab自身のpage領域
			GetClientRect( tab, &page) ;
			TabCtrl_AdjustRect( tab, FALSE, &page) ;
			// RECTの二点を同期APIへ借用し、対象client原点へ変換します
			MapWindowPoints( tab, target, reinterpret_cast<POINT *>( &page), 2) ;
			// 描画不能時も先に塗った標準背景を保持します
			static_cast<void>( DrawThemeBackground( theme, dc, TABP_BODY, 0, &page, nullptr)) ;
			CloseThemeData( theme) ;
		}
		return true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief staticだけへpage背景と標準有効/無効text色を適用します
	 * @param[in] tab 自所有tab
	 * @param[in] label 自所有static
	 * @param[in] dc WM_CTLCOLORSTATIC中だけ借用するDC
	 * @return OS所有NULL_BRUSH、static以外/描画不可はNULL。削除禁止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] static HBRUSH label( HWND tab, HWND label, HDC dc) noexcept
	{
		wchar_t name[16]{} ; // 標準class名だけ、本文を読みません
		// readonly editやcheckbox/buttonの標準描画を変更しません
		if( !GetClassNameW( label, name, static_cast<int>( std::size( name))) || lstrcmpiW( name, L"Static") != 0 ||
		   !paint( tab, label, dc)) {
			return nullptr ;
		}
		SetBkMode( dc, TRANSPARENT) ;
		SetTextColor( dc, GetSysColor( IsWindowEnabled( label) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)) ;
		// Win32のstock objectはOS所有で、呼出側はDeleteObjectしません
		return static_cast<HBRUSH>( GetStockObject( NULL_BRUSH)) ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief theme/system色/再配置後に所有pageと子を再描画対象へ戻します
	 * @param[in] page 自所有Window。NULL時は無処理
	 */
	//------------------------------------------------------------------------------
	static void invalidate( HWND page) noexcept
	{
		// 作成前のlayout通知からdesktop全体を再描画しません
		if( IsWindow( page)) {
			RedrawWindow( page, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN) ;
		}
	}
} ;
} // namespace koefude
