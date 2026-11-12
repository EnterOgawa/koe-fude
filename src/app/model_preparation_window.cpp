//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation_window.cpp
 * @brief 単一registryからモデル準備の説明と実測進捗を表示します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 MS ゴシックの準備画面と値だけの操作通知を追加
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380のopen/close通知gateを追加
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
#include "app/model_preparation_window.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <commctrl.h>

#include "app/localization.h"
#include "app/resource.h"
#include "inference/model_store.h"
#include "platform/win_helpers.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 正確なbyte数と読取りやすいMiBを併記します
 * @param[in] bytes fileまたは転送のbyte数。VRAMではありません
 * @return 所有表示文字列
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring capacity( const std::uint64_t bytes)
{
	return std::to_wstring( bytes / (1024 * 1024)) + L" MiB (" + std::to_wstring( bytes) + L" byte)" ;
}

//------------------------------------------------------------------------------
/**
 * @brief registryの共有用途とASR依存から用途名を選びます
 * @param[in] id registryの固定用途
 * @return 現在言語の用途名
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring roleName( const ModelId id)
{
	const auto &shared = sharedModelRoles() ; // モデル一覧をUIへ複製しない参照
	// 共通VADだけを発話区間検出の用途として説明する
	if( id == shared.voiceActivity) {
		return resource( IDS_PREP_ROLE_VAD) ;
	}
	// 話者の二つの依存fileを同じ話者推定用途として示す
	if( id == shared.segmentation || id == shared.embedding) {
		return resource( IDS_DIARIZE) ;
	}
	return resource( IDS_PREP_ROLE_ASR) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 表示する構成の用途と条件を一つの所有文字列へまとめます
 * @param[in] options UIで現在選択した構成
 * @param[in] plan 検査済み計画。NULLなら検査待ち
 * @param[in] speakerText 明示した補助参照モデル準備
 * @return 選択・copy可能な説明。外部の本文・音声は含みません
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring describe( const EngineOptions &options, const ModelPreparationPlan *plan,
									const bool speakerText)
{
	const auto &asr = asrDefinition( options.asrModel, options.asrProfile) ; // 単一構成参照
	// textは条件欄へ所有copyする現在構成の説明
	auto		text = resource( asr.familyResource) + L" / " + resource( asr.choiceResource) + L"\r\n" ;
	text += resource( IDS_PREP_INTRO) + L"\r\n\r\n" ;
	// API送信前または明示補助準備では追加PC内用途を容量一覧と同じ画面へ示す
	if( speakerText || (options.asrModel == AsrModel::OPEN_AI_AUDIO && options.asrProfile == AsrProfile::QUALITY &&
					   !options.autoLanguage && options.diarization)) {
		text += resource( IDS_SPEAKER_TEXT_NOTICE) + L"\r\n\r\n" ;
	}
	// 計画が確定した場合だけ、検査した保存先と不足容量を表示する
	if( plan) {
		text += resource( IDS_PREP_STORAGE) + L" " + plan->effectiveRoot.native() + L"\r\n" ;
		text += resource( IDS_PREP_CAPACITY) + L" " + capacity( plan->requiredBytes) + L"\r\n" ;
		text += resource( IDS_PREP_TRANSFER) + L" " + capacity( plan->downloadBytes) + L"\r\n" ;
		text += resource( IDS_PREP_DISK) + L" " + capacity( modelPreparationDiskBytes( *plan)) + L"\r\n\r\n" ;
	}
	text += resource( IDS_PREP_HOSTS) + L"\r\n" ;
	// 通信実装と同じ許可先を示し、redirect先の表示漏れを防ぐ
	for( const auto host : modelDownloadHosts()) { // process寿命中有効なhost
		text += std::wstring( host) + L"\r\n" ;
	}
	text += L"\r\n" + resource( IDS_PREP_TERMS) + L"\r\n" ;
	// 現在構成の依存だけを同じregistryから列挙する
	for( const auto id : requiredModels( options)) { // 重複のない固定用途
		const auto &file = modelDefinition( id) ; // 最終fileの固定条件
		const auto &download = modelDownloadDefinition( id) ; // 配布条件と元資産
		text += L"\r\n" + roleName( id) + L" · " + std::wstring( file.file) + L"\r\n" ;
		text += capacity( file.bytes) + L" · " + toWide( download.license) + L"\r\n" ;
		text += resource( IDS_PREP_SOURCE) + L" " + toWide( download.source) + L"\r\n" ;
		text += L"revision: " + toWide( download.revision) + L"\r\n" ;
	}
	return text ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 同じUI threadでWindowとfontを解放します
 * @see koefude::ModelPreparationWindow::~ModelPreparationWindow
 */
