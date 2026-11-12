//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_correction_window.cpp
 * @brief MS ゴシックの全量確認と一回の明示送信を接続します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 API確認・候補・原文選択を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1346の通常API補正・保存モデル表示・限定ログを接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の設定・文書二画面とtab表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と通常表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1350の確認済みTerra利用可否を表示
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1351のAPI専用HTTP失敗・保存状態を接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354の実API childと既存設定Applyを接続
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1355の実childとstatic背景を標準tabへ統一
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1374の未適用状態と失敗時model草稿を保持
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1376の設定全成功をmetadata/キー単独ACKと分離
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1382の案内fontをMS ゴシックへ統一
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1400の取得中表示と設定下部操作の整理を反映
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1402の候補選択と未確認/取得失敗の説明を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 dialogProcの役割・message別引数・戻り値・借用寿命を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/api_text_correction_window.h"
#include "app/api_catalog_time.h"
#include "app/api_text_provenance.h"
#include "app/api_text_status.h"
#include "app/localization.h"
#include "app/message_dialog.h"
#include "app/resource.h"
#include "app/tab_page_background.h"
#include "platform/win_helpers.h"
#include <optional>
#include <algorithm>
#include <array>
#include <commctrl.h>
#include <commdlg.h>
#include <ctime>
#include <cwchar>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief UTC取得日時を利用者が読める形へ変換します
 * @param[in] utc Unix秒
 * @return YYYY-MM-DD HH:mm:ss UTC、未取得/異常はresource文字列
 */
