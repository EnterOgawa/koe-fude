//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file smooth_scroll.cpp
 * @brief UIを待機させず、wheelとpageの縦移動を短時間で補間します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 入力の加算・反転・破棄に対応するscroll補間を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の比較縦scroll同期と表示anchorを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1362のnative追跡/終端後の有限観測と世代照合を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363で旧比較追従観測を退役し通常補間を保持
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
#include "app/smooth_scroll.h"
#include "app/comparison_scroll.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <richedit.h>

namespace koefude {
namespace {

constexpr UINT_PTR K_SCROLL_TIMER = 0x4B46 ; // このRich Editだけで使うtimer ID
constexpr double   K_SCROLL_DURATION = 160.0 ; // 一回の補間時間（ms）

//------------------------------------------------------------------------------
/**
 * @brief 補間の通知を止め、残ったtimer messageを無効にします
 * @param[in] window 対象Rich Edit
 * @param[in,out] state UI threadの借用状態
 */
//------------------------------------------------------------------------------
void			   stopScroll( const HWND window, SmoothScroll &state) noexcept
{
	KillTimer( window, K_SCROLL_TIMER) ;
	state.active = false ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現在の表示位置をpixel単位で取得します
 * @param[in] window 対象Rich Edit
 * @return controlの仮想文書座標
 */
//------------------------------------------------------------------------------
[[nodiscard]] POINT currentPosition( const HWND window) noexcept
{
	POINT point{} ; // 同期messageでcontrolが書き込むpixel座標
	SendMessageW( window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &point)) ;
	point.y = comparisonScrollRange( window).position ; // 長文も32bitで保持
	return point ;
}

//------------------------------------------------------------------------------
/**
 * @brief 位置を反映し、色付きtableを含む画面の再描画を要求します
 * @param[in] window 対象Rich Edit
 * @param[in,out] state 同期messageの再入を区別する状態
 * @param[in] position 有限でscroll範囲内のpixel位置
 */
//------------------------------------------------------------------------------
void applyPosition( const HWND window, SmoothScroll &state, const double position) noexcept
{
	POINT point = currentPosition( window) ; // 横位置は変更しない
	point.y = static_cast<LONG>( std::lround( position)) ;
	state.applying = true ;
	SendMessageW( window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>( &point)) ;
	state.applying = false ;
	InvalidateRect( window, nullptr, FALSE) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 現在のfontから一行分の移動量を取得します
 * @param[in] window 対象Rich Edit
 * @return 1以上の高さ（pixel）。GDIを使えない場合はDPIに応じた既定値
 */
//------------------------------------------------------------------------------
[[nodiscard]] int lineHeight( const HWND window) noexcept
{
	const int fallback = MulDiv( 18, static_cast<int>( GetDpiForWindow( window)), 96) ; // 既定行高
	const HDC dc = GetDC( window) ; // ReleaseDCで返す非所有描画context
	// 補助的なfont照会が失敗しても有限の移動量で操作を維持する
	if( !dc) {
		return fallback ;
	}
	const auto	  font = reinterpret_cast<HFONT>( SendMessageW( window, WM_GETFONT, 0, 0)) ; // 非所有font
	const HGDIOBJ previous = SelectObject( dc, font) ; // 選択前のGDI object
	TEXTMETRICW	  metric{} ; // 現在のfontの高さ
	const bool	  available = GetTextMetricsW( dc, &metric) != FALSE ; // 照会成否
	SelectObject( dc, previous) ;
	ReleaseDC( window, dc) ;
	return available ? std::max( 1L, metric.tmHeight + metric.tmExternalLeading) : fallback ;
}

//------------------------------------------------------------------------------
/**
 * @brief 新しい移動量を加算し、現在位置から補間を開始します
 * @param[in] window 対象Rich Edit
 * @param[in,out] state 未完了の移動先も保持するUI状態
 * @param[in] delta 上が負、下が正の移動量（pixel）
 */
//------------------------------------------------------------------------------
void startScroll( const HWND window, SmoothScroll &state, const double delta) noexcept
{
	SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE} ; // 文書の有限な縦scroll範囲
	// scroll範囲を取得できないcontrolでは移動先を捏造しない
	if( !GetScrollInfo( window, SB_VERT, &info)) {
		stopScroll( window, state) ;
		return ;
	}
	const POINT	 point = currentPosition( window) ; // 連続入力でも現在の描画位置から再開
	const double maximum = std::max( 0.0, static_cast<double>( info.nMax) - info.nPage + 1) ; // 末尾
	const bool	 sameDirection = state.active && (state.target - point.y) * delta >= 0 ; // 加算可否
	// 逆方向の入力では未完了の移動量を捨て、指示された方向へ直ちに向きを変える
	state.target = std::clamp( (sameDirection ? state.target : point.y) + delta, 0.0, maximum) ;
	state.origin = point.y ;
	state.started = GetTickCount64() ;
	BOOL animation = TRUE ; // OSのアクセシビリティ設定。照会失敗時は補間を有効にする
	SystemParametersInfoW( SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) ;
	// OSで動きを抑制した設定と移動不要の場合はtimerを開始しない
	if( !animation || std::abs( state.target - state.origin) < 0.5) {
		applyPosition( window, state, state.target) ;
		stopScroll( window, state) ;
		return ;
	}
	state.active = true ;
	// timer資源が不足した場合も、その入力の移動自体は即時に完了させる
	if( !SetTimer( window, K_SCROLL_TIMER, 15, nullptr)) {
		applyPosition( window, state, state.target) ;
		stopScroll( window, state) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 入力とtimerを処理し、選択・直接操作時は標準controlへ戻します
 * @param[in] window 対象Rich Edit
 * @param[in] message OS message
 * @param[in] wparam message固有値
 * @param[in] lparam message固有値
 * @param[in] id subclass識別子
 * @param[in] reference HWNDより長寿命のSmoothScrollの借用pointer
 * @return 処理済みは0、そのほかは標準controlの結果
 */
//------------------------------------------------------------------------------
LRESULT CALLBACK scrollProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
							DWORD_PTR reference) noexcept
{
	auto &state = *reinterpret_cast<SmoothScroll *>( reference) ; // 同一UI threadのAPI境界
	// 所有timerだけを処理し、親Windowの進捗timerと混同しない
	if( message == WM_TIMER && wparam == K_SCROLL_TIMER) {
		// KillTimer直前に配送された古いmessageで移動を再開しない
		if( state.active) {
			// clockから求める有限の経過比であり、推論の進捗とは独立している
			const double fraction = std::min( 1.0, (GetTickCount64() - state.started) / K_SCROLL_DURATION) ;
			const double rest = 1.0 - fraction ; // 終端で減速する三次補間の残時間比
			applyPosition( window, state, state.origin + (state.target - state.origin) * (1.0 - rest * rest * rest)) ;
			// 所定時間で必ずtimerを止め、末尾到達でも無期限に再描画しない
			if( fraction >= 1.0) {
				stopScroll( window, state) ;
			}
		}
		return 0 ;
	}
	// Ctrl/Shift操作は標準のzoom・横移動に譲り、通常のwheelだけを補間する
	if( message == WM_MOUSEWHEEL && (LOWORD(wparam) & (MK_CONTROL | MK_SHIFT)) == 0) {
		UINT lines = 3 ; // OS照会が失敗した場合の有限なwheel行数
		SystemParametersInfoW( SPI_GETWHEELSCROLLLINES, 0, &lines, 0) ;
		RECT area{} ; // 一画面移動の実client高さ
		GetClientRect( window, &area) ;
		// OSが選んだ行数または一画面を、wheel一目盛りのpixel距離へ換算する
		const double step =
			lines == WHEEL_PAGESCROLL ? std::max( 1L, area.bottom - 20) : std::min( lines, 100U) * lineHeight( window) ;
		const int delta = GET_WHEEL_DELTA_WPARAM(wparam) ; // 高解像度wheelの部分notchも保持
		startScroll( window, state, -static_cast<double>( delta) * step / WHEEL_DELTA) ;
		return 0 ;
	}
	// 行・page buttonは補間し、thumbや端の指定は標準の直接位置操作を維持する
	if( message == WM_VSCROLL && LOWORD(wparam) <= SB_PAGEDOWN) {
		RECT area{} ; // 一画面分の移動距離を決める実client高さ
		GetClientRect( window, &area) ;
		const UINT command = LOWORD(wparam) ; // 上下と行・pageの区別
		// 行移動またはpage移動に対応するclient内のpixel距離
		const int  distance =
			 command <= SB_LINEDOWN ? lineHeight( window) : std::max( 1L, area.bottom - lineHeight( window)) ;
		startScroll( window, state, (command == SB_LINEUP || command == SB_PAGEUP) ? -distance : distance) ;
		return 0 ;
	}
	// 単純なPage Up/Downだけを補間し、選択範囲を変更する修飾key操作は維持する
	if( message == WM_KEYDOWN && (wparam == VK_PRIOR || wparam == VK_NEXT) && GetKeyState( VK_SHIFT) >= 0 &&
	   GetKeyState( VK_CONTROL) >= 0) {
		RECT area{} ; // 一画面から一行残して読み進める距離
		GetClientRect( window, &area) ;
		const int distance = std::max( 1L, area.bottom - lineHeight( window)) ; // page移動pixel数
		startScroll( window, state, wparam == VK_PRIOR ? -distance : distance) ;
		return 0 ;
	}
	// 本文差替え、直接操作、resizeで古い文書の移動先を引き継がない
	if( message == WM_VSCROLL || message == WM_MOUSEWHEEL || message == WM_LBUTTONDOWN || message == WM_KEYDOWN ||
	   message == WM_SIZE || message == WM_SETTEXT || message == EM_STREAMIN ||
	   (message == EM_SETSCROLLPOS && !state.applying) || (message == WM_NCLBUTTONDOWN && wparam == HTVSCROLL) ||
	   (message == WM_SYSCOMMAND && (wparam & 0xFFF0U) == SC_VSCROLL) || message == WM_NCDESTROY) {
		stopScroll( window, state) ;
	}
	// 破棄後へ借用状態とtimerを残さない
	if( message == WM_NCDESTROY) {
		RemoveWindowSubclass( window, scrollProc, id) ;
	}
	return DefSubclassProc( window, message, wparam, lparam) ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 文書差替え前に残った補間先とtimerを捨てます
 * @param[in] control 自RichEdit
 * @param[in,out] state 同じcontrolの状態
 * @see koefude::cancelSmoothScroll
 */
//------------------------------------------------------------------------------
void cancelSmoothScroll( HWND control, SmoothScroll &state) noexcept
{
	stopScroll( control, state) ;
}
#if defined(KOEFUDE_WINDOW_TESTING)
//------------------------------------------------------------------------------
/**
 * @brief 実入力注入なしで製品の補間開始境界を確認します
 * @param[in] control 自所有control
 * @param[in,out] state 製品状態
 * @param[in] delta 有限pixel移動量
 * @see koefude::testStartSmoothScroll
 */
//------------------------------------------------------------------------------
void testStartSmoothScroll( HWND control, SmoothScroll &state, double delta) noexcept
{
	startScroll( control, state, delta) ;
}
#endif
//------------------------------------------------------------------------------
/**
 * @brief wheel・行・page移動を補間するsubclassを登録します
 * @param[in] control UI thread所有のRich Edit HWND
 * @param[in,out] state HWND破棄まで有効な所有状態。別controlと共有しません
 * @return 登録できればtrue。失敗時は標準scrollのままです
 * @see koefude::attachSmoothScroll
 */
//------------------------------------------------------------------------------
bool attachSmoothScroll( const HWND control, SmoothScroll &state) noexcept
{
	return SetWindowSubclass( control, scrollProc, 2, reinterpret_cast<DWORD_PTR>( &state)) != FALSE ;
}

} // namespace koefude
