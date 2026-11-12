//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file status_progress.cpp
 * @brief status全幅の進捗と段階説明を一度に描画します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 進捗をstatus帯全体へ統合
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1353の待機稼働帯を確定率と分離して描画
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/status_progress.h"
#include <algorithm>
#include <commctrl.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 完了率と独立した下端の不定帯範囲を計算します
 * @param[in] area 自status client領域
 * @param[in] frame 有界に巡回する描画位相
 * @return client内の下端3pixel以内。空領域では空矩形
 * @see koefude::statusActivityArea
 */
//------------------------------------------------------------------------------
RECT statusActivityArea( const RECT &area, unsigned frame) noexcept
{
	// 最小化等の空領域へ描画範囲を作りません
	if( area.right <= area.left || area.bottom <= area.top) {
		return {} ;
	}
	const LONG	   width = area.right - area.left ; // 自controlのclient幅
	const LONG	   band = std::min( width, std::clamp( width / 5, 12L, 96L)) ; // 読める細帯
	const unsigned phase = frame % 40 ; // 一周期4秒。生成率を表しません
	const int	   offset = MulDiv( width - band, static_cast<int>( phase < 20 ? phase : 40 - phase),
								   20) ; // client内を往復する表示専用位置
	return {area.left + offset, std::max( area.top, area.bottom - 3), area.left + offset + band, area.bottom} ;
}
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 全背景、確定進捗、説明textを重ねます
 * @param[in] window 標準status HWND
 * @param[in] dc OS所有の描画context
 * @param[in] state UI threadで固定された値
 */
//------------------------------------------------------------------------------
void paintStatus( HWND window, HDC dc, const StatusProgress &state) noexcept
{
	RECT area{} ; // status全体の描画範囲
	GetClientRect( window, &area) ;
	FillRect( dc, &area, GetSysColorBrush( COLOR_BTNFACE)) ;
	RECT fill = area ; // 確定進捗までの背景範囲
	fill.right = MulDiv( area.right, std::clamp( state.percent, 0, 100), 100) ;
	const HBRUSH brush = CreateSolidBrush( RGB(202, 232, 215)) ; // 説明を読める明るい進捗色
	// GDI確保失敗時も説明textは残す
	if( brush) {
		FillRect( dc, &fill, brush) ;
		DeleteObject( brush) ;
	}
	// 細い稼働帯は確定した塗りの幅を変えず、応答前も処理中と分かる表示です
	if( state.activity) {
		const RECT	 band = statusActivityArea( area, state.activityFrame) ; // 自client内だけ
		const HBRUSH activity = CreateSolidBrush( RGB(62, 139, 106)) ; // 確定進捗より濃い細帯
		// GDI不足時にも確定率と説明を維持します
		if( activity) {
			FillRect( dc, &band, activity) ;
			DeleteObject( activity) ;
		}
	}
	// 描画終了時にDCへ戻す、元の非所有GDI object
	const HGDIOBJ previous = SelectObject( dc, reinterpret_cast<HFONT>( SendMessageW( window, WM_GETFONT, 0, 0))) ;
	SetBkMode( dc, TRANSPARENT) ;
	SetTextColor( dc, RGB(20, 35, 27)) ;
	area.left += MulDiv( 6, static_cast<int>( GetDpiForWindow( window)), 96) ;
	area.right -= 6 ;
	DrawTextW( dc, state.text.c_str(), static_cast<int>( state.text.size()), &area,
			  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) ;
	SelectObject( dc, previous) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 標準statusの描画だけを差し替えます
 * @param[in] window status HWND
 * @param[in] message OS message
 * @param[in] wparam message固有値
 * @param[in] lparam message固有値
 * @param[in] id subclass識別子
 * @param[in] reference HWNDより長寿命のStatusProgressへのborrow
 * @return 処理済み0、そのほかは標準controlの結果
 */
//------------------------------------------------------------------------------
LRESULT CALLBACK statusProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
							DWORD_PTR reference) noexcept
{
	const auto &state = *reinterpret_cast<const StatusProgress *>( reference) ; // 同一UI threadのAPI境界
	// 通常描画で背景とtextを一度に描く
	if( message == WM_PAINT) {
		PAINTSTRUCT paint{} ; // OSが管理する更新範囲
		const HDC	dc = BeginPaint( window, &paint) ; // EndPaintまで有効なborrow
		paintStatus( window, dc, state) ;
		EndPaint( window, &paint) ;
		return 0 ;
	}
	// screenshotでも通常画面と同じ進捗を描く
	if( message == WM_PRINTCLIENT) {
		paintStatus( window, reinterpret_cast<HDC>( wparam), state) ;
		return 0 ;
	}
	// 全領域をWM_PAINTで塗るため、二重消去によるちらつきを避ける
	if( message == WM_ERASEBKGND) {
		return 1 ;
	}
	// Window破棄時にborrowを解除する
	if( message == WM_NCDESTROY) {
		RemoveWindowSubclass( window, statusProc, id) ;
	}
	return DefSubclassProc( window, message, wparam, lparam) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 標準statusのtext機能を維持して全幅進捗描画を登録します
 * @param[in] control UI所有のstatus HWND
 * @param[in,out] state HWND破棄まで有効なUI専用状態
 * @return 登録成功時true
 * @see koefude::attachStatusProgress
 */
//------------------------------------------------------------------------------
bool attachStatusProgress( HWND control, StatusProgress &state)
{
	return SetWindowSubclass( control, statusProc, 1, reinterpret_cast<DWORD_PTR>( &state)) != FALSE ;
}

} // namespace koefude