//------------------------------------------------------------------------------
std::wstring catalogTime( std::int64_t utc)
{
	return formatApiCatalogTime( utc) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 最終成功日時と最終試行を分けて日本語状態を返します
 * @param[in] meta component専用metadata
 * @param[in] models 認証状態を表示するか
 * @return resource由来状態
 */
//------------------------------------------------------------------------------
std::wstring catalogStatus( const ApiCatalogMetadata &meta, bool models)
{
	// 認証拒否は前回値の鮮度にかかわらず利用不可です
	if( models && (meta.http == 401 || meta.http == 403)) {
		return resource( IDS_API_META_AUTH_DENIED) ;
	}
	// 未取得は失敗・期限切れと区別します
	if( !meta.fetched) {
		return resource( meta.error ? IDS_API_META_FAILED : IDS_API_META_MISSING) ;
	}
	const auto now = apiCatalogNow() ; // 表示時のUTC秒
	// 逆行や期間超過は期限不明として扱います
	if( now < meta.fetched || now - meta.fetched > 86400 || now < meta.attempted ||
	   (meta.monotonic != std::chrono::steady_clock::time_point{} &&
		(std::chrono::steady_clock::now() < meta.monotonic ||
		 std::chrono::steady_clock::now() - meta.monotonic > std::chrono::seconds( 86400)))) {
		return resource( IDS_API_META_EXPIRED) ;
	}
	// 失敗後も成功日時を保持することを明示します
	if( meta.error || meta.http != 200) {
		return resource( IDS_API_META_PREVIOUS) ;
	}
	return resource( IDS_API_META_READY) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 自分のdialogを親monitorの作業領域へ収めます
 * @param[in] window 自所有dialog
 * @param[in] owner 親Window
 */
//------------------------------------------------------------------------------
void fitDialog( HWND window, HWND owner)
{
	MONITORINFO monitor{sizeof(monitor)} ; // 画面取得でなくOS配置情報
	// 親monitorの作業領域だけを参照します
	if( !GetMonitorInfoW( MonitorFromWindow( owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
		return ;
	}
	RECT current{} ; // 自分のWindow矩形
	GetWindowRect( window, &current) ;
	const auto &work = monitor.rcWork ; // taskbar等を除く領域
	const int	width = std::min( current.right - current.left, work.right - work.left) ; // 初期幅clamp
	const int	height = std::min( current.bottom - current.top, work.bottom - work.top) ; // 初期高clamp
	SetWindowPos( window, nullptr, work.left + (work.right - work.left - width) / 2,
				 work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE) ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 自分の画面だけを破棄します
 * @see koefude::ApiTextCorrectionWindow::~ApiTextCorrectionWindow
 */
//------------------------------------------------------------------------------
ApiTextCorrectionWindow::~ApiTextCorrectionWindow()
{
	// 取得中dialogを先に解放し、借用ownerより長く残しません
	if( IsWindow( catalogProgress_)) {
		DestroyWindow( catalogProgress_) ;
	}
	// 自分の設定画面だけを先に解放します
	if( settingsWindow_) {
		DestroyWindow( settingsWindow_) ;
	}
	// 自分のWindowだけを解放します
	if( window_) {
		DestroyWindow( window_) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 対象を固定して準備をqueueへ渡します
 * @param[in] owner 親
 * @param[in] request 原文所有値
 * @param[in] submit 同じJobController受付
 * @param[in] cancel 同じworker取消し
 * @param[in] catalogSubmit 本文なしmetadataの同worker受付
 * @see koefude::ApiTextCorrectionWindow::show
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::show( HWND owner, ApiTextCorrectionJobRequest request,
								   std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
								   std::function<void( std::uint64_t)>						 cancel,
								   std::function<std::uint64_t( ApiCatalogJobRequest)>		 catalogSubmit)
{
	// 進行中の別行・同じ行への再openでも対象を差し替えません
	if( active_) {
		return ;
	}
	direct_ = false ;
	opened_ = true ;
	stopped_ = false ;
	++windowGeneration_ ;
	pendingRefresh_ = true ;
	metadataFromSettings_ = false ;
	// 進行中は選択移動で送信対象を変えません
	if( !active_) {
		request_ = std::move( request) ;
		submit_ = std::move( submit) ;
		cancel_ = std::move( cancel) ;
		catalogSubmit_ = std::move( catalogSubmit) ;
	}
	// 初回だけ自分のdialogを作成します
	if( !window_) {
		window_ = CreateDialogParamW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDD_API_TEXT), owner, dialogProc,
									 reinterpret_cast<LPARAM>( this)) ; // WM_INITDIALOG中だけborrow
		// 作成失敗は固定codeで親へ返します
		if( !window_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		SendDlgItemMessageW( window_, IDC_API_KEY, EM_SETLIMITTEXT, 2048, 0) ;
		SendDlgItemMessageW( window_, IDC_API_BUDGET, EM_SETLIMITTEXT, 31, 0) ;
		SendDlgItemMessageW( window_, IDC_API_PREVIEW, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0) ;
		SendDlgItemMessageW( window_, IDC_API_CANDIDATE, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0) ;
		TCITEMW item{} ; // resource由来のtab文字列を同期copy
		item.mask = TCIF_TEXT ;
		auto original = resource( IDS_API_ORIGINAL_TAB) ; // 原文全文
		item.pszText = original.data() ;
		TabCtrl_InsertItem( GetDlgItem( window_, IDC_API_TABS), 0, &item) ;
		auto candidate = resource( IDS_API_CANDIDATE_TAB) ; // 改善候補
		item.pszText = candidate.data() ;
		TabCtrl_InsertItem( GetDlgItem( window_, IDC_API_TABS), 1, &item) ;
		showTab() ;
		fitDialog( window_, owner) ;
		layoutDocument() ;
	}
	// 非表示の自process試験では画面を表示しません
	if( IsWindowVisible( owner)) {
		ShowWindow( window_, SW_SHOWNA) ;
	}
	request_.budget = K_API_NO_BUDGET_LIMIT ;
	SetDlgItemTextW( window_, IDC_API_PREVIEW, L"") ;
	SetDlgItemTextW( window_, IDC_API_CANDIDATE, L"") ;
	TabCtrl_SetCurSel( GetDlgItem( window_, IDC_API_TABS), 0) ; // 新しい対象は必ず送信原文から
	showTab() ;
	// 明示文章改善の必要更新だけを開始します
	if( !active_) {
		if( catalogSubmit_) {
			refresh( true) ;
		} else {
			start( ApiTextAction::PREPARE) ;
		} // legacy専用fake接続だけ
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 画面を作らず起動時のlocal準備を依頼します
 * @param[in] submit 同じworker受付
 * @param[in] cancel 取消し
 * @param[in] catalogSubmit 本文なしmetadata受付
 * @see koefude::ApiTextCorrectionWindow::initialize
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::initialize( std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
										 std::function<void( std::uint64_t)>						   cancel,
										 std::function<std::uint64_t( ApiCatalogJobRequest)>		   catalogSubmit)
{
	direct_ = false ;
	submit_ = std::move( submit) ;
	cancel_ = std::move( cancel) ;
	catalogSubmit_ = std::move( catalogSubmit) ;
	ApiCatalogJobRequest local ; // 本文もpathも含まないlocal専用要求
	local.localOnly = true ;
	local.prices = false ;
	local.windowGeneration = windowGeneration_ ;
	active_ = catalogSubmit_( local) ;
	catalogActive_ = active_ != 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 音声APIの明示設定表示だけでmetadataを更新します
 * @see koefude::ApiTextCorrectionWindow::showAudioMetadata
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::showAudioMetadata()
{
	// 同じ設定sessionのtab往復でGETを増やしません
	if( audioSettingsOpened_) {
		return ;
	}
	audioSettingsOpened_ = true ;
	// 起動時local準備中なら終端後の一更新へ集約します
	if( active_) {
		pendingRefresh_ = true ;
	} else {
		refresh( true) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief local前提から行メニュー可否を返します
 * @return 認証確認前の実行受付可否
 * @see koefude::ApiTextCorrectionWindow::ready
 */
//------------------------------------------------------------------------------
bool ApiTextCorrectionWindow::ready() const
{
	return !active_ && !stopped_ && !apiMenuSnapshot( catalog_, keyReady_).empty() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 画面なしで対象行の明示操作を受け付けます
 * @param[in] value 固定source
 * @param[in] action ImproveまたはHTTP0表示操作
 * @return 受付ID、拒否0
 * @see koefude::ApiTextCorrectionWindow::direct
 */
//------------------------------------------------------------------------------
std::uint64_t ApiTextCorrectionWindow::direct( ApiTextCorrectionJobRequest value, ApiTextAction action)
{
	// 同じ単一workerの処理中や準備不足は追加要求へ変えません
	if( active_ || stopped_ || !submit_ || (action == ApiTextAction::IMPROVE && !ready())) {
		return 0 ;
	}
	value.action = action ;
	value.operationId = 0 ;
	// 有料実行は受付時の適用済み設定とlocal metadataへ束縛します
	if( action == ApiTextAction::IMPROVE) {
		value.catalogSnapshot = apiMenuSnapshot( catalog_, keyReady_) ;
		value.catalogGeneration = catalog_.generation ;
		value.keyRevision = catalog_.keyRevision ;
		value.budget = K_API_NO_BUDGET_LIMIT ;
	}
	request_ = value ;
	direct_ = true ;
	catalogActive_ = false ;
	activeSettings_ = false ; // 設定画面のcloseで取り消しません
	active_ = submit_( std::move( value)) ;
	update() ;
	return active_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 履歴なしで独立設定を開きます
 * @param[in] owner 親
 * @param[in] submit キー専用worker受付
 * @param[in] cancel 同worker取消し
 * @param[in] catalogSubmit 本文なし設定/metadata受付
 * @param[in] embedded 統合設定のchildとして配置するか
 * @see koefude::ApiTextCorrectionWindow::showSettings
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::showSettings( HWND owner, std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
										   std::function<void( std::uint64_t)>				  cancel,
										   std::function<std::uint64_t( ApiCatalogJobRequest)> catalogSubmit,
										   bool												  embedded)
{
	settingsEmbedded_ = embedded ; // 同じcontrols/workerを統合pageでも使います
	settingsPageVisible_ = embedded ;
	const bool reopening = !settingsOpened_ ; // 表示中の未適用入力は保持
	settingsOpened_ = true ;
	// close後の入力を次openへ復元しません
	if( reopening) {
		discardSettingsEdits() ;
		settingsReceived_ = false ;
	}
	// 通常行の進行中は取消し所有を変更しません
	if( !active_) {
		direct_ = false ;
	}
	stopped_ = false ;
	// 送信中は凍結された受付/対象を保持し、設定は閲覧だけです
	if( !active_) {
		submit_ = std::move( submit) ;
		cancel_ = std::move( cancel) ;
		catalogSubmit_ = std::move( catalogSubmit) ;
	}
	// 設定はsourceも履歴選択も要求しません
	if( !settingsWindow_) {
		settingsWindow_ =
			CreateDialogParamW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDD_API_SETTINGS), owner, dialogProc,
							   reinterpret_cast<LPARAM>( this)) ; // 初期化中の同期borrow
		// 作成失敗は固定診断へ変換します
		if( !settingsWindow_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		SendDlgItemMessageW( settingsWindow_, IDC_API_KEY, EM_SETLIMITTEXT, 2048, 0) ;
		SendDlgItemMessageW( settingsWindow_, IDC_API_KEY, EM_SETPASSWORDCHAR, 0x25cf, 0) ;
		const auto table = GetDlgItem( settingsWindow_, IDC_API_PRICES) ; // 標準read-only一覧
		ListView_SetExtendedListViewStyle( table, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES) ;
		const std::array labels{IDS_API_RATE_RANGE, IDS_API_RATE_INPUT,	  IDS_API_RATE_OUTPUT,
								IDS_API_RATE_CACHE, IDS_API_RATE_WRITE30, IDS_API_RATE_WRITE12} ;
		// 項目名を一回だけ作り、単価値は毎回catalogから更新します
		for( std::size_t i = 0; i < labels.size(); ++i) {
			auto	  label = resource( labels[i]) ; // resource由来の日本語列名
			LVCOLUMNW column{} ; // 標準tableが同期copy
			column.mask = LVCF_TEXT | LVCF_WIDTH ;
			column.pszText = label.data() ;
			RECT bounds{} ; // 自所有tableのclient幅に六列を収めます
			GetClientRect( table, &bounds) ;
			column.cx = (bounds.right - bounds.left) / static_cast<int>( labels.size()) ;
			ListView_InsertColumn( table, static_cast<int>( i), &column) ;
		}
		// 非表示で作成した既存dialogを実childへ移し、captionや二重Applyを除きます
		if( embedded) {
			SetWindowLongPtrW( settingsWindow_, GWL_STYLE,
							  (GetWindowLongPtrW( settingsWindow_, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION | WS_SYSMENU)) |
								  WS_CHILD | DS_CONTROL) ;
			SetWindowLongPtrW( settingsWindow_, GWL_EXSTYLE, WS_EX_CONTROLPARENT) ;
			SetParent( settingsWindow_, owner) ;
			DestroyWindow( GetDlgItem( settingsWindow_, IDC_API_DETAILS)) ; // childでは詳細buttonを撤去
			ShowWindow( GetDlgItem( settingsWindow_, IDC_API_SETTINGS_APPLY), SW_HIDE) ;
			ShowWindow( GetDlgItem( settingsWindow_, IDCANCEL), SW_HIDE) ;
		} else {
			fitDialog( settingsWindow_, owner) ;
		}
	}
	// 自所有hidden試験では表示しません
	if( embedded || IsWindowVisible( owner)) {
		ShowWindow( settingsWindow_, SW_SHOWNA) ;
	}
	// 終端待ちのreopenは旧世代を無視して一回だけ新鮮な状態を取得します
	if( active_ && activeSettings_ && reopening) {
		pendingRefresh_ = true ;
	}
	update() ;
	// 独立設定openだけがmetadata更新を許可します
	if( !active_) {
		++windowGeneration_ ;
		metadataFromSettings_ = true ;
		refresh( true) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 既存callbacksを使って統合設定の実child pageを開きます
 * @param[in] owner host
 * @see koefude::ApiTextCorrectionWindow::showSettingsPage
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::showSettingsPage( HWND owner)
{
	showSettings( owner, submit_, cancel_, catalogSubmit_, true) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現sessionの未適用欄を照合します
 * @return 明示Applyが必要
 * @see koefude::ApiTextCorrectionWindow::settingsDirty
 */
//------------------------------------------------------------------------------
bool ApiTextCorrectionWindow::settingsDirty() const noexcept
{
	const bool persistChanged =
		persistEdited_ && (IsDlgButtonChecked( settingsWindow_, IDC_API_PERSIST) == BST_CHECKED) != keyPersisted_ ;
	return settingsOpened_ && (keyEdited_ || persistChanged || editingModel_ != selected_ || settingsSaveFailed_) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未適用API編集だけを既存の一回Applyへ渡します
 * @see koefude::ApiTextCorrectionWindow::applySettingsPage
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::applySettingsPage()
{
	// 一般tabのApplyや未編集placeholderを保存操作へ変換しません
	if( settingsDirty()) {
		saveSettings() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 未適用secret破棄と設定操作だけの協調取消しを依頼します
 * @see koefude::ApiTextCorrectionWindow::closeSettingsPage
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::closeSettingsPage()
{
	// 同じ設定の既存closeはSend所有を変えず、未適用secretを消去します
	if( IsWindow( settingsWindow_) && settingsOpened_) {
		static_cast<void>( dialogProc( settingsWindow_, WM_CLOSE, 0, 0)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 統合設定childの内容を現在clientへ配置します
 * @see koefude::ApiTextCorrectionWindow::layoutSettingsPage
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::layoutSettingsPage()
{
	// 独立旧adapterのgeometryは変更しません
	if( !settingsEmbedded_ || !IsWindow( settingsWindow_)) {
		return ;
	}
	RECT area{} ; // 自所有page client
	GetClientRect( settingsWindow_, &area) ;
	const int  dpi = static_cast<int>( GetDpiForWindow( settingsWindow_)) ; // 現DPI
	const int  width = MulDiv( area.right, 96, dpi) ; // 論理幅
	const int  height = MulDiv( area.bottom, 96, dpi) ; // 論理高
	const auto place =
		//------------------------------------------------------------------------------
		/**
		 * @brief 同じDPIでpage内だけへ配置します
		 * @param[in] id 配置する自dialog内のchild control ID
		 * @param[in] x 親client左端からの論理pixel位置（96 DPI基準）
		 * @param[in] y 親client上端からの論理pixel位置（96 DPI基準）
		 * @param[in] w 論理pixel幅。1未満は1へ丸めて現DPIへ換算します
		 * @param[in] h 論理pixel高さ。1未満は1へ丸めて現DPIへ換算します
		 * @note 捕捉したDPIで座標を換算し、MoveWindowで再描画を要求します。controlの所有権は移しません
		 */
		//------------------------------------------------------------------------------
		[&]( int id, int x, int y, int w, int h)
		{
			MoveWindow( GetDlgItem( settingsWindow_, id), MulDiv( x, dpi, 96), MulDiv( y, dpi, 96),
					   MulDiv( std::max( 1, w), dpi, 96), MulDiv( std::max( 1, h), dpi, 96), TRUE) ;
		} ;
	place( IDC_API_NOTICE_LABEL, 4, 2, width - 8, 40) ;
	place( IDC_API_KEY_LABEL, 4, 54, 108, 24) ;
	place( IDC_API_KEY, 120, 48, width - 128, 26) ;
	place( IDC_API_PERSIST, 120, 82, width - 128, 24) ;
	place( IDC_API_KEY_STATE, 120, 112, width - 128, 36) ;
	place( IDC_API_MODEL_LABEL, 4, 158, 108, 24) ;
	place( IDC_API_MODEL, 120, 152, width - 128, 220) ;
	place( IDC_API_PRICES_LABEL, 4, 198, width - 248, 24) ;
	place( IDC_API_REFRESH, width - 240, 190, 232, 28) ;
	place( IDC_API_REPORT, 4, 224, width - 12, 38) ;
	place( IDC_API_PRICES, 4, 266, width - 12, 62) ;
	place( IDC_API_METADATA, 4, 336, width - 12, std::max( 64, height - 340)) ;
	// 全10単価を同じpage幅へ収め、横scrollを要求しません
	for( int column = 0; column < 6; ++column) {
		ListView_SetColumnWidth( GetDlgItem( settingsWindow_, IDC_API_PRICES), column,
								std::max<LONG>( 1, (area.right - MulDiv( 16, dpi, 96)) / 6)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 同じsessionの表示だけを切り替え、取得中表示を同期します
 * @param[in] visible 文章改善tabが選択されているか。通信は開始しません
 * @see koefude::ApiTextCorrectionWindow::setSettingsPageVisible
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::setSettingsPageVisible( bool visible)
{
	settingsPageVisible_ = visible ;
	ShowWindow( settingsWindow_, visible ? SW_SHOWNA : SW_HIDE) ;
	updateSettingsActivity() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 表示中の設定tabだけでmetadata取得のowned dialogを同期します
 * @see koefude::ApiTextCorrectionWindow::updateSettingsActivity
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::updateSettingsActivity()
{
	const bool fetching = settingsEmbedded_ && settingsOpened_ && settingsPageVisible_ && !stopped_ && active_ &&
						  catalogActive_ && catalogRefresh_ && !catalogProgressDismissed_ ; // 実metadata要求だけの表示
	// 完了/失敗/close/別tabで所有dialogとanimationを同時に解放します
	if( !fetching) {
		if( IsWindow( catalogProgress_)) {
			DestroyWindow( catalogProgress_) ;
		}
		return ;
	}
	// 同じoperation中の通常更新でdialogやanimationを再作成しません
	if( !IsWindow( catalogProgress_)) {
		const auto owner = GetParent( settingsWindow_) ; // 統合設定の非所有HWND
		catalogProgress_ =
			CreateDialogParamW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDD_API_CATALOG_PROGRESS), owner,
							   catalogProgressProc, reinterpret_cast<LPARAM>( this)) ; // 同期初期化borrow
		// 表示失敗は同じcallback境界の既存診断へ返します
		if( !catalogProgress_) {
			catalogProgressDismissed_ = true ; // 同opのエラー表示から再生成を繰り返しません
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		fitDialog( catalogProgress_, owner) ;
		SendDlgItemMessageW( catalogProgress_, IDC_API_CATALOG_PROGRESS, PBM_SETMARQUEE, TRUE, 50) ;
	}
	SetDlgItemTextW( settingsWindow_, IDC_API_REPORT, resource( IDS_API_CATALOG_WORKING).c_str()) ;
	// 自所有SW_HIDE試験ではdialogを実表示せず、通常画面では前面化を要求しません
	if( IsWindowVisible( settingsWindow_)) {
		ShowWindow( catalogProgress_, SW_SHOWNOACTIVATE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief metadata取得中dialogのclose/Escを同じ有効要求の協調取消しへ渡します
 * @param[in] window UI threadでOSから渡される自所有dialogのHWND。Windowを別threadへ渡しません
 * @param[in] message WM_INITDIALOG、WM_COMMAND、WM_CLOSE、WM_NCDESTROY等の通知種別です
 * @param[in] wparam WM_COMMANDではLOWORDのIDCANCELを検査します。初期化時はOSの既定focus候補で、それ以外は通知固有値です
 * @param[in] lparam WM_INITDIALOGではdialog寿命中有効なApiTextCorrectionWindowの非null借用pointerをDWLP_USERへ登録します
 *        他のmessageでは使用しません
 * @return 初期化とclose/Escの処理時はTRUE、owner未登録・破棄通知・未処理messageはFALSEです。TRUEはmetadata取得や取消し完了を表しません
 * @note closeではdialogを破棄し、現在のmetadata操作だけを取消します。取消しcallbackの例外は固定診断へ変換して設定内へ表示します
 */
//------------------------------------------------------------------------------
INT_PTR CALLBACK ApiTextCorrectionWindow::catalogProgressProc( HWND window, UINT message, WPARAM wparam,
															  LPARAM lparam) noexcept
{
	auto *self =
		reinterpret_cast<ApiTextCorrectionWindow *>( GetWindowLongPtrW( window, DWLP_USER)) ; // dialog寿命中だけのborrow
	// 同期生成時だけownerを登録します
	if( message == WM_INITDIALOG) {
		SetWindowLongPtrW( window, DWLP_USER, lparam) ;
		return TRUE ;
	}
	// 生成前/破棄後の通知は標準処理へ返します
	if( !self) {
		return FALSE ;
	}
	// owner破棄も含め、破棄したHWNDを次operationへ残しません
	if( message == WM_NCDESTROY) {
		self->catalogProgress_ = nullptr ;
		return FALSE ;
	}
	// close/Escは確認なしで現在のmetadata要求だけを協調取消しします
	if( message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
		self->catalogProgressDismissed_ = true ;
		DestroyWindow( window) ;
		try {
			// 通常音声/有料文章要求にはこのdialogの取消しを適用しません
			if( self->active_ && self->catalogActive_ && self->catalogRefresh_ && self->cancel_) {
				self->cancel_( self->active_) ;
			}
			self->report_ = resource( IDS_API_PHASE_CANCELLING) ;
			self->update() ;
		} catch( const OperationError &error) {
			// callbackの失敗は固定説明と安全なcodeだけで設定内へ戻します
			self->report_ = resource( IDS_API_FAILED) + L" code=" + std::to_wstring( error.code()) ;
			SetDlgItemTextW( self->settingsWindow_, IDC_API_REPORT, self->report_.c_str()) ;
		} catch( ...) {
			// 任意例外文字列を表示せず、取消し受付失敗を固定説明にします
			self->report_ = resource( IDS_API_FAILED) + L" code=" + std::to_wstring( ERROR_INTERNAL_ERROR) ;
			SetDlgItemTextW( self->settingsWindow_, IDC_API_REPORT, self->report_.c_str()) ;
		}
		return TRUE ;
	}
	return FALSE ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未適用編集とダミー入力を破棄します
 * @see koefude::ApiTextCorrectionWindow::discardSettingsEdits
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::discardSettingsEdits()
{
	pendingSettings_.reset() ;
	settingsSaveFailed_ = false ;
	refreshAfterApply_ = false ;
	keyEdited_ = false ;
	persistEdited_ = false ;
	editingModel_ = selected_ ;
	rebuilding_ = true ;
	SetDlgItemTextW( settingsWindow_, IDC_API_KEY, L"") ;
	rebuilding_ = false ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一回Applyの一般設定をキー成功後にworkerへ渡します
 * @see koefude::ApiTextCorrectionWindow::submitSettings
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::submitSettings()
{
	// 同じ開いた設定の一回Applyだけを次段へ進めます
	if( stopped_ || !pendingSettings_ || !settingsOpened_ || applyGeneration_ != windowGeneration_) {
		pendingSettings_.reset() ;
		return ;
	}
	ApiCatalogJobRequest value ; // 本文も秘密も持たない一般設定要求
	value.windowGeneration = windowGeneration_ ;
	value.keyRevision = catalog_.keyRevision ;
	value.settingsOnly = true ;
	value.settings = pendingSettings_ ;
	activeSettings_ = true ;
	active_ = catalogSubmit_( value) ;
	catalogActive_ = active_ != 0 ;
	catalogRefresh_ = false ;
	// queue拒否を全適用成功と表示しません
	if( !active_) {
		settingsSaveFailed_ = true ;
		pendingSettings_.reset() ;
		// キー成功後のModels更新は、次の明示保存成功ACKまで保留します
	}
	report_ = resource( active_ ? IDS_API_WORKING : IDS_API_BUSY) ;
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 一般設定をworkerへ保存します
 * @see koefude::ApiTextCorrectionWindow::saveSettings
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::saveSettings()
{
	// 進行中Sendと二重Applyの設定を変更しません
	if( active_ || !catalogSubmit_ || !submit_ || !settingsOpened_) {
		return ;
	}
	settingsApplySucceeded_ = false ; // 失敗/取消し/queue拒否を続行可能へ変えません
	ApiGeneralSettings settings ; // 利用者予算なしの新規設定、秘密を持てません
	settings.model = editingModel_ ;
	ApiTextCorrectionJobRequest key ; // 本文や履歴を持たない一回のキー要求
	key.action = ApiTextAction::SET_KEY ;
	key.persistKey = IsDlgButtonChecked( settingsWindow_, IDC_API_PERSIST) == BST_CHECKED ;
	// 未編集placeholderを読み出したり秘密として保存したりしません
	if( keyEdited_) {
		std::array<wchar_t, 2049> secret{} ; // 変換失敗時もゼロ化
		GetDlgItemTextW( settingsWindow_, IDC_API_KEY, secret.data(), static_cast<int>( secret.size())) ;
		const auto length = std::wcslen( secret.data()) ; // 有界single-line control
		const bool blank = std::all_of( secret.begin(), secret.begin() + length,
			//------------------------------------------------------------------------------
			/**
			 * @brief 半角/全角空白等だけを削除意図とします
			 * @param[in] c 文字
			 * @return 空白
			 */
			//------------------------------------------------------------------------------
			[]( wchar_t c)
			{
				return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\x3000' ;
			}) ;
		try {
			// 編集済みの空/空白だけは削除し、非空値はsecure ownerで一度渡します
			if( blank) {
				key.action = ApiTextAction::ERASE_KEY ;
			} else {
				key.secret = std::make_shared<ApiKeySecret>( toUtf8( secret.data())) ;
			}
		} catch( ...) {
			SecureZeroMemory( secret.data(), sizeof(secret)) ;
			throw ;
		}
		SecureZeroMemory( secret.data(), sizeof(secret)) ;
	} else {
		key.retainKey = true ; // checkbox変更だけならworker内の秘密を保持
	}
	keyApplied_ = false ;
	// キー成功後に一般保存だけ失敗した場合、明示再適用の成功後に一回更新します
	refreshAfterApply_ = refreshAfterApply_ && settingsSaveFailed_ ;
	pendingSettings_ = settings ;
	applyGeneration_ = windowGeneration_ ;
	request_.draft.reset() ;
	request_.catalogSnapshot.clear() ;
	// キーも保存方式も未編集なら、秘密へ触れずモデル保存だけを依頼します
	const bool persistChanged = persistEdited_ && key.persistKey != keyPersisted_ ;
	if( !keyEdited_ && !persistChanged) {
		submitSettings() ;
		return ;
	}
	refreshAfterApply_ = refreshAfterApply_ || keyEdited_ ;
	const bool persistOnly = !keyEdited_ ; // 失敗時もsecretなしの方式変更だけ保持
	rebuilding_ = true ;
	SetDlgItemTextW( settingsWindow_, IDC_API_KEY, L"") ; // 実入力をUIへ残しません
	rebuilding_ = false ;
	keyEdited_ = false ;
	persistEdited_ = persistOnly ;
	catalogActive_ = false ;
	activeSettings_ = true ;
	applyingKey_ = true ;
	active_ = submit_( std::move( key)) ; // request_へ秘密を保存しません
	// キー段の受付失敗では一般設定段を開始しません
	if( !active_) {
		applyingKey_ = false ;
		pendingSettings_.reset() ;
		refreshAfterApply_ = false ;
	}
	report_ = resource( active_ ? IDS_API_WORKING : IDS_API_BUSY) ;
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現在tabの本文だけを表示します
 * @see koefude::ApiTextCorrectionWindow::showTab
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::showTab()
{
	// 未作成画面には操作しません
	if( !window_) {
		return ;
	}
	const bool candidate = TabCtrl_GetCurSel( GetDlgItem( window_, IDC_API_TABS)) == 1 ; // 現tab
	ShowWindow( GetDlgItem( window_, IDC_API_PREVIEW), candidate ? SW_HIDE : SW_SHOWNA) ;
	ShowWindow( GetDlgItem( window_, IDC_API_CANDIDATE), candidate ? SW_SHOWNA : SW_HIDE) ;
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief compact文書の配置をclientとDPIへ合わせます
 * @see koefude::ApiTextCorrectionWindow::layoutDocument
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::layoutDocument()
{
	// 自分の文書画面だけ配置します
	if( !window_) {
		return ;
	}
	RECT client{} ; // 実client
	GetClientRect( window_, &client) ;
	const int dpi = static_cast<int>( GetDpiForWindow( window_)) ; // 現monitor
	const int margin = MulDiv( 12, dpi, 96) ; // 外周余白
	const int top = MulDiv( 88, dpi, 96) ; // 短い説明後
	const int footer = MulDiv( 88, dpi, 96) ; // 状態と主操作
	const int width = std::max( 1L, client.right - 2 * margin) ; // 利用可能幅
	const int bottom = std::max( top + 80, static_cast<int>( client.bottom) - footer) ; // 本文下端
	MoveWindow( GetDlgItem( window_, IDC_API_SUMMARY), margin, margin, width, MulDiv( 38, dpi, 96), TRUE) ;
	const auto tabs = GetDlgItem( window_, IDC_API_TABS) ; // 自分のtab
	MoveWindow( tabs, margin, top, width, bottom - top, TRUE) ;
	RECT text{0, 0, width, bottom - top} ; // tab内部の実本文領域
	TabCtrl_AdjustRect( tabs, FALSE, &text) ;
	// 二つの本文は全く同じ位置で切り替えます
	for( const int id : {IDC_API_PREVIEW, IDC_API_CANDIDATE}) {
		MoveWindow( GetDlgItem( window_, id), margin + text.left + 3, top + text.top + 3,
				   std::max( 1L, text.right - text.left - 6), std::max( 1L, text.bottom - text.top - 6), TRUE) ;
	}
	MoveWindow( GetDlgItem( window_, IDC_API_REPORT), margin, bottom + 6, width, MulDiv( 30, dpi, 96), TRUE) ;
	const std::array ids{IDC_API_SEND, IDC_API_ADOPT,		  IDC_API_CANCEL,
						 IDC_API_MORE, IDC_API_SETTINGS_OPEN, IDCANCEL} ; // 主操作
	const int		 gap = MulDiv( 6, dpi, 96) ; // button間隔
	const int		 button = (width - gap * 5) / 6 ; // 等幅、最小幅はWM_GETMINMAXINFOで保証
	// 主操作だけを下端へ合わせます
	for( std::size_t i = 0; i < ids.size(); ++i) {
		MoveWindow( GetDlgItem( window_, ids[i]), margin + static_cast<int>( i) * (button + gap),
				   static_cast<int>( client.bottom) - margin - MulDiv( 29, dpi, 96), button, MulDiv( 29, dpi, 96), TRUE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief API画面内のmetadataだけを更新します
 * @param[in] prices 公開価格も取得するか
 * @see koefude::ApiTextCorrectionWindow::refresh
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::refresh( bool prices)
{
	// 全API要求は同じ単一受付を共有します
	if( active_ || stopped_ || (!opened_ && !settingsOpened_ && !audioSettingsOpened_) || !catalogSubmit_) {
		return ;
	}
	direct_ = false ;
	request_.draft.reset() ;
	request_.catalogSnapshot.clear() ;
	pendingRefresh_ = false ;
	ApiCatalogJobRequest request ; // 本文/source/pathを持ちません
	request.windowGeneration = windowGeneration_ ;
	request.keyRevision = catalog_.keyRevision ;
	request.prices = prices ;
	activeSettings_ = metadataFromSettings_ ;
	active_ = catalogSubmit_( request) ;
	catalogActive_ = active_ != 0 ;
	catalogRefresh_ = catalogActive_ ;
	catalogProgressDismissed_ = false ;
	report_ = resource( active_ ? IDS_API_CATALOG_WORKING : IDS_API_BUSY) ;
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 選択のsnapshotを同期し古い同意を失効します
 * @see koefude::ApiTextCorrectionWindow::selectModel
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::selectModel()
{
	request_.draft.reset() ;
	request_.catalogSnapshot.clear() ;
	choices_ = apiCatalogChoices( catalog_, selected_) ;
	// 現在の厳密IDに対応したsnapshotだけを利用します
	for( const auto &choice : choices_) {
		// 未確認IDにはsnapshotがありません
		if( choice.id == selected_) {
			request_.catalogSnapshot = choice.snapshot ;
		}
	}
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じ原文と一回の操作をenqueueします
 * @param[in] action 明示操作
 * @see koefude::ApiTextCorrectionWindow::start
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::start( ApiTextAction action)
{
	// 二重押下とclose後は余分な要求を作りません
	if( active_ || stopped_ || !submit_) {
		return ;
	}
	direct_ = false ; // 独立設定/旧明示画面の終端と通常行を区別します
	// 互換性・鮮度未確認の新規準備は拒否します。既存候補操作は継続できます
	if( action == ApiTextAction::PREPARE && catalogSubmit_ && request_.catalogSnapshot.empty()) {
		return ;
	}
	auto value = request_ ; // 同じsource ownerへ束縛
	value.action = action ;
	value.operationId = 0 ;
	value.secret.reset() ;
	// 文書画面は適用済み一般設定だけを使います
	if( action == ApiTextAction::PREPARE) {
		value.budget = K_API_NO_BUDGET_LIMIT ;
		request_.draft.reset() ;
	}
	// キー設定要求へ文書本文/候補を付けません
	if( action == ApiTextAction::SET_KEY || action == ApiTextAction::ERASE_KEY) {
		value.source.reset() ;
		value.key = {} ;
		value.record = {} ;
		value.draft.reset() ;
		value.catalogSnapshot.clear() ;
	}
	// キー入力はmaskされた小さいcontrolから一回だけ所有します
	if( action == ApiTextAction::SET_KEY) {
		std::array<wchar_t, 2049> secret{} ; // 読取後必ずゼロ化
		GetDlgItemTextW( settingsWindow_, IDC_API_KEY, secret.data(), static_cast<int>( secret.size())) ;
		try {
			value.secret = std::make_shared<ApiKeySecret>( toUtf8( secret.data())) ;
		} catch( ...) {
			SecureZeroMemory( secret.data(), sizeof(secret)) ;
			throw ;
		}
		SecureZeroMemory( secret.data(), sizeof(secret)) ;
		SetDlgItemTextW( settingsWindow_, IDC_API_KEY, L"") ;
		value.persistKey = IsDlgButtonChecked( settingsWindow_, IDC_API_PERSIST) == BST_CHECKED ;
	}
	// ボタン一回が確認済み全文・予算を束縛します
	if( action == ApiTextAction::SEND) {
		// 設定だけ/予算変更だけでは送信しません
		if( !request_.draft || !keyReady_) {
			return ;
		}
		value.consent = confirmApiText( *request_.draft) ;
	}
	catalogActive_ = false ;
	activeSettings_ = action == ApiTextAction::SET_KEY || action == ApiTextAction::ERASE_KEY ;
	active_ = submit_( std::move( value)) ;
	report_ = resource( active_ ? IDS_API_WORKING : IDS_API_BUSY) ;
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 同じoperation終端を受領します
 * @param[in] event 型付きevent
 * @return 自分の要求
 * @see koefude::ApiTextCorrectionWindow::receive
 */
//------------------------------------------------------------------------------
bool ApiTextCorrectionWindow::receive( const JobEvent &event)
{
	// 他画面や古いoperationの結果を混ぜません
	if( !active_ || event.id != active_) {
		return false ;
	}
	// 進捗中は固定の待機表示を保持します
	if( !event.terminal) {
		report_ = resource( event.progress.stage == Stage::SAVING		 ? IDS_API_PERSISTING
						   : event.progress.stage == Stage::TRANSCRIBING ? IDS_API_AWAITING
																		 : IDS_API_PREPARING) ;
		// chunk完了数に基づく率だけを表示します
		if( event.progress.percent >= 0) {
			report_ += L" " + std::to_wstring( event.progress.percent) + L"%" ;
		}
		update() ;
		return true ;
	}
	active_ = 0 ;
	// metadata終端は画面generationとキーrevisionを照合します
	if( catalogActive_) {
		catalogActive_ = false ;
		const auto result = event.apiCatalog ; // 本文なし終端
		// close/reopen前の終端を現選択へ適用しません
		if( result && (result->request.localOnly || opened_ || settingsOpened_ || audioSettingsOpened_) &&
		   result->request.windowGeneration == windowGeneration_ &&
		   (!result->request.keyRevision || result->request.keyRevision == catalog_.keyRevision)) {
			catalog_ = result->catalog ;
			// 初回取得/明示適用後は別保存の一般設定を復元します
			if( !settingsReceived_ || (result->request.settingsOnly && event.succeeded && !result->code)) {
				settingsReceived_ = true ;
				selected_ = catalog_.settings.model ;
				editingModel_ = selected_ ;
			}
			selected_ = catalog_.settings.model ; // 送信のmodel/予算は同じ適用済み値
			// HTTP0設定適用は現在キー状態を変更しません
			if( !result->request.settingsOnly) {
				keyReady_ = result->keyReady ;
				keyPersisted_ = result->keyPersisted ;
			}
			choices_ = apiCatalogChoices( catalog_, selected_) ;
			SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_RESETCONTENT, 0, 0) ;
			unsigned index = 0 ; // 有界一覧位置
			// 未対応IDも理由と一緒に一覧へ表示します
			for( const auto &choice : choices_) {
				const auto text =
					toWide( choice.id) + L" / " +
					resource( IDS_API_MODEL_AVAILABLE + static_cast<unsigned>( choice.state)) ; // resource状態
				SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_ADDSTRING, 0,
									reinterpret_cast<LPARAM>( text.c_str())) ; // 同期借用
				// 選択ID消失でも和集合へ残した同じIDを選択します
				if( choice.id == selected_) {
					SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_SETCURSEL, index, 0) ;
				}
				++index ;
			}
			report_ = resource( event.error == ErrorKind::CANCELLED							 ? IDS_API_CATALOG_CANCELLED
							   : result->code && keyApplied_ && result->request.settingsOnly ? IDS_API_PARTIAL_APPLY
							   : result->code				  ? IDS_API_CATALOG_SAVE_FAILED
							   : result->request.settingsOnly ? IDS_API_SETTINGS_SAVED
							   : !result->request.localOnly
								   ? (catalog_.modelMeta.error || catalog_.priceMeta.error ? IDS_API_CATALOG_FAILED
																						   : IDS_API_CATALOG_READY)
								   : IDS_API_READY) ;
			// キー段成功後もモデル保存失敗は明示し、全適用成功とは扱いません
			if( result->request.settingsOnly) {
				settingsSaveFailed_ = result->code != 0 || !event.succeeded ;
				settingsApplySucceeded_ = !settingsSaveFailed_ ;
				pendingSettings_.reset() ;
				keyApplied_ = false ;
				persistEdited_ = false ;
			}
			selectModel() ;
			// 全適用成功後だけ新しいキーのModelsを更新し、価格GETを重複しません
			if( result->request.settingsOnly && !result->code && refreshAfterApply_ && keyReady_) {
				refreshAfterApply_ = false ;
				metadataFromSettings_ = true ;
				refresh( false) ;
			}
			// metadata取得は本文送信をせず、適合時だけ確認全文を準備します
			if( opened_ && request_.source) {
				start( request_.catalogSnapshot.empty() ? ApiTextAction::PREVIEW : ApiTextAction::PREPARE) ;
			} // 閲覧は期限やキーから独立
		}
		// metadataのtyped終端がない失敗/取消しでも取得中表示を終端案内へ戻します
		if( !result && catalogRefresh_ && settingsOpened_) {
			report_ =
				resource( event.error == ErrorKind::CANCELLED ? IDS_API_CATALOG_CANCELLED : IDS_API_CATALOG_FAILED) ;
		}
		// typed ACKなしの取消し/例外でも未保存の一般設定をcleanへ変えません
		if( !result && pendingSettings_ && settingsOpened_ && applyGeneration_ == windowGeneration_) {
			settingsSaveFailed_ = true ;
			pendingSettings_.reset() ;
			// 明示保存再試行の成功まではmetadata更新も保留します
			report_ = resource( IDS_API_CATALOG_SAVE_FAILED) ;
		}
		// runner開始前の取消しでも待機表示を完了させ閲覧を維持します
		if( !result && opened_) {
			report_ = resource( IDS_API_FAILED) + L" code=" + std::to_wstring( event.code) ;
			start( ApiTextAction::PREVIEW) ;
		}
		update() ;
		// 取得途中のreopenは終端回収後に一回だけ更新します
		if( pendingRefresh_ && (opened_ || settingsOpened_ || audioSettingsOpened_)) {
			refresh( true) ;
		}
		return true ;
	}
	// typed終端がない例外は秘密なしの固定診断にします
	if( !event.apiText) {
		applyingKey_ = false ;
		pendingSettings_.reset() ;
		refreshAfterApply_ = false ;
		report_ = resource( IDS_API_FAILED) + L" code=" + std::to_wstring( event.code) ;
		// close/reopen後の取消し終端でも現在状態を一回だけ取り直します
		if( pendingRefresh_ && settingsOpened_ && !stopped_) {
			metadataFromSettings_ = true ;
			refresh( true) ;
		}
		update() ;
		return true ;
	}
	const auto &result = *event.apiText ; // 同じqueueで受領済みの所有終端
	// 通常行の終端は確認/候補Windowを作らず公開状態だけを同期します
	if( direct_) {
		// 認証確認後の世代と適用設定を次要求へ継承します
		if( result.catalog) {
			catalog_ = *result.catalog ;
			selected_ = catalog_.settings.model ;
		}
		// 同候補の保存再試行にHTTPを使いません
		if( result.persistence.operationId) {
			request_.record = result.persistence.record ;
			request_.key = result.persistence.key ;
		}
		report_ = apiTextEventStatus( event).text() ;
		update() ;
		return true ;
	}
	report_ = resource( IDS_API_READY) ;
	// 一回の事前概算と予算は実課金と分けて示します
	if( result.draft) {
		report_ += L" " + std::to_wstring( result.draft->projection.size()) + L" bytes / " +
				   std::to_wstring( result.draft->chunks.size()) + L" chunks; " + resource( IDS_API_ESTIMATE) + L" $" +
				   std::to_wstring( static_cast<double>( result.draft->estimate) / 1000000.0) + L" / " +
				   resource( IDS_API_BUDGET) + L" $" +
				   std::to_wstring( static_cast<double>( result.draft->budget) / 1000000.0) ;
	}
	// known usageと課金不明は候補の成功/保存と別に表示します
	if( result.persistence.operationId) {
		const auto &record = result.persistence.record ; // 最大32chunkだけのmetadata
		report_ = resource( record.saveFailed ? IDS_API_SAVE_FAILED : IDS_API_SAVED) ;
		// 全結果の内訳を合計せず各chunkのpresenceを表示します
		if( record.latest) {
			report_ +=
				L"\r\n" + apiTextRecordStatus( record, event.id, event.error == ErrorKind::CANCELLED, event.code).text() ;
			// 採用保存の成功は未採用候補の表示と明確に分けます
			if( result.persistence.committed && record.adopted && sameApiTextAttempt( record.latest, record.adopted)) {
				report_ += L"\r\n" + resource( IDS_API_ADOPTED_SAVED) ;
			}
			std::size_t chunkIndex = 0 ; // 確認時の予約plan位置
			for( const auto &chunk : record.latest->results) { // 有限の実usage
				report_ += L"\r\nHTTP=" + std::to_wstring( chunk.httpStatus) + L" code=" + std::to_wstring( chunk.code) +
						   L" " + resource( IDS_API_NOT_SENT + static_cast<unsigned>( chunk.billing)) ;
				const auto cost = chunk.priceApplicable
									  ? apiTextUsageCost( chunk.usage, record.latest->draft.catalogSnapshot)
									  : std::nullopt ; // 不明を0へしません
				report_ += L" " + resource( IDS_API_INPUT_TOKENS) + L"=" +
						   (chunk.usage.input ? std::to_wstring( *chunk.usage.input) : L"?") + L" " +
						   resource( IDS_API_OUTPUT_TOKENS) + L"=" +
						   (chunk.usage.output ? std::to_wstring( *chunk.usage.output) : L"?") + L" " +
						   resource( IDS_API_CACHED_TOKENS) + L"=" +
						   (chunk.usage.cached ? std::to_wstring( *chunk.usage.cached) : L"?") + L" " +
						   resource( IDS_API_REASONING_TOKENS) + L"=" +
						   (chunk.usage.reasoning ? std::to_wstring( *chunk.usage.reasoning) : L"?") + L" " +
						   resource( IDS_API_CACHE_WRITES) ;
				// 実tier未通知や実scope不一致は参考額と別に予約額を表示します
				if( !record.latest->draft.catalogSnapshot.empty() && (!chunk.serviceTier || !chunk.priceApplicable) &&
				   chunkIndex < record.latest->draft.chunks.size()) {
					report_ += L" " + resource( IDS_API_UNKNOWN_RESERVATION) + L" $" +
							   std::to_wstring( static_cast<double>( record.latest->draft.chunks[chunkIndex].estimate) /
											   1000000.0) ;
				}
				++chunkIndex ;
				report_ += L" " + resource( chunk.priceApplicable ? IDS_API_TIER_MISSING : IDS_API_SCOPE_UNKNOWN) ;
				// 実tier通知はpresenceのまま表示します
				if( chunk.serviceTier) {
					report_ += L" " + toWide( *chunk.serviceTier) ;
				}
				// usageに基づく概算範囲を実billing額と呼びません
				if( cost) {
					report_ += L" " + resource( IDS_API_COST_RANGE) + L" $" +
							   std::to_wstring( static_cast<double>( cost->first) / 1000000.0) + L".." +
							   std::to_wstring( static_cast<double>( cost->second) / 1000000.0) ;
				}
			}
		}
	}
	// 同じ原文の準備結果だけを確認領域へ全量表示します
	if( result.draft) {
		request_.key = result.draft->source ;
		request_.draft = result.draft ;
		request_.budget = result.draft->budget ;
		SetDlgItemTextW( window_, IDC_API_PREVIEW, result.preview.c_str()) ;
		keyReady_ = result.keyReady ;
		SetDlgItemTextW( window_, IDC_API_CANDIDATE, result.candidate.c_str()) ;
	}
	// 未取得・期限超過でも閲覧用の全文と保存済み候補は表示します
	if( result.request.action == ApiTextAction::PREVIEW) {
		request_.key = result.request.key ;
		SetDlgItemTextW( window_, IDC_API_PREVIEW, result.preview.c_str()) ;
		SetDlgItemTextW( window_, IDC_API_CANDIDATE, result.candidate.c_str()) ;
	}
	// キー値を再表示せず、設定済み状態だけ受領します
	if( result.request.action == ApiTextAction::SET_KEY || result.request.action == ApiTextAction::ERASE_KEY) {
		keyReady_ = result.keyReady ;
		keyPersisted_ = result.keyPersisted ;
		// 保存方式だけの失敗は編集を保持し、成功した方式だけをcleanにします
		if( applyingKey_ && event.succeeded && !result.code) {
			persistEdited_ = false ;
		}
		request_.draft.reset() ;
		request_.catalogSnapshot.clear() ;
		// キーrevisionを受領し古いModels資格を失効します
		if( result.keyRevision) {
			catalog_.keyRevision = result.keyRevision ;
			catalog_.modelsKeyRevision = 0 ;
		}
		// 一回Applyはキー成功後にだけ一般設定保存へ進みます
		if( applyingKey_) {
			applyingKey_ = false ;
			// close/stale/取消し/stopと失敗は次段を開始しません
			if( event.succeeded && !result.code && !stopped_ && settingsOpened_ && pendingSettings_ &&
			   applyGeneration_ == windowGeneration_) {
				keyApplied_ = true ;
				submitSettings() ;
			} else {
				pendingSettings_.reset() ;
				refreshAfterApply_ = false ;
				report_ = resource( IDS_API_FAILED) ;
			}
			// 旧世代終端を回収した後のreopenだけ、一回更新します
			if( pendingRefresh_ && settingsOpened_ && !active_ && !stopped_) {
				metadataFromSettings_ = true ;
				refresh( true) ;
			}
			update() ;
			return true ;
		}
		// 旧test adapterの単独キー操作だけがModelsを一回更新します
		if( keyReady_ && settingsOpened_ && catalogSubmit_) {
			metadataFromSettings_ = true ;
			refresh( false) ;
		}
	}
	// 保存失敗でも候補ownerを保持し、再保存はHTTP0です
	if( result.persistence.operationId) {
		request_.record = result.persistence.record ;
		SetDlgItemTextW( window_, IDC_API_CANDIDATE, result.candidate.c_str()) ;
		// 明示送信の終端は取得した改善案へ移り、本文を確認してから採用します
		if( result.request.action == ApiTextAction::SEND && !result.candidate.empty()) {
			TabCtrl_SetCurSel( GetDlgItem( window_, IDC_API_TABS), 1) ;
			showTab() ;
		}
	}
	update() ;
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 有界状態だけからcontrolを更新します
 * @see koefude::ApiTextCorrectionWindow::update
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::update()
{
	// 未作成画面を操作しません
	if( !window_ && !settingsWindow_) {
		return ;
	}
	choices_ = apiCatalogChoices( catalog_, editingModel_) ; // 期限到来は自動GETを始めません
	std::wstring metadata = resource( IDS_API_CATALOG_SOURCE) ; // 固定公開出典
	metadata += L"\r\n" + resource( IDS_API_PRICE_TIME) + L"=" + catalogTime( catalog_.priceMeta.fetched) + L" / " +
				catalogStatus( catalog_.priceMeta, false) + L" / HTTP=" + std::to_wstring( catalog_.priceMeta.http) +
				L" code=" + std::to_wstring( catalog_.priceMeta.error) ;
	metadata += L"\r\n" + resource( IDS_API_MODEL_TIME) + L"=" + catalogTime( catalog_.modelMeta.fetched) + L" / " +
				catalogStatus( catalog_.modelMeta, true) + L" / HTTP=" + std::to_wstring( catalog_.modelMeta.http) +
				L" code=" + std::to_wstring( catalog_.modelMeta.error) ;
	bool eligible = false ; // 現在鮮度で再確認
	bool previous = false ; // 前回値の明示送信表示
	// 選択modelの全単価と理由を同じ正本から表示します
	for( const auto &choice : choices_) {
		// 他IDの価格を選択へ流用しません
		if( choice.id == selected_) {
			eligible = choice.state == ApiModelAvailability::AVAILABLE ;
			previous = choice.previous ;
			metadata += L"\r\n" + toWide( choice.id) + L" / " +
						resource( IDS_API_MODEL_AVAILABLE + static_cast<unsigned>( choice.state)) ;
			const auto model = catalog_.models.find( selected_) ; // 将来の終了日も警告表示
			// 終了日未通知を勝手に補いません
			if( model != catalog_.models.end() && !model->second.shutdown.empty()) {
				metadata += L" / " + resource( IDS_API_SHUTDOWN_DATE) + L"=" + toWide( model->second.shutdown) ;
			}
		}
	}
	const auto price = catalog_.prices.find( editingModel_) ; // 設定の未適用選択に対応する表示単価
	// 取得済み行があればmissingも個別表示します
	if( price != catalog_.prices.end()) {
		metadata += L"\r\n" + resource( IDS_API_PRICE_COLUMNS) ;
		// Shortの5つの単価だけを同じ順に表示します
		for( std::size_t i = 0; i < 5; ++i) {
			metadata += L" / " + (price->second.rates[i]
									  ? std::to_wstring( static_cast<double>( *price->second.rates[i]) / 1000000.0)
									  : L"?") ;
		}
	}
	detail_ = resource( IDS_API_NOTICE) + L"\r\n" + metadata + L"\r\n" + report_ ;
	std::wstring compact = resource( IDS_API_PRICE_TIME) + L": " + catalogTime( catalog_.priceMeta.fetched) + L" / " +
						   catalogStatus( catalog_.priceMeta, false) ;
	// 公開価格の失敗codeを同じ設定へ示し、キー未設定を価格失敗の理由にしません
	if( catalog_.priceMeta.error) {
		compact += L" (HTTP=" + std::to_wstring( catalog_.priceMeta.http) + L" code=" +
				   std::to_wstring( catalog_.priceMeta.error) + L")" ;
	}
	compact += L"\r\n" + resource( IDS_API_MODEL_TIME) + L": " +
			   (keyReady_ ? catalogStatus( catalog_.modelMeta, true) : resource( IDS_API_META_KEY_MISSING)) ;
	// 認証一覧の失敗は公開価格と区別し、本文や生応答を表示しません
	if( keyReady_ && catalog_.modelMeta.error) {
		compact += L" (HTTP=" + std::to_wstring( catalog_.modelMeta.http) + L" code=" +
				   std::to_wstring( catalog_.modelMeta.error) + L")" ;
	}
	compact += L"\r\n" + resource( IDS_API_RATE_SOURCE) ;
	compact += L"\r\n" + resource( IDS_API_RATE_POLICY) ;
	// 編集中モデルの利用可否も同じcatalogから表示します
	for( const auto &choice : choices_) {
		// 選択IDの検証済み利用可否だけを表示し、未知IDを有効にしません
		if( choice.id == editingModel_) {
			compact += L"\r\n" + resource( IDS_API_MODEL_AVAILABLE + static_cast<unsigned>( choice.state)) ;
		}
	}
	const auto table = GetDlgItem( settingsWindow_, IDC_API_PRICES) ; // 自分の標準一覧
	ListView_DeleteAllItems( table) ;
	// Standard Short/Longの二行を全五項目ずつ更新し、未掲載を0にしません
	for( int row = 0; row < 2; ++row) {
		auto	label = resource( row == 0 ? IDS_API_RATE_SHORT : IDS_API_RATE_LONG) ; // 期間の公式名
		LVITEMW item{} ; // 同期copyだけ
		item.mask = LVIF_TEXT ;
		item.iItem = row ;
		item.pszText = label.data() ;
		ListView_InsertItem( table, &item) ;
		// 五種類の料金をpresence付きでcatalogから読み、貼付け固定しません
		for( int column = 0; column < 5; ++column) {
			const auto	 index = static_cast<std::size_t>( row * 5 + column) ; // ratesの固定順
			std::wstring rate = resource( IDS_API_RATE_MISSING) ; // 未掲載と価格0を区別
			// 掲載値だけをUSD/100万tokenへ整形します
			if( price != catalog_.prices.end() && price->second.rates[index]) {
				rate = std::to_wstring( static_cast<double>( *price->second.rates[index]) / 1000000.0) ;
				// 有効な6桁小数の末尾0を除きます
				while( rate.find( L'.') != std::wstring::npos && rate.back() == L'0') {
					rate.pop_back() ;
				}
				// 小数点だけを残しません
				if( !rate.empty() && rate.back() == L'.') {
					rate.pop_back() ;
				}
			}
			ListView_SetItemText( table, row, column + 1, rate.data()) ;
		}
	}
	SetDlgItemTextW( settingsWindow_, IDC_API_METADATA, compact.c_str()) ;
	rebuilding_ = true ;
	SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_RESETCONTENT, 0, 0) ;
	// 単一catalog由来のIDと状態だけを表示します
	for( std::size_t i = 0; i < choices_.size(); ++i) {
		const auto text = toWide( choices_[i].id) ; // 選択IDはそのまま保持
		SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( text.c_str())) ;
		// 厳密IDの一致だけを選択します
		if( choices_[i].id == editingModel_) {
			SendDlgItemMessageW( settingsWindow_, IDC_API_MODEL, CB_SETCURSEL, i, 0) ;
		}
	}
	// 未編集の欄だけを固定長ダミーで描画し、実キーを再読しません
	if( !keyEdited_) {
		SetDlgItemTextW( settingsWindow_, IDC_API_KEY, keyReady_ ? L"●●●●●●●●" : L"") ;
	}
	// 保存checkboxの明示編集をmetadata更新で上書きしません
	if( !persistEdited_) {
		CheckDlgButton( settingsWindow_, IDC_API_PERSIST, keyPersisted_ ? BST_CHECKED : BST_UNCHECKED) ;
	}
	rebuilding_ = false ;
	SetDlgItemTextW( window_, IDC_API_SEND, resource( previous ? IDS_API_SEND_PREVIOUS : IDS_API_SEND_CURRENT).c_str()) ;
	const bool idle = active_ == 0 && !stopped_ ; // 明示受付可否
	auto	   report = report_ ; // 操作状態と保存出所を分ける所有表示
	// 現在comboの選択で保存済み候補のモデルを上書きしません
	if( request_.record.latest) {
		report +=
			L"\r\n" + apiTextProvenanceDisplay( *request_.record.latest,
											   sameApiTextAttempt( request_.record.latest, request_.record.adopted)) ;
	}
	// 前の採用Aと最新未採用Bを別々に表示します
	if( request_.record.adopted && !sameApiTextAttempt( request_.record.latest, request_.record.adopted)) {
		report += L"\r\n" + apiTextProvenanceDisplay( *request_.record.adopted, true) ;
	}
	// 表示選好が原文なら補正済みと説明しません
	if( request_.record.showOriginal) {
		report += L"\r\n" + resource( IDS_API_PROVENANCE_ORIGINAL) ;
	}
	detail_ += L"\r\n" + report ;
	auto compactReport =
		resource( active_					  ? IDS_API_WORKING
				 : request_.record.saveFailed ? IDS_API_SAVE_FAILED
				 : request_.record.latest ? IDS_API_STATE_PENDING + static_cast<unsigned>( request_.record.latest->state)
										  : IDS_API_READY) ; // 診断値は詳細だけ
	// 未設定でも巨大な空画面にせず設定への操作を示します
	if( !keyReady_ || !eligible) {
		compactReport = resource( IDS_API_SETUP_NEEDED) ;
	}
	SetDlgItemTextW( window_, IDC_API_REPORT, compactReport.c_str()) ;
	SetDlgItemTextW( settingsWindow_, IDC_API_REPORT, report_.c_str()) ;
	updateSettingsActivity() ;
	const bool candidateTab =
		window_ && TabCtrl_GetCurSel( GetDlgItem( window_, IDC_API_TABS)) == 1 ; // 現在確認している本文
	auto summary =
		targetName_ + L"\r\n" + resource( IDS_API_NEXT_MODEL) + L": OpenAI / " + toWide( selected_) ; // 表示だけのfile名
	// 候補tabでは現在設定と生成済み本文の出所を混同しません
	if( candidateTab && request_.record.latest) {
		const auto saved = apiTextProvenance( *request_.record.latest) ; // 保存値だけ
		summary = targetName_ + L"\r\n" + resource( IDS_API_REQUESTED_MODEL) + L": " +
				  apiProvenanceDisplayId( saved.requested) + L" / " + resource( IDS_API_ACTUAL_MODEL) + L": " +
				  apiProvenanceDisplayId( saved.actual) + L" / " +
				  resource( sameApiTextAttempt( request_.record.latest, request_.record.adopted)
							   ? IDS_API_PROVENANCE_ADOPTED
							   : IDS_API_PROVENANCE_CANDIDATE) ;
	}
	// 未送信の確認済み見積だけを常設します
	if( !candidateTab && request_.draft) {
		summary += L" / " + resource( IDS_API_ESTIMATE) + L" $" +
				   std::to_wstring( static_cast<double>( request_.draft->estimate) / 1000000.0) ;
	}
	SetDlgItemTextW( window_, IDC_API_SUMMARY, summary.c_str()) ;
	SetDlgItemTextW( settingsWindow_, IDC_API_KEY_STATE,
					resource( keyReady_ ? IDS_API_KEY_READY : IDS_API_KEY_MISSING).c_str()) ;
	// 操作に必要な状態だけでenableを決めます
	for( const int id : {IDC_API_SET_KEY, IDC_API_ERASE_KEY, IDC_API_PLAN, IDC_API_BUDGET, IDC_API_KEY, IDC_API_PERSIST,
						IDC_API_ORIGINAL, IDC_API_CORRECTED, IDC_API_MODEL, IDC_API_REFRESH}) {
		EnableWindow( GetDlgItem( settingsWindow_, id), idle) ;
	}
	EnableWindow( GetDlgItem( settingsWindow_, IDC_API_SETTINGS_APPLY), idle && settingsDirty()) ;
	EnableWindow( GetDlgItem( window_, IDC_API_SEND),
				 idle && !candidateTab && keyReady_ && (!catalogSubmit_ || eligible) && request_.draft &&
					 request_.draft->estimate <= request_.draft->budget && !request_.record.saveFailed) ;
	EnableWindow( GetDlgItem( window_, IDC_API_ADOPT),
				 idle && candidateTab && !request_.record.saveFailed && request_.record.latest &&
					 request_.record.latest->state == ApiTextState::CANDIDATE_READY) ;
	EnableWindow( GetDlgItem( window_, IDC_API_RETRY), idle && request_.record.saveFailed) ;
	EnableWindow( GetDlgItem( window_, IDC_API_DISCARD), idle && request_.record.latest != nullptr) ;
	EnableWindow( GetDlgItem( window_, IDC_API_CANCEL), active_ != 0) ;
	// hostはdirty/busyだけを参照し、同期通知により編集直後と保存ACK直後を描画します
	if( settingsEmbedded_ && IsWindow( settingsWindow_)) {
		SendMessageW( GetParent( settingsWindow_), K_SETTINGS_CHANGED_MESSAGE, 0,
					 reinterpret_cast<LPARAM>( settingsWindow_)) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 新規受付停止と取消しを要求します
 * @see koefude::ApiTextCorrectionWindow::stop
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::stop()
{
	stopped_ = true ;
	// 終了は同じworkerの協調取消しだけを要求します
	if( active_ && cancel_) {
		cancel_( active_) ;
	}
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 更新起動の失敗だけで終了を取り消す時にidle受付へ戻します
 * @note キー/設定/metadataを再読/変更/送信しません。UI thread専用です
 * @see koefude::ApiTextCorrectionWindow::resumeAfterUpdateFailure
 */
//------------------------------------------------------------------------------
void ApiTextCorrectionWindow::resumeAfterUpdateFailure() noexcept
{
	// 保存済みidleでの更新終了だけが対象で、実行中APIを再開しません
	if( !active_) {
		stopped_ = false ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 文章改善の文書dialogと設定dialogの通知を同じcontrollerへ振り分けます
 * @param[in] window このcontrollerが管理する文書または設定dialogの、通知中有効な借用HWND
 * @param[in] message 初期化・入力操作・描画・配置・終了を識別するWin32 message ID
 * @param[in] wparam WM_COMMANDでは下位wordが操作ID、上位wordがcontrol通知code
 *        WM_ERASEBKGND/WM_PRINTCLIENT/WM_CTLCOLORSTATICでは通知中だけ借用するHDC、
 *        WM_INITDIALOGではOSが既定focusを設定するcontrolのHWND。その他の通知では参照しません
 * @param[in,out] lparam WM_INITDIALOGではnon-nullの生存中ApiTextCorrectionWindow（this）
 *        処理対象のWM_NOTIFYでは読取り専用NMHDR、WM_DPICHANGEDでは読取り専用RECT、
 *        WM_GETMINMAXINFOでは最小追跡sizeを書き込むMINMAXINFOへの、通知中有効なnon-null pointer
 *        WM_CTLCOLORSTATICでは描画先controlの借用HWND。その他の通知では参照しません
 * @return 初期化時はTRUEを返して既定focusをOSへ任せます。通常の処理済み操作はTRUE、
 *         未処理・owner未登録・WM_NCDESTROYの後処理後・try内操作の例外処理後はFALSE
 *         埋込み設定のWM_CTLCOLORSTATICは、staticへの描画成功時にOS所有のstock NULL_BRUSHを
 *         INT_PTRへ変換して直接返します。static以外または描画不可時はNULL（0）で標準処理へ戻します
 * @pre 所有UI threadから呼び、初期登録したcontrollerはdialogの破棄完了まで同じaddressで生存します
 * @note 初期化時のthisをDWLP_USERへ非所有登録し、後続通知から借用します。借用pointerを解放しません
 *       埋込み設定のWM_ERASEBKGND/WM_PRINTCLIENTは描画成否をDWLP_MSGRESULTへ設定してTRUEを返します
 *       描画時は借用HDCの背景・文字色を変更します。stock NULL_BRUSHはNULL pointerとは異なり、削除禁止です
 *       try内操作の例外はreport_へ失敗説明を設定して再表示します。TRUEはworkerの処理/保存成功を表しません
 * @see koefude::ApiTextCorrectionWindow::dialogProc
 */
//------------------------------------------------------------------------------
INT_PTR CALLBACK ApiTextCorrectionWindow::dialogProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
	auto *self =
		reinterpret_cast<ApiTextCorrectionWindow *>( GetWindowLongPtrW( window, DWLP_USER)) ; // dialog寿命内borrow
	// 初期化時にだけcallerの所有pointerを登録します
	if( message == WM_INITDIALOG) {
		self = reinterpret_cast<ApiTextCorrectionWindow *>( lparam) ; // CreateDialogParam同期境界
		// 設定と文書のHWNDを混同しません
		if( GetDlgItem( window, IDC_API_KEY)) {
			self->settingsWindow_ = window ;
		} else {
			self->window_ = window ;
		}
		SetWindowLongPtrW( window, DWLP_USER, lparam) ;
		return TRUE ;
	}
	// 所有状態のない初期通知は標準処理へ返します
	if( !self) {
		return FALSE ;
	}
	// 親の終了で破棄されたchildを次回のHWNDへ読み替えません
	if( message == WM_NCDESTROY && window == self->settingsWindow_) {
		self->settingsWindow_ = nullptr ;
		self->settingsOpened_ = false ;
		return FALSE ;
	}
	try {
		// 統合設定の実childだけを同じ標準tabの背景へ合わせます
		if( window == self->settingsWindow_ && self->settingsEmbedded_) {
			const auto tab = GetDlgItem( GetParent( window), IDC_SETTINGS_TABS) ; // host所有tab
			// child全体の灰色背景をtab pageへ合わせます
			if( message == WM_ERASEBKGND || message == WM_PRINTCLIENT) {
				const bool painted = TabPageBackground::paint( tab, window, reinterpret_cast<HDC>( wparam)) ;
				SetWindowLongPtrW( window, DWLP_MSGRESULT, painted ? TRUE : FALSE) ;
				return TRUE ;
			}
			// readonly edit/buttonはhelperが拒否し、標準controlの描画を保持します
			if( message == WM_CTLCOLORSTATIC) {
				return reinterpret_cast<INT_PTR>(
					TabPageBackground::label( tab, reinterpret_cast<HWND>( lparam), reinterpret_cast<HDC>( wparam))) ;
			}
			// theme/system色更新時は子のラベルも新しい背景で再描画します
			if( message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {
				TabPageBackground::invalidate( window) ;
			}
		}
		// 文書の標準tabだけを切り替えます
		if( message == WM_NOTIFY && window == self->window_) {
			const auto *note = reinterpret_cast<const NMHDR *>( lparam) ; // 同期borrow
			// 原文と候補は同じgeometryで全文を表示します
			if( note && note->idFrom == IDC_API_TABS && note->code == TCN_SELCHANGE) {
				self->showTab() ;
				return TRUE ;
			}
		}
		// monitor変更は自分の推奨矩形だけへ適用します
		if( message == WM_DPICHANGED && window == self->window_) {
			const auto *suggested = reinterpret_cast<const RECT *>( lparam) ; // OS借用
			SetWindowPos( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
						 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE) ;
			self->layoutDocument() ;
			return TRUE ;
		}
		// resize/DPI変更後も全文領域と操作列を分離します
		if( message == WM_SIZE && window == self->window_) {
			self->layoutDocument() ;
			return TRUE ;
		}
		// 最小sizeは主操作が重ならない幅へ固定します
		if( message == WM_GETMINMAXINFO && window == self->window_) {
			auto	  *limits = reinterpret_cast<MINMAXINFO *>( lparam) ; // OS出力borrow
			const auto dpi = static_cast<int>( GetDpiForWindow( window)) ; // 現DPI
			limits->ptMinTrackSize = {MulDiv( 720, dpi, 96), MulDiv( 440, dpi, 96)} ;
			return TRUE ;
		}
		// 自分のcommandだけを固定操作へ変換します
		if( message == WM_COMMAND) {
			const auto id = LOWORD(wparam) ; // control識別子
			// モデル選択変更は確認済みdraftを即失効します
			if( id == IDC_API_MODEL && HIWORD(wparam) == CBN_SELCHANGE && !self->active_ && !self->rebuilding_) {
				const auto index = SendDlgItemMessageW( window, IDC_API_MODEL, CB_GETCURSEL, 0, 0) ; // 未適用選択
				// 画面の実選択だけを編集値へ保持します
				if( index >= 0 && static_cast<std::size_t>( index) < self->choices_.size()) {
					self->editingModel_ = self->choices_[static_cast<std::size_t>( index)].id ;
				}
				self->request_.draft.reset() ;
				self->request_.catalogSnapshot.clear() ;
				self->update() ;
				return TRUE ;
			}
			// 明示metadata更新だけを開始します
			if( id == IDC_API_REFRESH) {
				self->metadataFromSettings_ = true ;
				self->refresh( true) ;
				return TRUE ;
			}
			// 実入力編集とprogrammatic placeholder更新を区別します
			if( id == IDC_API_KEY && HIWORD(wparam) == EN_CHANGE && !self->rebuilding_) {
				self->keyEdited_ = true ;
				self->update() ;
				return TRUE ;
			}
			// 保存方式は利用者が明示変更した時だけキー要求へ反映します
			if( id == IDC_API_PERSIST && HIWORD(wparam) == BN_CLICKED && !self->rebuilding_) {
				self->persistEdited_ = true ;
				self->update() ;
				return TRUE ;
			}
			switch( id) {
				case IDC_API_SETTINGS_OPEN: // 同じcontrollerの独立設定へ移動します
					self->showSettings( GetParent( window), self->submit_, self->cancel_, self->catalogSubmit_) ;
					return TRUE ;
				case IDC_API_SETTINGS_APPLY:
					self->saveSettings() ;
					return TRUE ;
				case IDC_API_DETAILS: {
					const auto detail = self->detail_ ; // modal再入のupdateから表示本文を独立所有
					showMessageDialog( window, detail.c_str(), resource( IDS_API_SETTINGS_TITLE).c_str(), MB_OK) ;
					return TRUE ;
				}
				case IDC_API_MORE: { // 追加操作は明示した時だけ表示します
					const auto menu = CreatePopupMenu() ; // 自分の短いmenu
					// 作成失敗を成功扱いにしません
					if( !menu) {
						throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
					}
					const std::array ids{IDC_API_COPY,		IDC_API_SAVE,	 IDC_API_RETRY,	 IDC_API_ORIGINAL,
										 IDC_API_CORRECTED, IDC_API_DISCARD, IDC_API_DETAILS} ; // 追加操作
					// resource由来の短い項目を有限個だけ登録します
					for( std::size_t i = 0; i < ids.size(); ++i) {
						const bool enabled =
							!self->active_ &&
							(ids[i] != IDC_API_RETRY || self->request_.record.saveFailed) ; // 再保存は失敗時だけ
						AppendMenuW( menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), ids[i],
									resource( IDS_API_MORE_COPY + static_cast<unsigned>( i)).c_str()) ;
					}
					RECT button{} ; // popup位置
					GetWindowRect( GetDlgItem( window, IDC_API_MORE), &button) ;
					const auto chosen = TrackPopupMenuEx( menu, TPM_RETURNCMD | TPM_NONOTIFY, button.left, button.bottom,
														 window, nullptr) ; // 同期追跡
					DestroyMenu( menu) ;
					// 元controllerの固定sourceへだけ適用します
					if( chosen) {
						return dialogProc( window, WM_COMMAND, chosen, 0) ;
					}
					return TRUE ;
				}
				case IDC_API_COPY: // 明示copyは自分の候補controlだけへ適用
					SendDlgItemMessageW( window, IDC_API_CANDIDATE, EM_SETSEL, 0, -1) ;
					SendDlgItemMessageW( window, IDC_API_CANDIDATE, WM_COPY, 0, 0) ;
					return TRUE ;
				case IDC_API_SAVE: { // 利用者の標準保存dialogだけを開きます
					std::array<wchar_t, 32768> path{} ; // 明示出力先の有限buffer
					OPENFILENAMEW			   choice{} ; // 自画面の標準dialog契約
					choice.lStructSize = sizeof(choice) ;
					choice.hwndOwner = window ;
					choice.lpstrFilter = L"UTF-8 TXT\0*.txt\0\0" ;
					choice.lpstrFile = path.data() ;
					choice.nMaxFile = static_cast<DWORD>( path.size()) ;
					choice.lpstrDefExt = L"txt" ;
					choice.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST ;
					// 取消し時は何も保存しません
					if( GetSaveFileNameW( &choice)) {
						self->request_.exportPath = path.data() ;
						self->start( ApiTextAction::EXPORT) ;
					}
					return TRUE ;
				}
				case IDC_API_PLAN:
					self->start( ApiTextAction::PREPARE) ;
					return TRUE ; // 再確認
				case IDC_API_SET_KEY:
					self->start( ApiTextAction::SET_KEY) ;
					return TRUE ; // worker I/O
				case IDC_API_ERASE_KEY:
					self->start( ApiTextAction::ERASE_KEY) ;
					return TRUE ; // 固定target削除
				case IDC_API_SEND:
					self->start( ApiTextAction::SEND) ;
					return TRUE ; // 一回だけの明示送信
				case IDC_API_ADOPT:
					self->start( ApiTextAction::ADOPT) ;
					return TRUE ; // 保存ack後だけ採用
				case IDC_API_DISCARD:
					self->start( ApiTextAction::DISCARD) ;
					return TRUE ; // 送信なし候補破棄
				case IDC_API_RETRY:
					self->start( ApiTextAction::RETRY_SAVE) ;
					return TRUE ; // HTTP0再保存
				case IDC_API_ORIGINAL:
					self->start( ApiTextAction::ORIGINAL) ;
					return TRUE ; // 原文選好
				case IDC_API_CORRECTED:
					self->start( ApiTextAction::CORRECTED) ;
					return TRUE ; // 採用本文
				case IDC_API_CANCEL: // 同じoperationだけ取消します
					if( self->active_ && self->cancel_) {
						self->cancel_( self->active_) ;
					}
					return TRUE ;
				case IDCANCEL:
					// Escと閉じるbuttonも×と同じく所有要求を協調取消しします
					if( self->active_ && self->cancel_ && ((window == self->settingsWindow_) == self->activeSettings_)) {
						self->cancel_( self->active_) ;
					}
					// 閉じた設定のmask入力は再表示せず消去します
					if( window == self->settingsWindow_) {
						self->discardSettingsEdits() ;
						self->settingsOpened_ = false ;
						self->settingsPageVisible_ = false ;
						self->updateSettingsActivity() ;

					} else {
						self->opened_ = false ;
					}
					// 他画面所有のSendへclose世代変更を混ぜません
					if( !self->active_ || ((window == self->settingsWindow_) == self->activeSettings_)) {
						++self->windowGeneration_ ;
					}
					// 文書close自身だけが本文確認を閉じます
					if( window == self->window_) {
						self->request_.draft.reset() ;
					}
					ShowWindow( window, SW_HIDE) ;
					// 未適用編集を破棄後、既存文書の確認だけを再準備します（HTTP0）
					if( window == self->settingsWindow_ && !self->active_ && self->opened_ && self->request_.source) {
						self->selectModel() ;
						self->start( self->request_.catalogSnapshot.empty() ? ApiTextAction::PREVIEW
																		   : ApiTextAction::PREPARE) ;
					}
					return TRUE ; // 自画面だけを隠します
				default:
					break ; // 未知commandは標準処理
			}
		}
		// 閉じる操作は新規送信せず、自要求の取消しを伝えます
		if( message == WM_CLOSE) {
			// 進行中なら課金停止を保証せず同じ要求を取消します
			if( self->active_ && self->cancel_ && ((window == self->settingsWindow_) == self->activeSettings_)) {
				self->cancel_( self->active_) ;
			}
			// 設定closeは文書対象を差し替えません
			if( window == self->settingsWindow_) {
				self->discardSettingsEdits() ;
				self->settingsOpened_ = false ;
				self->settingsPageVisible_ = false ;
				self->updateSettingsActivity() ;

			} else {
				self->opened_ = false ;
			}
			// 別画面所有の進行中要求へ失効を混ぜません
			if( !self->active_ || ((window == self->settingsWindow_) == self->activeSettings_)) {
				++self->windowGeneration_ ;
			}
			// 文書close自身だけが確認本文を閉じます
			if( window == self->window_) {
				self->request_.draft.reset() ;
			}
			ShowWindow( window, SW_HIDE) ;
			// 閉じた未適用設定を使わず現在文書だけを再準備します
			if( window == self->settingsWindow_ && !self->active_ && self->opened_ && self->request_.source) {
				self->selectModel() ;
				self->start( self->request_.catalogSnapshot.empty() ? ApiTextAction::PREVIEW : ApiTextAction::PREPARE) ;
			}
			return TRUE ;
		}
		// 親破棄後の借用HWNDを残しません
		if( message == WM_NCDESTROY) {
			// 破棄された方のHWNDだけを解放します
			if( window == self->settingsWindow_) {
				self->settingsWindow_ = nullptr ;
			} else {
				self->window_ = nullptr ;
			}
			SetWindowLongPtrW( window, DWLP_USER, 0) ;
		}
	} catch( const OperationError &error) {
		self->report_ = resource( IDS_API_FAILED) + L" code=" + std::to_wstring( error.code()) ;
		self->update() ;
	} catch( ...) {
		self->report_ = resource( IDS_API_FAILED) ;
		self->update() ;
	}
	return FALSE ;
}
} // namespace koefude
