//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file message_dialog.cpp
 * @brief 自所有の標準controlへMS ゴシックを適用するmodal案内を実装します
 * @version 1.0.0
 * @date 2026/11/10
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 1.0.0 Shunji Ogawa 2026/11/10 新規作成
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1382の有限button・DPI配置・font所有を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1414の実計測診断とwork area内の複数行button配置を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1422で整形中に混入した配置変更を整形前へ復元
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/message_dialog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "app/localization.h"
#include "app/resource.h"

namespace koefude {
namespace {
constexpr int K_BODY = 100 ; // このdialogだけの本文control
constexpr int K_ICON = 101 ; // 共有標準iconを借用するcontrol

/** @brief 一同期modalの借用文面と所有GDIをまとめます */
struct MessageDialogState {
	/// 同期呼出し元が所有する本文
	const wchar_t				  *text = nullptr ;
	/// 同期呼出し元が所有する題名
	const wchar_t				  *title = nullptr ;
	/// 同期呼出し元が所有するbutton文面
	std::array<const wchar_t *, 3> labels{} ;
	/// 許可する標準返値だけ
	std::array<int, 3>			   ids{} ;
	/// 最大三button
	std::size_t					   count = 0 ;
	/// Enterの既定返値
	int							   defaultId = IDOK ;
	/// 0はEscape/close不可
	int							   cancelId = IDOK ;
	/// LoadIconの共有資産ID。なしはNULL
	LPCWSTR						   icon = nullptr ;
	/// 自所有dialogだけ。破棄後NULL
	HWND						   window = nullptr ;
	/// 全control破棄後に解放する所有font
	HFONT						   font = nullptr ;
	/// 失敗は0のまま
	int							   result = 0 ;
	/// callback内の失敗を外へ戻す値
	DWORD						   error = ERROR_SUCCESS ;
	/// 内部試験だけが指定する非表示mode
	bool						   hidden = false ;