//------------------------------------------------------------------------------
ModelPreparationWindow::~ModelPreparationWindow()
{
	// 親の破棄で既に消えた場合は同じHWNDを再破棄しない
	if( IsWindow( window_)) {
		DestroyWindow( window_) ;
	}
	// fontを参照するcontrolが消えてから所有GDI resourceを解放する
	if( font_) {
		DeleteObject( font_) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief keyboard移動先を返します
 * @return 非所有HWND。未作成ならNULL
 * @see koefude::ModelPreparationWindow::handle
 */
//------------------------------------------------------------------------------
HWND ModelPreparationWindow::handle() const noexcept
{
	return window_ ;
}

//------------------------------------------------------------------------------
/**
 * @brief 標準Windowを必要時に作成して表示します
 * @param[in] owner このinstanceより長く有効な親Window。非NULL
 * @throws OperationError Windowまたはcontrolの作成失敗
 * @see koefude::ModelPreparationWindow::show
 */
//------------------------------------------------------------------------------
void ModelPreparationWindow::show( const HWND owner)
{
	// 再表示は同じWindowと選択位置を使い、workerや要求を増やさない
	if( !IsWindow( window_)) {
		owner_ = owner ;
		const auto instance = GetModuleHandleW( nullptr) ; // 本exeの非所有module
		WNDCLASSW  definition{} ; // 標準の再配置可能なWindow class
		definition.hInstance = instance ;
		definition.lpfnWndProc = windowProc ;
		definition.lpszClassName = L"KoeFudeModelPreparation" ;
		definition.hCursor = LoadCursorW( nullptr, IDC_ARROW) ;
		definition.hbrBackground = GetSysColorBrush( COLOR_BTNFACE) ;
		// 同一processで既登録なら同じclassを再利用する
		if( !RegisterClassW( &definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		const int	dpi = static_cast<int>( GetDpiForWindow( owner)) ; // 初期の親と同じDPI
		MONITORINFO monitor{} ; // 高DPIでも操作buttonを画面外へ置かない作業領域
		monitor.cbSize = sizeof(monitor) ;
		// 親と同じmonitorを確認できない場合に寸法を推測しない
		if( !GetMonitorInfoW( MonitorFromWindow( owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		const int width = std::min<int>( MulDiv( 820, dpi, 96),
										monitor.rcWork.right - monitor.rcWork.left) ; // 画面に収まる初期幅
		const int height = std::min<int>( MulDiv( 610, dpi, 96),
										 monitor.rcWork.bottom - monitor.rcWork.top) ; // 画面に収まる初期高
		window_ = CreateWindowExW( WS_EX_CONTROLPARENT, definition.lpszClassName, resource( IDS_PREP_TITLE).c_str(),
								  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
								  owner, nullptr, instance, this) ;
		// 作成失敗を準備成功と表示しない
		if( !window_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
	}
	opened_ = true ;
	// 自所有SW_HIDEの内部試験では表示/前面化を行いません
	if( IsWindowVisible( owner)) {
		ShowWindow( window_, SW_SHOWNORMAL) ;
		SetForegroundWindow( window_) ;
	}
}

//------------------------------------------------------------------------------
/**
 * @brief 必要な標準controlを同じUI threadで作成します
 * @see koefude::ModelPreparationWindow::initialize
 */
//------------------------------------------------------------------------------
void ModelPreparationWindow::initialize()
{
	// 有界な条件欄・段階・実byte進捗と三つの明示操作だけを作成する
	for( const int id : {IDC_PREP_DETAILS, IDC_PREP_STATUS, IDC_PREP_PROGRESS, IDC_PREP_DOWNLOAD, IDC_PREP_CANCEL,
						IDC_PREP_RECHECK}) { // 固定control ID
		const bool	   details = id == IDC_PREP_DETAILS ; // 選択・copy可能な条件欄
		const bool	   button = id >= IDC_PREP_DOWNLOAD ; // 三つの操作button
		const wchar_t *name = details					? L"EDIT"
							  : button					? L"BUTTON"
							  : id == IDC_PREP_PROGRESS ? PROGRESS_CLASSW
														: L"STATIC" ; // 標準class
		const DWORD style = details	 ? WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL
							: button ? WS_TABSTOP | BS_PUSHBUTTON
									 : 0 ; // 役割ごとのstyle
		const HWND	child = CreateWindowExW( 0, name, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
											reinterpret_cast<HMENU>( static_cast<INT_PTR>( id)), GetModuleHandleW( nullptr),
											nullptr) ; // Win32のcontrol ID値を渡すAPI境界
		// 一部controlがない不完全な画面を継続しない
		if( !child) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
	}
	SendDlgItemMessageW( window_, IDC_PREP_DETAILS, EM_SETLIMITTEXT, 64 * 1024, 0) ;
	SendDlgItemMessageW( window_, IDC_PREP_PROGRESS, PBM_SETRANGE32, 0, 100) ;
	layout() ;
}

//------------------------------------------------------------------------------
/**
 * @brief MS ゴシックを現在DPIへ合わせて配置します
 * @see koefude::ModelPreparationWindow::layout
 */
//------------------------------------------------------------------------------
void ModelPreparationWindow::layout()
{
	const UINT dpi = GetDpiForWindow( window_) ; // 現在monitorのDPI
	// DPI変更時だけfontを更新し、旧fontは全controlから外してから破棄する
	if( dpi != fontDpi_) {
		const HFONT next = CreateFontW( -MulDiv( 14, static_cast<int>( dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
									   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
									   FIXED_PITCH, L"ＭＳ ゴシック") ; // 利用者指定の所有font
		// resource不足時にNULL fontで画面を続行しない
		if( !next) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// 六つのcontrolが旧fontを使わなくなるまで保持する
		for( const int id : {IDC_PREP_DETAILS, IDC_PREP_STATUS, IDC_PREP_PROGRESS, IDC_PREP_DOWNLOAD, IDC_PREP_CANCEL,
							IDC_PREP_RECHECK}) { // 更新対象の標準control
			SendDlgItemMessageW( window_, id, WM_SETFONT, reinterpret_cast<WPARAM>( next), TRUE) ;
		}
		// 置換前に作成したfontだけを破棄する
		if( font_) {
			DeleteObject( font_) ;
		}
		font_ = next ;
		fontDpi_ = dpi ;
	}
	RECT area{} ; // 実clientのpixel座標
	GetClientRect( window_, &area) ;
	const int  scale = static_cast<int>( dpi) ; // MulDivの明示型
	const int  width = MulDiv( area.right, 96, scale) ; // 96DPI換算の幅
	const int  height = MulDiv( area.bottom, 96, scale) ; // 96DPI換算の高さ
	const auto place =
		//------------------------------------------------------------------------------
		/**
		 * @brief 論理座標を現在DPIへ変換してcontrolを配置します
		 * @param[in] id 所有するcontrolの識別子
		 * @param[in] x 論理左端pixel
		 * @param[in] y 論理上端pixel
		 * @param[in] w 論理幅pixel。最小1へ制限
		 * @param[in] h 論理高pixel。最小1へ制限
		 */
		//------------------------------------------------------------------------------
		[this, scale]( int id, int x, int y, int w, int h)
		{
			MoveWindow( GetDlgItem( window_, id), MulDiv( x, scale, 96), MulDiv( y, scale, 96),
					   MulDiv( std::max( 1, w), scale, 96), MulDiv( std::max( 1, h), scale, 96), TRUE) ;
		} ; // 同期配置だけの値callback
	place( IDC_PREP_DETAILS, 8, 8, width - 16, height - 134) ;
	place( IDC_PREP_STATUS, 8, height - 118, width - 16, 48) ;
	place( IDC_PREP_PROGRESS, 8, height - 65, width - 16, 16) ;
	place( IDC_PREP_DOWNLOAD, 8, height - 39, 252, 30) ;
	place( IDC_PREP_CANCEL, 270, height - 39, 140, 30) ;
	place( IDC_PREP_RECHECK, 420, height - 39, 178, 30) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 所有DTOから条件・容量・実測状態を再表示します
 * @param[in] options 現在選択した構成
 * @param[in] event 同じ構成の最新通知。呼出し中のborrow
 * @param[in] busy 終端未受領を含む操作中ならtrue
 * @param[in] speakerText 明示補助参照の用途説明を加える場合true
 * @see koefude::ModelPreparationWindow::update
 */
//------------------------------------------------------------------------------
void ModelPreparationWindow::update( const EngineOptions &options, const ModelPreparationEvent &event, const bool busy,
									const bool speakerText)
{
	// 閉じた画面のために条件文字列を作り直さない
	if( !IsWindow( window_) || !IsWindowVisible( window_)) {
		return ;
	}
	SetWindowTextW( window_, resource( IDS_PREP_TITLE).c_str()) ;
	SetDlgItemTextW( window_, IDC_PREP_DOWNLOAD, resource( IDS_PREP_DOWNLOAD).c_str()) ;
	SetDlgItemTextW( window_, IDC_PREP_CANCEL, resource( IDS_CANCEL).c_str()) ;
	SetDlgItemTextW( window_, IDC_PREP_RECHECK, resource( IDS_PREP_RECHECK).c_str()) ;
	auto details = describe( options, event.plan.get(), speakerText) ; // 小さいmetadataだけ
	// 同じ条件なら選択・scroll位置を維持する
	if( details != details_) {
		details_ = std::move( details) ;
		SetDlgItemTextW( window_, IDC_PREP_DETAILS, details_.c_str()) ;
	}
	// statusは実測byteや診断を追記する現在段階の説明
	auto status = resource( IDS_PREP_STAGES + static_cast<UINT>( event.progress.stage)) ;
	int	 percent = 0 ; // 総量不明の段階では架空の進捗を付けない
	// 実byteがある転送だけで割合を計算する
	if( event.progress.stage == ModelPreparationStage::DOWNLOADING && event.progress.transferBytes) {
		percent = static_cast<int>( 100 * std::min( event.progress.transferredBytes, event.progress.transferBytes) /
								   event.progress.transferBytes) ;
		status += L" " + std::to_wstring( percent) + L"% (" + std::to_wstring( event.progress.transferredBytes) + L" / " +
				  std::to_wstring( event.progress.transferBytes) + L" byte)" ;
	}
	// 検証済みの最終Readyだけを全体の完了とする
	if( event.progress.stage == ModelPreparationStage::READY) {
		percent = 100 ;
	}
	// API診断を保持し、モデル準備の失敗を音声の認識失敗へ変換しない
	if( event.progress.stage == ModelPreparationStage::FAILURE) {
		status += L" (" + std::to_wstring( static_cast<int>( event.error)) + L"/" + std::to_wstring( event.code) + L")" ;
	}
	SetDlgItemTextW( window_, IDC_PREP_STATUS, status.c_str()) ;
	SendDlgItemMessageW( window_, IDC_PREP_PROGRESS, PBM_SETPOS, static_cast<WPARAM>( percent), 0) ;
	EnableWindow( GetDlgItem( window_, IDC_PREP_DOWNLOAD), !busy && event.plan && !event.plan->ready) ;
	EnableWindow( GetDlgItem( window_, IDC_PREP_CANCEL), busy) ;
	EnableWindow( GetDlgItem( window_, IDC_PREP_RECHECK), !busy) ;
}

//------------------------------------------------------------------------------
/**
 * @brief OS callback境界でWindow操作を処理します
 * @param[in] window 対象Window
 * @param[in] message 標準message ID
 * @param[in] wparam message固有値
 * @param[in] lparam message固有値。WM_NCCREATEでは同期borrow
 * @return Win32で定義された処理結果
 * @see koefude::ModelPreparationWindow::windowProc
 */
//------------------------------------------------------------------------------
LRESULT CALLBACK ModelPreparationWindow::windowProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
	// 親が寿命を維持するwindow instanceへの借用pointer。関連付け前はNULL
	auto *self = reinterpret_cast<ModelPreparationWindow *>( GetWindowLongPtrW( window, GWLP_USERDATA)) ;
	// CreateWindowExの同期borrowを関連付け、親がinstance寿命を維持する
	if( message == WM_NCCREATE) {
		const auto *create = reinterpret_cast<const CREATESTRUCTW *>( lparam) ; // OSの同期borrow
		self = static_cast<ModelPreparationWindow *>( create->lpCreateParams) ;
		self->window_ = window ;
		SetWindowLongPtrW( window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( self)) ;
	}
	try {
		// instanceがない初期messageは標準Window処理へ任せる
		if( !self) {
			return DefWindowProcW( window, message, wparam, lparam) ;
		}
		// UIの操作だけを処理し、workerやfileをcallbackから使用しない
		switch( message) {
			case WM_CREATE:
				self->initialize() ;
				return 0 ; // 標準controlの初期化
			case WM_SIZE:
				self->layout() ;
				return 0 ; // 可変サイズの条件欄
			case WM_GETMINMAXINFO: {
				auto	 *limits = reinterpret_cast<MINMAXINFO *>( lparam) ; // OSが所有する出力値
				const int dpi = static_cast<int>( GetDpiForWindow( window)) ; // 最小寸法のDPI
				limits->ptMinTrackSize = {MulDiv( 640, dpi, 96), MulDiv( 420, dpi, 96)} ;
				return 0 ; // buttonが重ならない最小幅
			}
			case WM_DPICHANGED: {
				const auto *next = reinterpret_cast<const RECT *>( lparam) ; // OSの推奨矩形
				SetWindowPos( window, nullptr, next->left, next->top, next->right - next->left, next->bottom - next->top,
							 SWP_NOZORDER | SWP_NOACTIVATE) ;
				self->layout() ;
				return 0 ; // fontと配置を現在monitorへ追従
			}
			case WM_COMMAND:
				// 値だけを親へ通知し、同じUI threadでもcallbackを再入させない
				if( LOWORD(wparam) == IDC_PREP_DOWNLOAD || LOWORD(wparam) == IDC_PREP_CANCEL ||
				   LOWORD(wparam) == IDC_PREP_RECHECK) {
					PostMessageW( self->owner_, WM_COMMAND, LOWORD(wparam), 0) ;
					return 0 ;
				}
				// Escapeは画面だけを閉じ、取消しは専用buttonの明示操作とする
				if( LOWORD(wparam) == IDCANCEL) {
					ShowWindow( window, SW_HIDE) ;
					return 0 ;
				}
				break ;
			case WM_CLOSE:
				self->opened_ = false ;
				ShowWindow( window, SW_HIDE) ;
				return 0 ; // 転送は継続し、親の終了時には必ず協調停止する
			case WM_NCDESTROY:
				SetWindowLongPtrW( window, GWLP_USERDATA, 0) ;
				self->window_ = nullptr ;
				self->opened_ = false ;
				break ; // 同期borrowの解除
			default:
				break ; // 標準controlの通常動作を維持
		}
	} catch( ...) {
		MessageBoxW( window, resource( IDS_INTERNAL_ERROR).c_str(), L"KoeFude", MB_OK | MB_ICONERROR) ;
		// 初期化例外は不完全なWindowを成立させない
		if( message == WM_CREATE) {
			return -1 ;
		}
	}
	return DefWindowProcW( window, message, wparam, lparam) ;
}

} // namespace koefude