	//------------------------------------------------------------------------------
	/**
	 * @brief control破棄を先に済ませ、最後の所有fontを解放します
	 */
	//------------------------------------------------------------------------------
	~MessageDialogState() noexcept
	{
		// 作成後の失敗やWM_QUITでも自所有windowを残しません
		if( window) {
			DestroyWindow( window) ;
		}
		// controlはfontを所有しないため、windowより後に解放します
		if( font) {
			DeleteObject( font) ;
		}
	}
} ;

//------------------------------------------------------------------------------
/**
 * @brief 同期modalの返値を確定し、自所有dialogを破棄します
 * @param[in,out] state 同じUI threadが所有する状態。windowはNULL可で、破棄時はcallbackがNULLへ戻します
 * @param[in] result 呼出し側が許可した標準button ID、または失敗を表す0
 * @note fontはここで解放せず、全control破棄後のstate寿命終端で解放します
 */
//------------------------------------------------------------------------------
void finishDialog( MessageDialogState &state, const int result) noexcept
{
	state.result = result ;
	// modal loopが破棄済みHWNDへ送信しないようWM_NCDESTROYでNULLにします
	if( state.window) {
		DestroyWindow( state.window) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 11pt MS ゴシックで本文を計測し、全buttonを一行へ右寄せして自dialogを配置します
 * @param[in,out] state 同じUI threadが所有する生成済みdialogとcontrol。本文/labelは同期中有効なUTF-16を借用します
 * @param[in] dpi 配置用DPI。96～768へ制限してfontと余白のpixel寸法を計算します
 * @param[in] suggested 同期中だけ借用するDPI変更時の推奨外矩形（pixel）。NULLはownerまたはwork areaの中央です
 * @return font確保・本文/font計測・一行幅の検証を通過し配置を要求したらtrue。事前拒否はfalseです
 * @note falseでは未使用fontを解放し、既存font/control配置を保持します。trueでは全control差替え後に旧fontを解放します
 * @note Win32配置APIと非client寸法計測の返却値は集約しません。高さは最小本文三行を確保する従来算式です
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool layoutDialog( MessageDialogState &state, UINT dpi, const RECT *suggested = nullptr) noexcept
{
	dpi = std::clamp( dpi, 96U, 768U) ; // 未初期化値をfont計算へ流しません
	const int	density = static_cast<int>( dpi) ; // MulDivの有界DPI
	const HFONT next =
		CreateFontW( -MulDiv( 11, density, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SHIFTJIS_CHARSET,
					OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"ＭＳ ゴシック") ;
	// GDI確保失敗時は差替えを開始しません
	if( !next) {
		return false ;
	}
	const HWND window = state.window ; // 自同期dialogだけ
	const HDC  dc = GetDC( window) ; // 計測だけに使い、pixelを取得しません
	// DCを取得できなければ未使用fontを解放します
	if( !dc) {
		DeleteObject( next) ;
		return false ;
	}
	const auto	oldObject = SelectObject( dc, next) ; // 計測中だけfontを借用
	MONITORINFO monitor{sizeof(monitor)} ; // ownerが属するwork area
	// monitor情報が不明なら不正なwork areaで続行しません
	if( !GetMonitorInfoW( MonitorFromWindow( window, MONITOR_DEFAULTTONEAREST), &monitor)) {
		SelectObject( dc, oldObject) ;
		ReleaseDC( window, dc) ;
		DeleteObject( next) ;
		return false ;
	}
	const int margin = MulDiv( 20, density, 96) ; // 本文と外縁の間隔
	const int gap = MulDiv( 10, density, 96) ; // button間の間隔
	const int iconWidth = state.icon ? MulDiv( 32, density, 96) + margin : 0 ;
	const int workWidth =
		std::max<int>( MulDiv( 280, density, 96), monitor.rcWork.right - monitor.rcWork.left - margin * 2) ;
	const int bodyWidth = std::max<int>( 1, std::min<int>( MulDiv( 520, density, 96), workWidth - margin * 2 - iconWidth)) ;
	RECT	  body{0, 0, bodyWidth, 0} ; // 改行を保ち、長いpathも幅内へ折り返します
	const int measured =
		DrawTextW( dc, state.text, -1, &body, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX) ;
	TEXTMETRICW metrics{} ; // fontに合うbutton高さの下限
	// 空本文以外の計測失敗とfont計測失敗は不完全配置にしません
	if( (!measured && *state.text) || !GetTextMetricsW( dc, &metrics)) {
		SelectObject( dc, oldObject) ;
		ReleaseDC( window, dc) ;
		DeleteObject( next) ;
		return false ;
	}
	int buttonWidth = MulDiv( 88, density, 96) ; // 標準応答の最小幅
	// 全buttonが最長labelを収める幅となるまで計測します
	for( std::size_t index = 0; index < state.count; ++index) {
		RECT label{} ; // mnemonicを除いた表示幅
		DrawTextW( dc, state.labels[index], -1, &label, DT_CALCRECT | DT_SINGLELINE) ;
		buttonWidth = std::max<int>( buttonWidth, label.right + margin * 2) ;
	}
	SelectObject( dc, oldObject) ; // DCがfontを参照しない状態へ戻します
	ReleaseDC( window, dc) ;
	const int buttonHeight = std::max<int>( MulDiv( 32, density, 96), metrics.tmHeight + gap * 2) ;
	const int buttonsWidth = static_cast<int>( state.count) * buttonWidth + static_cast<int>( state.count - 1) * gap ;
	const int width = std::max<int>( bodyWidth + iconWidth, buttonsWidth) + margin * 2 ;
	// 未使用の巨大button文面をwork area外へ置いて成功にしません
	if( width > workWidth) {
		DeleteObject( next) ;
		return false ;
	}
	RECT frame{0, 0, width, 0} ; // 非client高さを先に把握します
	AdjustWindowRectExForDpi( &frame, static_cast<DWORD>( GetWindowLongPtrW( window, GWL_STYLE)), FALSE,
							 WS_EX_DLGMODALFRAME, dpi) ;
	const int availableHeight =
		std::max<int>( metrics.tmHeight * 3,
					  monitor.rcWork.bottom - monitor.rcWork.top - margin * 5 - buttonHeight -
						  (frame.bottom - frame.top)) ; // 長い詳細はreadonly editで縦scrollします
	const int  bodyHeight = std::min<int>( std::max<int>( body.bottom, MulDiv( 32, density, 96)), availableHeight) ;
	const HWND bodyControl = GetDlgItem( window, K_BODY) ; // 自所有readonly本文
	SendMessageW( bodyControl, WM_SETFONT, reinterpret_cast<WPARAM>( next), FALSE) ;
	MoveWindow( bodyControl, margin + iconWidth, margin, bodyWidth, bodyHeight, FALSE) ;
	ShowScrollBar( bodyControl, SB_VERT, body.bottom > bodyHeight) ;
	// iconは共有handleを借用し、font所有の対象へ含めません
	if( state.icon) {
		MoveWindow( GetDlgItem( window, K_ICON), margin, margin, iconWidth - margin, MulDiv( 32, density, 96), FALSE) ;
	}
	// 同じfontを全buttonへ適用し、標準dialog managerのTab順を維持します
	for( std::size_t index = 0; index < state.count; ++index) {
		const HWND button = GetDlgItem( window, state.ids[index]) ; // 同じdialogのIDだけ
		SendMessageW( button, WM_SETFONT, reinterpret_cast<WPARAM>( next), FALSE) ;
		MoveWindow( button, width - margin - buttonsWidth + static_cast<int>( index) * (buttonWidth + gap),
				   bodyHeight + margin * 2, buttonWidth, buttonHeight, FALSE) ;
	}
	SendMessageW( window, WM_SETFONT, reinterpret_cast<WPARAM>( next), FALSE) ;
	const HFONT previous = state.font ; // 全参照差替え後だけ旧fontを削除します
	state.font = next ;
	// 初回は旧fontがなく、DPI再配置だけが旧所有fontを持ちます
	if( previous) {
		DeleteObject( previous) ;
	}
	frame = {0, 0, width, bodyHeight + margin * 3 + buttonHeight} ;
	AdjustWindowRectExForDpi( &frame, static_cast<DWORD>( GetWindowLongPtrW( window, GWL_STYLE)), FALSE,
							 WS_EX_DLGMODALFRAME, dpi) ;
	RECT	   anchor = monitor.rcWork ; // ownerなしの起動失敗はwork area中央
	const HWND owner = GetWindow( window, GW_OWNER) ; // 自呼出しのownerだけ
	// ownerがあれば同じownerの矩形中央を使います
	if( owner) {
		GetWindowRect( owner, &anchor) ;
	}
	const int outerWidth = frame.right - frame.left ; // 非client込み幅
	const int outerHeight = frame.bottom - frame.top ; // 非client込み高さ
	const int x = suggested ? suggested->left : anchor.left + (anchor.right - anchor.left - outerWidth) / 2 ;
	const int y = suggested ? suggested->top : anchor.top + (anchor.bottom - anchor.top - outerHeight) / 2 ;
	SetWindowPos(
		window, nullptr,
		std::clamp<int>( x, monitor.rcWork.left, std::max<int>( monitor.rcWork.left, monitor.rcWork.right - outerWidth)),
		std::clamp<int>( y, monitor.rcWork.top, std::max<int>( monitor.rcWork.top, monitor.rcWork.bottom - outerHeight)),
		outerWidth, outerHeight, SWP_NOZORDER | SWP_NOACTIVATE) ;
	InvalidateRect( window, nullptr, TRUE) ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief 生成済みdialogへ本文・任意icon・最大三buttonを作り、初回の一行配置を要求します
 * @param[in,out] state 同じUI threadの自所有windowと設定済みID/借用文面。文面はmodal終了まで有効でNULL不可です
 * @return 全control生成と初回の配置検証を通ればtrue。生成/計測/幅検証の失敗はfalseです
 * @note falseでも部分controlを持ち得るため、呼出し側が同じdialog全体を破棄します。共有iconは解放しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool createDialogControls( MessageDialogState &state) noexcept
{
	const auto instance = GetModuleHandleW( nullptr) ; // このappだけのmodule
	const HWND window = state.window ; // callbackが確定した自dialog
	const auto bodyId = reinterpret_cast<HMENU>( static_cast<INT_PTR>( K_BODY)) ; // Win32 child ID境界
	// readonly editは長い詳細のkeyboard/縦scrollと全文保持を両立します
	if( !CreateWindowExW( 0, L"EDIT", state.text,
						WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
						0, 0, 1, 1, window, bodyId, instance, nullptr)) {
		return false ;
	}
	// 指定iconだけを共有資産から読み、自所有STATICへ設定します
	if( state.icon) {
		const HWND icon = CreateWindowExW( 0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON, 0, 0, 1, 1, window,
										  reinterpret_cast<HMENU>( static_cast<INT_PTR>( K_ICON)), instance, nullptr) ;
		// icon用control生成失敗も案内全体の生成失敗とします
		if( !icon) {
			return false ;
		}
		SendMessageW( icon, STM_SETICON, reinterpret_cast<WPARAM>( LoadIconW( nullptr, state.icon)), 0) ;
	}
	// 有限のbuttonだけをID順に作り、標準dialog keyboard処理へ渡します
	for( std::size_t index = 0; index < state.count; ++index) {
		const DWORD style = state.ids[index] == state.defaultId ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON ;
		// button生成に失敗した場合は残りを作らず自dialog全体を破棄します
		if( !CreateWindowExW( 0, L"BUTTON", state.labels[index], WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 1, 1,
							window, reinterpret_cast<HMENU>( static_cast<INT_PTR>( state.ids[index])), instance,
							nullptr)) {
			return false ;
		}
	}
	SendMessageW( window, DM_SETDEFID, static_cast<WPARAM>( state.defaultId), 0) ;
	// Yes/Noだけの標準確認はclose/Escapeから返しません
	if( !state.cancelId) {
		EnableMenuItem( GetSystemMenu( window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED) ;
	}
	return layoutDialog( state, GetDpiForWindow( window)) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 自dialogの初期生成・標準応答・DPI更新・破棄を同じUI threadで処理します
 * @param[in] window Win32が同期呼出し中に貸す自dialogのHWND
 * @param[in] message 処理対象のWin32 message ID。未知messageは標準dialog処理へ委ねます
 * @param[in] word message固有値。WM_COMMANDはbutton ID/通知、WM_DPICHANGEDは新DPIです
 * @param[in] value WM_INITDIALOGはmodal終了まで有効なstateの借用pointer、WM_DPICHANGEDは同期借用RECT（pixel）です
 * @return 処理済みならTRUE、標準処理へ委ねるならFALSE。WM_CTLCOLORSTATICだけは借用system brushを返します
 * @note 生成/再配置失敗ではstate.errorを保持して失敗0で閉じます。例外を発生する処理を呼びません
 */
//------------------------------------------------------------------------------
INT_PTR CALLBACK messageDialogProc( HWND window, UINT message, WPARAM word, LPARAM value) noexcept
{
	auto *state = reinterpret_cast<MessageDialogState *>( GetWindowLongPtrW( window, DWLP_USER)) ;
	// 初期化時だけ自同期stateをWindowへ借用させます
	if( message == WM_INITDIALOG) {
		state = reinterpret_cast<MessageDialogState *>( value) ;
		state->window = window ;
		SetWindowLongPtrW( window, DWLP_USER, value) ;
		SetWindowTextW( window, state->title) ;
		// 部分生成を成功とせず、modal loopへ診断値を戻します
		if( !createDialogControls( *state)) {
			state->error = GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY ;
			finishDialog( *state, 0) ;
		}
		return FALSE ; // hidden試験ではfocusを操作しません
	}
	// 初期化前と破棄後は標準処理だけに委ねます
	if( !state) {
		return FALSE ;
	}
	// 空templateのdialog自身も現在の所有fontを明示して照合可能にします
	if( message == WM_GETFONT) {
		SetWindowLongPtrW( window, DWLP_MSGRESULT, reinterpret_cast<LONG_PTR>( state->font)) ;
		return TRUE ;
	}
	// DPI変更は同じowner/font契約で全controlを再配置します
	if( message == WM_DPICHANGED) {
		// 失敗時は同意を返さず、この案内だけを終了します
		if( !layoutDialog( *state, HIWORD(word), reinterpret_cast<const RECT *>( value))) {
			state->error = ERROR_NOT_ENOUGH_MEMORY ;
			finishDialog( *state, 0) ;
		}
		return TRUE ;
	}
	// Enter/Tab/mnemonicはIsDialogMessageの標準button処理から届きます
	if( message == WM_COMMAND && HIWORD(word) == BN_CLICKED) {
		const int id = LOWORD(word) ; // 標準dialog返値の範囲
		// EscapeのIDCANCELは許可済みの辞退返値へだけ写します
		if( id == IDCANCEL) {
			// Yes/Noのみの確認ではEscapeを無視します
			if( state->cancelId) {
				finishDialog( *state, state->cancelId) ;
			}
			return TRUE ;
		}
		// 未定義buttonや別dialogのIDを返値として受け入れません
		for( std::size_t index = 0; index < state->count; ++index) {
			// この案内で作ったbuttonだけが終端を選択できます
			if( state->ids[index] == id) {
				finishDialog( *state, id) ;
				return TRUE ;
			}
		}
	}
	// 閉じるは同意へ変換せず、許可された辞退だけにします
	if( message == WM_CLOSE) {
		// Yes/Noのみの確認は標準MessageBox同様にclose不可です
		if( state->cancelId) {
			finishDialog( *state, state->cancelId) ;
		}
		return TRUE ;
	}
	// readonly本文は標準dialogと同じ背景で、選択copyも可能にします
	if( message == WM_CTLCOLORSTATIC) {
		const auto dc = reinterpret_cast<HDC>( word) ; // 同期paint中だけ借用
		SetTextColor( dc, GetSysColor( COLOR_WINDOWTEXT)) ;
		SetBkColor( dc, GetSysColor( COLOR_3DFACE)) ;
		return reinterpret_cast<INT_PTR>( GetSysColorBrush( COLOR_3DFACE)) ;
	}
	// stateより先にcontrolが消えるので、最後のfontは呼出し側で解放できます
	if( message == WM_NCDESTROY) {
		state->window = nullptr ;
		SetWindowLongPtrW( window, DWLP_USER, 0) ;
	}
	return FALSE ;
}

/** @brief fontを後から適用する空の自所有dialog templateです */
struct alignas( DWORD) MessageDialogTemplate {
	/// controlは同じcallbackで作成
	DLGTEMPLATE definition{WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, WS_EX_DLGMODALFRAME, 0, 0, 0, 1, 1} ;
	/// menuなし
	WORD		menu = 0 ;
	/// 標準dialog class
	WORD		windowClass = 0 ;
	/// 題名は同期stateから設定
	WORD		title = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 空templateから自dialogと全controlを生成し、標準managerの既定buttonを確定します
 * @param[in,out] state 同じUI threadの未生成状態。文面はmodal終了まで有効で、生成HWND/fontを所有します
 * @param[in] owner 同threadの非所有親HWND。親なしの案内ではNULL可です
 * @return callbackの全control生成・初回配置まで通ればtrue。失敗はfalseでerrorをstateへ残し得ます
 * @note templateは同期生成中だけ貸し、部分windowはcallbackまたはstateの破棄で回収します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool createMessageDialog( MessageDialogState &state, HWND owner) noexcept
{
	const MessageDialogTemplate dialog ; // 呼出し中だけOSがtemplateを読みます
	// 作成終端で標準dialog managerが変更する初期styleより後に既定を確定します
	if( !CreateDialogIndirectParamW( GetModuleHandleW( nullptr), &dialog.definition, owner, messageDialogProc,
								   reinterpret_cast<LPARAM>( &state)) ||
	   !state.window) {
		return false ;
	}
	SendMessageW( state.window, DM_SETDEFID, static_cast<WPARAM>( state.defaultId), 0) ;
	SendMessageW( GetDlgItem( state.window, state.defaultId), BM_SETSTYLE, BS_DEFPUSHBUTTON, FALSE) ;
	return true ;
}

//------------------------------------------------------------------------------
/**
 * @brief ownerの元enabled状態を保持し、同じUI threadのmessage loopで同期modalを実行します
 * @param[in,out] state 未生成の自所有状態。借用文面は全loop中有効で、終端時はwindowがNULLになります
 * @param[in] owner 同じprocess/UI threadの非所有親HWND。NULL可で、不正な所属は生成前に拒否します
 * @param[in] initialized hidden試験の生成後同期callback。stateを呼出し中だけ借用し、NULLなら観測しません
 * @return 許可されたbutton返値。生成/queue失敗またはWM_QUITは0でGetLastErrorへ診断値を返します
 * @note 自分が無効化したownerだけを復帰し、消費したWM_QUITは外側へ再配送します。hiddenも同じ生成/loop/cleanupです
 */
//------------------------------------------------------------------------------
int runMessageDialog( MessageDialogState &state, HWND owner,
					 void (*initialized)( MessageDialogState &) = nullptr) noexcept
{
	DWORD process = 0 ; // ownerを他thread/processから借りません
	// ownerが指定されている時だけ所属threadを確認します
	if( owner &&
	   (GetWindowThreadProcessId( owner, &process) != GetCurrentThreadId() || process != GetCurrentProcessId())) {
		SetLastError( ERROR_INVALID_WINDOW_HANDLE) ;
		return 0 ;
	}
	// 生成失敗はowner状態を変える前に戻します
	if( !createMessageDialog( state, owner)) {
		SetLastError( state.error ? state.error : ERROR_NOT_ENOUGH_MEMORY) ;
		return 0 ;
	}
	const bool restoreOwner = owner && IsWindowEnabled( owner) ; // 元から無効なら復帰しません
	// 自ownerだけを無効化し、nested dialogも元状態を保持します
	if( restoreOwner) {
		EnableWindow( owner, FALSE) ;
	}
	// hidden試験は画面表示・activation・focusを一切行いません
	if( !state.hidden) {
		ShowWindow( state.window, SW_SHOW) ;
		SendMessageW( state.window, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>( GetDlgItem( state.window, state.defaultId)),
					 TRUE) ;
	}
	// 内部試験は生成済み実controlを観測し、自dialog専用messageだけを予約します
	if( initialized) {
		initialized( state) ;
	}
	int	 quitCode = 0 ; // 自modalがWM_QUITを消費した場合の再配送値
	bool quit = false ; // 後続の外側loopへ終了を引き渡します
	// 自dialogの終端まで標準dialog managerで同threadのmessageを回収します
	while( state.window) {
		MSG		   message{} ; // このthreadだけのmessage
		const BOOL received = GetMessageW( &message, nullptr, 0, 0) ; // owner modal中の標準loop
		// 外側の終了やqueue失敗で同意を作らずに破棄します
		if( received <= 0) {
			quit = received == 0 ;
			quitCode = static_cast<int>( message.wParam) ;
			state.error = received < 0 ? GetLastError() : ERROR_CANCELLED ;
			finishDialog( state, 0) ;
			break ;
		}
		// dialog専用のTab/Enter/Escape処理以外を通常dispatchします
		if( !IsDialogMessageW( state.window, &message)) {
			TranslateMessage( &message) ;
			DispatchMessageW( &message) ;
		}
	}
	// 自分が無効化した同じownerだけを元のenabled状態へ戻します
	if( restoreOwner && IsWindow( owner)) {
		EnableWindow( owner, TRUE) ;
	}
	// WM_QUITは外側のapp loopへ戻し、同意と解釈しません
	if( quit) {
		PostQuitMessage( quitCode) ;
	}
	SetLastError( state.error) ;
	return state.result ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有限のMessageBox flagsを最大三buttonの標準返値・既定/取消し・共有iconへ変換します
 * @param[in,out] state 未生成の自所有状態。失敗時も借用本文や一部設定を保持し得ますが、windowは生成しません
 * @param[in] text modal終了まで有効なNULL終端UTF-16本文。空可、NULLは拒否します
 * @param[in] title modal終了まで有効なNULL終端UTF-16題名。空可、NULLは拒否します
 * @param[in] flags MB_OK/MB_YESNO/MB_YESNOCANCELの種別、既定番号と標準icon。未対応種別/範囲外番号は拒否します
 * @param[in] labels OK/Yes/No/Cancel順の四文面。選ばれる文面はNULL不可で、modal終了まで借用元が保持します
 * @return 対応種別と有効引数ならtrue。拒否はfalseでGetLastErrorをERROR_INVALID_PARAMETERへ設定します
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool configureMessageDialog( MessageDialogState &state, const wchar_t *text, const wchar_t *title,
										  UINT flags, const std::array<const wchar_t *, 4> &labels) noexcept
{
	state.text = text ;
	state.title = title ;
	// 現アプリが使う有限button種別だけを解釈します
	switch( flags & MB_TYPEMASK) {
		case MB_OK: // 通常案内はEscapeでもOKを返します
			state.labels = {labels[0], nullptr, nullptr} ;
			state.ids = {IDOK, 0, 0} ;
			state.count = 1 ;
			state.cancelId = IDOK ;
			break ;
		case MB_YESNO: // 破壊/送信確認をEscapeで勝手に選択しません
			state.labels = {labels[1], labels[2], nullptr} ;
			state.ids = {IDYES, IDNO, 0} ;
			state.count = 2 ;
			state.cancelId = 0 ;
			break ;
		case MB_YESNOCANCEL: // 終了確認はCancelへ戻せます
			state.labels = {labels[1], labels[2], labels[3]} ;
			state.ids = {IDYES, IDNO, IDCANCEL} ;
			state.count = 3 ;
			state.cancelId = IDCANCEL ;
			break ;
		default: // 未使用のMessageBox機能を推測で実装しません
			SetLastError( ERROR_INVALID_PARAMETER) ;
			return false ;
	}
	const UINT defaultIndex = (flags & MB_DEFMASK) >> 8 ; // 標準の0起点button番号
	// 不正引数とNULL文面はOSへ渡しません
	if( !text || !title || defaultIndex >= state.count) {
		SetLastError( ERROR_INVALID_PARAMETER) ;
		return false ;
	}
	state.defaultId = state.ids[defaultIndex] ;
	// 既存呼出しの四標準iconだけを同じ共有資産へ対応させます
	switch( flags & MB_ICONMASK) {
		case MB_ICONERROR:
			state.icon = IDI_ERROR ;
			break ; // エラー
		case MB_ICONQUESTION:
			state.icon = IDI_QUESTION ;
			break ; // 明示確認
		case MB_ICONWARNING:
			state.icon = IDI_WARNING ;
			break ; // 警告
		case MB_ICONINFORMATION:
			state.icon = IDI_INFORMATION ;
			break ; // 情報
		default:
			break ; // iconなし
	}
	return true ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief resourceの標準button文面を同期保持し、共通のowner modal案内を表示します
 * @param[in] owner 同じUI threadの非所有親HWND。親なしの案内ではNULL可です
 * @param[in] text 同期modal終了まで有効なNULL終端UTF-16本文。空可、NULL不可です
 * @param[in] title 同期modal終了まで有効なNULL終端UTF-16題名。空可、NULL不可です
 * @param[in] flags 共通設定が対応するbutton種別・既定番号・標準icon
 * @return 標準button ID。生成/引数/資源確保失敗は0でGetLastErrorへ診断値を返します
 * @note 同じUI threadで同期実行し、設定・通信・保存を行いません
 * @see koefude::showMessageDialog
 */
//------------------------------------------------------------------------------
int showMessageDialog( HWND owner, const wchar_t *text, const wchar_t *title, UINT flags) noexcept
{
	try {
		const auto		   ok = resource( IDS_MESSAGE_OK) ; // 同期modal中に所有する翻訳button
		const auto		   yes = resource( IDS_MESSAGE_YES) ; // 既存標準応答
		const auto		   no = resource( IDS_MESSAGE_NO) ; // 既存標準応答
		const auto		   cancel = resource( IDS_MESSAGE_CANCEL) ; // 既存標準応答
		MessageDialogState state ; // fontとwindowはこの呼出しだけが所有
		// 同じ有限設定処理をhidden実controlの試験からも使用します
		if( !configureMessageDialog( state, text, title, flags, {ok.c_str(), yes.c_str(), no.c_str(), cancel.c_str()})) {
			return 0 ;
		}
		return runMessageDialog( state, owner) ;
	} catch( ...) {
		// allocation失敗でもnoexcept境界を越えず、同意を作りません
		SetLastError( ERROR_NOT_ENOUGH_MEMORY) ;
		return 0 ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 明示同意と辞退の二buttonを生成し、既定をIDNOへ固定した同期案内を表示します
 * @param[in] owner 同じUI threadの非所有親HWND。NULL不可です
 * @param[in] text modal終了まで有効なNULL終端UTF-16本文。空可、NULL不可です
 * @param[in] title modal終了まで有効なNULL終端UTF-16題名。空可、NULL不可です
 * @param[in] improve modal終了まで借用する同意button文面。空可、NULL不可です
 * @param[in] later modal終了まで借用する辞退button文面。空可、NULL不可です
 * @return 同意IDYES/辞退IDNO、close/EscapeはIDCANCEL。引数/生成失敗は0とWin32診断値です
 * @note font/windowはこの同期stateが所有し、借用文面やownerを解放しません
 * @see koefude::showImproveDialog
 */
//------------------------------------------------------------------------------
int showImproveDialog( HWND owner, const wchar_t *text, const wchar_t *title, const wchar_t *improve,
					  const wchar_t *later) noexcept
{
	// NULL借用値からdialogを作成しません
	if( !owner || !text || !title || !improve || !later) {
		SetLastError( ERROR_INVALID_PARAMETER) ;
		return 0 ;
	}
	MessageDialogState state ; // 同期呼出し内だけの所有GDIと借用文字列
	state.text = text ;
	state.title = title ;
	state.labels = {improve, later, nullptr} ;
	state.ids = {IDYES, IDNO, 0} ;
	state.count = 2 ;
	state.defaultId = IDNO ;
	state.cancelId = IDCANCEL ;
	return runMessageDialog( state, owner) ;
}
} // namespace koefude
