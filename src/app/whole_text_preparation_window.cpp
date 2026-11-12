//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_preparation_window.cpp
 * @brief 準備snapshotと不足時の対応を推論なしで表示します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1279の補正準備入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 callback失敗表示を当該呼出しだけに限定し再入を防止
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示モデル導入と同一枠の操作排他を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の専用取得と事前案内を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
 * 0.1.0 Shunji Ogawa 2026/10/21 ADR1314の固定Ministral16GB契約を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1343の文章専用補正を接続
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380のopen/close通知gateを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実使用の標準headerを直接include
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415の関数契約と参照を実宣言へ束縛
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#include "app/whole_text_preparation_window.h"
#include "app/localization.h"
#include "app/resource.h"
#include "core/types.h"
#include "inference/whole_text_runtime.h"
#include "platform/win_helpers.h"
#include <optional>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <shobjidl.h>
#include <string>
#include <utility>
#include <wrl/client.h>

namespace koefude {
namespace {
//------------------------------------------------------------------------------
/**
 * @brief 所有要素をmetadataと診断codeで説明します
 * @param[in] title 用途resource
 * @param[in] element 検査snapshot
 * @return 選択可能な所有UTF-16説明
 */
//------------------------------------------------------------------------------
[[nodiscard]] std::wstring describe( UINT title, const WholeTextPreparationElement &element)
{
	auto text = resource( title) + L": " + resource( IDS_WTP_STATES + static_cast<UINT>( element.state)) ; // 状態名
	// 不足・拒否・失敗だけへ診断を付け、Ready/未検査を内部errorに見せません
	if( element.state != WholeTextPreparationState::READY && element.state != WholeTextPreparationState::UNCHECKED &&
	   element.code != 0) {
		text += L" [" + std::to_wstring( static_cast<unsigned>( element.error)) + L" / " + std::to_wstring( element.code) +
				L"]" ;
	}
	text += L"\r\n" ;
	// 保存先は検査で得た値だけを示します
	if( !element.effectivePath.empty()) {
		text += element.effectivePath.native() + L"\r\n" ;
	}
	// registry由来の固定metadataだけを有限件数で表示します
	for( const auto &model : element.models) { // 補助WhisperまたはGemma用途だけ
		text += toWide( model.id) + L" · " + model.file.native() + L"\r\n" ;
		text += resource( IDS_WTP_WEIGHT) + L" " + std::to_wstring( model.bytes) + L" byte\r\n" ;
		text += L"revision: " + toWide( model.revision) + L"\r\n" ;
		text += L"license: " + toWide( model.license) + L"\r\n" ;
		text += resource( IDS_PREP_SOURCE) + L" " + toWide( model.source) + L"\r\n" ;
	}
	return text + L"\r\n" ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 親がstopped確認後にUIとworkerを解放します
 * @see koefude::WholeTextPreparationWindow::~WholeTextPreparationWindow
 */
//------------------------------------------------------------------------------
WholeTextPreparationWindow::~WholeTextPreparationWindow()
{
	requestStop() ;
	controller_.reset() ; // 通常終了では親がstoppedを確認済みです
	// 親の破棄で既に消えたWindowを再破棄しません
	if( IsWindow( window_)) {
		DestroyWindow( window_) ;
	}
	// controlを破棄してからfontを解放します
	if( font_) {
		DeleteObject( font_) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief keyboard移動先を返します
 * @return 非所有HWND、未作成NULL
 * @see koefude::WholeTextPreparationWindow::handle
 */
//------------------------------------------------------------------------------
HWND WholeTextPreparationWindow::handle() const noexcept
{
	return window_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief worker終了を確認します
 * @return 未起動または終了時true
 * @see koefude::WholeTextPreparationWindow::stopped
 */
//------------------------------------------------------------------------------
bool WholeTextPreparationWindow::stopped() const noexcept
{
	return !controller_ || controller_->stopped() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 新規受付を停止し、joinせず取消しを要求します
 * @see koefude::WholeTextPreparationWindow::requestStop
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::requestStop()
{
	stopping_ = true ;
	// 未起動のworkerは作成しません
	if( controller_) {
		controller_->requestStop() ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 初期pathを所有し、検査を開始せず表示します
 * @param[in] owner 全寿命で有効な非NULL親。非表示親なら表示しません
 * @param[in] request 初回だけ採用するpathとCPU方式。モデルI/Oは行いません
 * @param[in] canStart 空可の親受付条件。UI threadだけで呼びます
 * @param[in] runner 初回だけ固定する同期処理。空は既定を維持します
 * @throws OperationError 標準Window/control作成失敗
 * @see koefude::WholeTextPreparationWindow::show
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::show( HWND owner, WholeTextPreparationRequest request, std::function<bool()> canStart,
									  WholeTextPreparationRunner runner)
{
	canStart_ = std::move( canStart) ;
	parentAvailable_ = !canStart_ || canStart_() ;
	// 終了取消し後は、前workerの終了を確認してから明示操作を再び許可します
	if( stopping_ && stopped()) {
		controller_.reset() ;
		active_ = 0 ;
		stopping_ = false ;
	}
	// 初回だけ所有pathを固定し、再表示で検査要求を増やしません
	if( !IsWindow( window_)) {
		// 本番の明示modeもworker作成前に同じ同期境界を固定します
		if( runner && !controller_) {
			runner_ = std::move( runner) ;
		}
		request_ = std::move( request) ;
		WNDCLASSW definition{} ; // 自appの標準class
		definition.lpfnWndProc = windowProc ;
		definition.hInstance = GetModuleHandleW( nullptr) ;
		definition.lpszClassName = L"KoeFudeWholeTextPreparation" ;
		definition.hCursor = LoadCursorW( nullptr, IDC_ARROW) ;
		definition.hbrBackground = GetSysColorBrush( COLOR_BTNFACE) ;
		// 同一processの再利用だけを許可します
		if( !RegisterClassW( &definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		window_ = CreateWindowExW( WS_EX_CONTROLPARENT, definition.lpszClassName, resource( IDS_WTP_TITLE).c_str(),
								  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 840, 640, owner,
								  nullptr, definition.hInstance, this) ;
		// 不完全なWindowを成功扱いにしません
		if( !window_) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
	}
	update() ;
	// 非表示の自process試験ではWindowを表示しません。前面化は行いません
	opened_ = true ; // HWND再利用と利用者のopen状態を分離します
	if( IsWindowVisible( owner)) {
		ShowWindow( window_, SW_SHOWNOACTIVATE) ;
	}
}
//------------------------------------------------------------------------------
/**
 * @brief 有限の標準controlを作成します
 * @see koefude::WholeTextPreparationWindow::initialize
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::initialize()
{
	// 選択、読取り専用説明、開始、取消しの標準controlだけを作成します
	for( const int id : {IDC_WTP_VARIANT, IDC_WTP_DETAILS, IDC_WTP_CHECK, IDC_WTP_CANCEL, IDC_WTP_WHISPER,
						IDC_WTP_IMPORT, IDC_WTP_DOWNLOAD}) { // 有限ID
		const bool	combo = id == IDC_WTP_VARIANT ; // CPU方式選択
		const bool	edit = id == IDC_WTP_DETAILS ; // 説明兼実段階欄
		const auto	name = combo ? L"COMBOBOX" : edit ? L"EDIT" : L"BUTTON" ; // OS class
		const DWORD style = combo  ? CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
							: edit ? ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP | WS_BORDER
								   : WS_TABSTOP | BS_PUSHBUTTON ; // 役割別style
		// ID値をWin32 control境界へ渡します。pointer所有権は移しません
		const auto	child =
			CreateWindowExW( 0, name, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
							reinterpret_cast<HMENU>( static_cast<INT_PTR>( id)), GetModuleHandleW( nullptr), nullptr) ;
		// control不足は明示失敗へ返します
		if( !child) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
	}
	const auto portable = resource( IDS_WTP_PORTABLE) ; // 同期SendMessage中だけ有効な文字列
	const auto avx2 = resource( IDS_WTP_AVX2) ; // 同期SendMessage中だけ有効な文字列
	SendDlgItemMessageW( window_, IDC_WTP_VARIANT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( portable.c_str())) ;
	SendDlgItemMessageW( window_, IDC_WTP_VARIANT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( avx2.c_str())) ;
	const auto ministral = resource( IDS_WTP_MINISTRAL16GB) ; // 固有評価方式の説明
	SendDlgItemMessageW( window_, IDC_WTP_VARIANT, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>( ministral.c_str())) ;
	static_cast<void>( wholeTextRuntimeDescriptor( request_.variant)) ; // 未知値を拒否
	SendDlgItemMessageW( window_, IDC_WTP_VARIANT, CB_SETCURSEL, static_cast<WPARAM>( request_.variant), 0) ;
	SendDlgItemMessageW( window_, IDC_WTP_DETAILS, EM_SETLIMITTEXT, 64 * 1024, 0) ;
	layout() ;
}
//------------------------------------------------------------------------------
/**
 * @brief MS ゴシックとcontrolを現在DPIへ配置します
 * @see koefude::WholeTextPreparationWindow::layout
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::layout()
{
	const auto dpi = GetDpiForWindow( window_) ; // 所有Windowの現在DPI
	// DPI変更時だけ指定fontを置き換えます
	if( fontDpi_ != dpi) {
		const auto next = CreateFontW( -MulDiv( 14, static_cast<int>( dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
									  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
									  FIXED_PITCH, L"ＭＳ ゴシック") ; // 所有font
		// font作成失敗を黙って続行しません
		if( !next) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// 旧fontを全controlから外します
		for( const auto id : {IDC_WTP_VARIANT, IDC_WTP_DETAILS, IDC_WTP_CHECK, IDC_WTP_CANCEL, IDC_WTP_WHISPER,
							 IDC_WTP_IMPORT, IDC_WTP_DOWNLOAD}) { // 固定control
			SendDlgItemMessageW( window_, id, WM_SETFONT, reinterpret_cast<WPARAM>( next), TRUE) ;
		}
		// 参照が外れた旧fontだけを解放します
		if( font_) {
			DeleteObject( font_) ;
		}
		font_ = next ;
		fontDpi_ = dpi ;
	}
	RECT area{} ; // 所有client座標
	GetClientRect( window_, &area) ;
	const int margin = MulDiv( 8, static_cast<int>( dpi), 96) ; // 小さい標準余白
	const int row = MulDiv( 30, static_cast<int>( dpi), 96) ; // control高
	const int width = std::max( 1L, area.right - 2 * margin) ; // client内幅
	MoveWindow( GetDlgItem( window_, IDC_WTP_VARIANT), margin, margin, width, row * 6, TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_DETAILS), margin, row + 2 * margin, width,
			   std::max( 1L, area.bottom - 3 * row - 6 * margin), TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_WHISPER), margin, area.bottom - 2 * row - 2 * margin, width / 3 - margin,
			   row, TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_DOWNLOAD), width / 3 + margin, area.bottom - 2 * row - 2 * margin,
			   width / 3 - margin, row, TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_IMPORT), 2 * width / 3 + margin, area.bottom - 2 * row - 2 * margin,
			   width / 3 - margin, row, TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_CHECK), margin, area.bottom - row - margin, width / 2 - margin, row, TRUE) ;
	MoveWindow( GetDlgItem( window_, IDC_WTP_CANCEL), width / 2 + margin, area.bottom - row - margin, width / 2 - margin,
			   row, TRUE) ;
}
//------------------------------------------------------------------------------
/**
 * @brief metadataと所有snapshotから表示だけを更新します
 * @see koefude::WholeTextPreparationWindow::update
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::update()
{
	// 未作成時と同期control通知による再入時は表示更新を省略します
	if( !IsWindow( window_) || updating_) {
		return ;
	}
	updating_ = true ;
	try {
#if defined(KOEFUDE_WINDOW_TESTING)
        ++updateCount_ ;
		// 自試験の一回だけ同じcallbackへ入り、guardが再更新を抑えることを測ります
		if( nestedCallbackOnce_) {
			nestedCallbackOnce_ = false ;
			callbackFailureOnce_ = true ;
			SendMessageW( window_, WM_COMMAND, 0, 0) ;
		}
#endif
        auto text = resource( IDS_WTP_INTRO) + L"\r\n\r\n" +
					resource( IDS_WTP_STAGES + static_cast<UINT>( event_.progress)) + L"\r\n" ; // 総量不明の実段階
		// 明示操作が拒否された時だけ理由を保持し、timerから新規通知しません
		if( refused_) {
			text += resource( IDS_WTP_BUSY) + L"\r\n" ;
		}
		// 実byteの段階内進捗だけを表示し、copy完了を導入完了と呼びません
		if( event_.totalBytes != 0) {
			text += std::to_wstring( event_.completedBytes) + L" / " + std::to_wstring( event_.totalBytes) + L" byte\r\n" ;
		}
		// 終端診断を省略せず表示します
		if( event_.code || event_.progress == WholeTextPreparationStage::FAILURE) {
			text += L"[" + std::to_wstring( static_cast<unsigned>( event_.error)) + L" / " +
					std::to_wstring( event_.code) + L"]\r\n" ;
		}
		// 成功した検査snapshotだけを同じ要求の結果として表示します
		if( event_.plan) {
			const auto &plan = *event_.plan ; // 所有通知の不変borrow
			text += resource( plan.ready ? IDS_WTP_READY : IDS_WTP_NOT_READY) + L"\r\n\r\n" ;
			text += describe( IDS_WTP_RUNTIME, plan.runtime) ;
			// 文章専用で未検査の補助モデルを不足として案内しません
			if( request_.use == WholeTextPreparationUse::AUDIO_ASSISTED) {
				text += describe( IDS_WTP_WHISPER, plan.whisper) ;
			}
			text += describe( IDS_WTP_GEMMA, plan.gemma) ;
			text += resource( IDS_PREP_STORAGE) + L"\r\n" + plan.request.cacheRoot.native() + L"\r\n" +
					plan.request.bundleRoot.native() + L"\r\n\r\n" ;
			// 前提拒否の原因と対応を区別して説明します
			if( plan.runtime.state == WholeTextPreparationState::MISSING) {
				text += resource( IDS_WHOLE_RUNTIME_MISSING) + L"\r\n" ;
			}
			// 対応しないCPUを自動で別variantへ変更しません
			else if( plan.runtime.state == WholeTextPreparationState::UNSUPPORTED) {
				text += resource( IDS_WHOLE_CPU_UNSUPPORTED) + L"\r\n" ;
			}
			// 構成差は配布folder全体の復元へ案内します
			else if( plan.runtime.state == WholeTextPreparationState::CORRUPT) {
				text += resource( IDS_WHOLE_RUNTIME_REJECTED) + L"\r\n" ;
			}
		} else { // 開いただけの三要素も明示的な未検査として表示します
			const auto preview = wholeTextPreparationPreview( request_) ; // metadataだけ、I/O0
			text += describe( IDS_WTP_RUNTIME, preview.runtime) ;
			// 文章専用の用途表示は固定Ministralだけです
			if( request_.use == WholeTextPreparationUse::AUDIO_ASSISTED) {
				text += describe( IDS_WTP_WHISPER, preview.whisper) ;
			}
			text += describe( IDS_WTP_GEMMA, preview.gemma) ;
		}
		text += L"\r\n" + resource( IDS_WTP_NEXT) ;
		// 元HTTP/API codeを保持したまま正規取得とlocal導入だけを案内します
		if( event_.code != 0 || (event_.plan && event_.plan->gemma.code != 0)) {
			text += L"\r\n" + resource( IDS_WTP_DOWNLOAD_FAILURE) ;
		}
		// 既存取得器と同じ通信先を明示操作より前に表示します
		for( const auto host : modelDownloadHosts()) { // 固定allowlistだけ
			text += L"\r\nhttps://" + std::wstring( host) ;
		}
		// 同一内容は再設定せず選択・scrollを保持します
		if( text != details_) {
			details_ = std::move( text) ;
			SetDlgItemTextW( window_, IDC_WTP_DETAILS, details_.c_str()) ;
		}
		SetDlgItemTextW( window_, IDC_WTP_CHECK, resource( IDS_WTP_CHECK).c_str()) ;
		SetDlgItemTextW( window_, IDC_WTP_CANCEL, resource( IDS_CANCEL).c_str()) ;
		SetDlgItemTextW( window_, IDC_WTP_WHISPER, resource( IDS_WTP_WHISPER_PREPARE).c_str()) ;
		SetDlgItemTextW( window_, IDC_WTP_IMPORT, resource( IDS_WTP_IMPORT).c_str()) ;
		SetDlgItemTextW( window_, IDC_WTP_DOWNLOAD, resource( IDS_WTP_DOWNLOAD).c_str()) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_DOWNLOAD), !active_ && !stopping_ && parentAvailable_) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_WHISPER), request_.use == WholeTextPreparationUse::AUDIO_ASSISTED &&
															   !active_ && !stopping_ && parentAvailable_) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_IMPORT), !active_ && !stopping_ && parentAvailable_) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_CHECK), !active_ && !stopping_ && parentAvailable_) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_VARIANT), request_.use == WholeTextPreparationUse::AUDIO_ASSISTED &&
															   !active_ && !stopping_ && parentAvailable_) ;
		EnableWindow( GetDlgItem( window_, IDC_WTP_CANCEL), active_ != 0 && !event_.terminal) ;
	} catch( ...) {
		// allocation等の失敗でもguardを解放し、元例外をcallbackへ戻します
		updating_ = false ;
		throw ;
	}
	updating_ = false ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示開始だけが一枠workerへ要求を送ります
 * @param[in] action 同じ一枠で行う固定操作
 * @see koefude::WholeTextPreparationWindow::start
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::start( WholeTextPreparationAction action)
{
	// 二重開始と親の停止後の開始を拒否します
	if( active_ || stopping_ || (canStart_ && !canStart_())) {
		refused_ = true ;
		update() ;
		return ;
	}
	// 検査workerは明示開始以外では起動しません
	if( !controller_) {
		controller_ = std::make_unique<WholeTextPreparationController>( runner_) ;
	}
	auto request = request_ ; // 操作中だけsourceを所有する要求
	request.action = action ;
	// picker取消しはoperationを作りません
	if( action == WholeTextPreparationAction::IMPORT_GEMMA) {
		const auto source = pickSource() ; // 標準dialogまたは自試験の同期fake
		// OS dialogを閉じただけなら元snapshotを保持します
		if( !source) {
			return ;
		}
		request.sourceFile = *source ;
	}
	// modal dialogから戻った後も同じ受付境界を再確認します
	if( active_ || stopping_ || (canStart_ && !canStart_())) {
		refused_ = true ;
		update() ;
		return ;
	}
	refused_ = false ;
	active_ = controller_->submit( std::move( request)) ;
	// 受付した要求の通知だけを表示します
	if( active_) {
		event_ = {} ;
		event_.id = active_ ;
	}
	update() ;
}
//------------------------------------------------------------------------------
/**
 * @brief 未受領終端を含む占有を返します
 * @return 新規準備/生成を拒否する間true
 * @see koefude::WholeTextPreparationWindow::busy
 */
//------------------------------------------------------------------------------
bool WholeTextPreparationWindow::busy() const noexcept
{
	return active_ != 0 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 標準file dialogからlocal file一件を選びます
 * @return 所有path。取消しはnullopt
 * @throws OperationError dialog失敗
 * @see koefude::WholeTextPreparationWindow::pickSource
 */
//------------------------------------------------------------------------------
std::optional<std::filesystem::path> WholeTextPreparationWindow::pickSource()
{
#if defined(KOEFUDE_WINDOW_TESTING)
    // 自試験processではOS dialog自動操作を行いません
	if( picker_) {
		return picker_() ;
	}
#endif
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog ; // UI apartment内の所有dialog
	auto status = CoCreateInstance( CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
								   IID_PPV_ARGS(&dialog)) ; // 標準一file選択
	// dialogを作れない場合は操作を開始しません
	if( FAILED(status)) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
	status = dialog->SetOptions( FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT |
								FOS_NOCHANGEDIR | FOS_NODEREFERENCELINKS) ;
	// Recentや作業directoryを変更する代替経路を使いません
	if( FAILED(status)) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
	const COMDLG_FILTERSPEC filter[] = {{L"GGUF", L"*.gguf"}} ; // 一種類の標準filter
	status = dialog->SetFileTypes( 1, filter) ;
	// filterを設定できない場合も曖昧なdialogを開きません
	if( FAILED(status)) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
	status = dialog->Show( window_) ;
	// 利用者の取消しは失敗通知も新しい操作も作りません
	if( status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		return std::nullopt ;
	}
	// OSの元HRESULTを保持します
	if( FAILED(status)) {
		throw OperationError( ErrorKind::INTERNAL, static_cast<std::uint32_t>( status)) ;
	}
	Microsoft::WRL::ComPtr<IShellItem> item ; // 選択結果の所有COM object
	status = dialog->GetResult( &item) ;
	// file以外の選択をrequestへ渡しません
	if( FAILED(status)) {
		throw OperationError( ErrorKind::INVALID_INPUT, static_cast<std::uint32_t>( status)) ;
	}
	PWSTR raw = nullptr ; // OSが割り当てるUnicode絶対path
	status = item->GetDisplayName( SIGDN_FILESYSPATH, &raw) ;
	const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> name( raw, CoTaskMemFree) ; // 所有解放
	// virtual itemやpath取得失敗はモデルI/O前に拒否します
	if( FAILED(status) || !name) {
		throw OperationError( ErrorKind::INVALID_INPUT, static_cast<std::uint32_t>( status)) ;
	}
	return std::filesystem::path( name.get()) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 自workerの最新DTOだけを短時間で回収します
 * @see koefude::WholeTextPreparationWindow::poll
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::poll()
{
	const bool available = !canStart_ || canStart_() ; // I/Oを含まないUI所有の受付条件
	// 条件が変わった時だけ操作可能表示を更新し、同じ拒否を反復表示しません
	if( available != parentAvailable_) {
		parentAvailable_ = available ;
		update() ;
	}
	// 起動・再描画から検査を開始しません
	if( !controller_) {
		return ;
	}
	const auto next = controller_->takeEvent() ; // 一件の所有通知
	// 未更新・未知IDを表示へ適用しません
	if( !next || next->id != active_) {
		return ;
	}
	event_ = *next ;
	// 終端受領後だけ次の明示開始を許可します
	if( event_.terminal) {
		active_ = 0 ;
	}
	update() ;
}
#if defined(KOEFUDE_WINDOW_TESTING)
//------------------------------------------------------------------------------
/**
 * @brief OS dialogを操作せず返却値だけを差し替えます
 * @param[in] picker 自process同期fake。nulloptは取消し
 * @see koefude::WholeTextPreparationWindow::setPickerForTesting
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::setPickerForTesting( std::function<std::optional<std::filesystem::path>()> picker)
{
	picker_ = std::move( picker) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 自process試験だけが実file検査を差し替えます
 * @param[in] runner worker開始前に固定する同期fake
 * @pre 未起動またはworker終了済みです
 * @see koefude::WholeTextPreparationWindow::setRunnerForTesting
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::setRunnerForTesting( WholeTextPreparationRunner runner)
{
	// 試験でも実行中依存の差替えを拒否します
	if( controller_ && !controller_->stopped()) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	controller_.reset() ;
	runner_ = std::move( runner) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 試験用の所有snapshotを参照します
 * @return UI instance寿命内のborrow
 * @see koefude::WholeTextPreparationWindow::eventForTesting
 */
//------------------------------------------------------------------------------
const WholeTextPreparationEvent &WholeTextPreparationWindow::eventForTesting() const noexcept
{
	return event_ ;
}
//------------------------------------------------------------------------------
/**
 * @brief 次の自callbackだけで人工例外を発生させます
 * @param[in] nested trueなら失敗表示中にも一回だけ自callbackを再入させます
 * @note 試験processのUI thread専用です。外部入力は操作しません
 * @see koefude::WholeTextPreparationWindow::failNextCallbackForTesting
 */
//------------------------------------------------------------------------------
void WholeTextPreparationWindow::failNextCallbackForTesting( bool nested) noexcept
{
	callbackFailureOnce_ = true ;
	nestedCallbackOnce_ = nested ;
}
//------------------------------------------------------------------------------
/**
 * @brief guardを通過した表示更新数を返します
 * @return UI threadの実測回数
 * @see koefude::WholeTextPreparationWindow::updateCountForTesting
 */
//------------------------------------------------------------------------------
std::uint64_t WholeTextPreparationWindow::updateCountForTesting() const noexcept
{
	return updateCount_ ;
}
#endif
//------------------------------------------------------------------------------
/**
 * @brief 準備Windowの生成・配置・明示操作をUI threadで振り分け、例外を準備失敗表示へ変換します
 * @param[in] window OSから渡される自WindowのHWND。GWLP_USERDATAにはWindow寿命中だけownerを借用します
 * @param[in] message WM_NCCREATE/CREATE、サイズ/DPI、command、close/破棄等のWin32通知種別です
 * @param[in] wparam commandではcontrol IDと通知code、サイズ/DPIではOS定義値です。種類に従ってだけ解釈します
 * @param[in] lparam WM_NCCREATEでは非null CREATESTRUCTWのlpCreateParamsからownerを借用します。WM_GETMINMAXINFOは書込先、
 *        WM_DPICHANGEDは同期借用RECTです。他はmessage固有値です
 * @return 処理済みの生成/配置/command/closeは0、WM_CREATE内の例外は-1、それ以外はDefWindowProcWの値です。0を準備/取得の完了と解釈しません
 * @note owner未登録時は標準処理へ返し、WM_NCDESTROYで借用を解除します。操作例外は固定error/codeへ記録し、表示更新失敗にも固定診断を残してC++例外を越えません
 */
//------------------------------------------------------------------------------
LRESULT CALLBACK WholeTextPreparationWindow::windowProc( HWND window, UINT message, WPARAM wparam,
														LPARAM lparam) noexcept
{
	// Win32同期borrowだけをWindow寿命に関連付けます
	auto *self = reinterpret_cast<WholeTextPreparationWindow *>( GetWindowLongPtrW( window, GWLP_USERDATA)) ;
	// 初期のCreateWindowEx呼出し中にだけ受け取る非所有pointerです
	if( message == WM_NCCREATE) {
		const auto *create = reinterpret_cast<const CREATESTRUCTW *>( lparam) ; // OSの同期borrow
		self = static_cast<WholeTextPreparationWindow *>( create->lpCreateParams) ;
		self->window_ = window ;
		SetWindowLongPtrW( window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( self)) ;
	}
	// 関連付け前のOS messageは標準処理へ返します
	if( !self) {
		return DefWindowProcW( window, message, wparam, lparam) ;
	}
	bool callbackFailed = false ; // この呼出しでcatchした場合だけ一回表示します
	try {
#if defined(KOEFUDE_WINDOW_TESTING)
        // 自process試験だけの一回例外です。再入前に必ず消費します
		if( self->callbackFailureOnce_) {
			self->callbackFailureOnce_ = false ;
			throw OperationError( ErrorKind::INTERNAL, 0x12790001) ;
		}
#endif
        // 自分の操作だけを処理し、モデル検査はworkerへ渡します
		switch( message) {
			case WM_CREATE:
				self->initialize() ;
				return 0 ; // 標準control作成
			case WM_SIZE:
				self->layout() ;
				return 0 ; // client寸法への配置
			case WM_GETMINMAXINFO: {
				auto	  *limits = reinterpret_cast<MINMAXINFO *>( lparam) ; // OS所有の出力
				const auto dpi = static_cast<int>( GetDpiForWindow( window)) ; // 現DPI
				limits->ptMinTrackSize = {MulDiv( 720, dpi, 96), MulDiv( 360, dpi, 96)} ;
				return 0 ; // 操作列の重なり防止
			}
			case WM_DPICHANGED: {
				const auto *next = reinterpret_cast<const RECT *>( lparam) ; // OS推奨矩形
				SetWindowPos( window, nullptr, next->left, next->top, next->right - next->left, next->bottom - next->top,
							 SWP_NOACTIVATE | SWP_NOZORDER) ;
				self->layout() ;
				return 0 ; // DPI追従
			}
			case WM_COMMAND:
				// 明示開始だけがhash検査を送信します
				if( LOWORD(wparam) == IDC_WTP_CHECK) {
					self->start() ;
					return 0 ;
				}
				// 専用Gemma取得も同じ一枠を占有し、表示だけでは実行しません
				if( LOWORD(wparam) == IDC_WTP_DOWNLOAD) {
					self->start( WholeTextPreparationAction::DOWNLOAD_GEMMA) ;
					return 0 ;
				}
				// 同じ一枠の専用buttonだけが取得・導入を開始します
				if( LOWORD(wparam) == IDC_WTP_WHISPER || LOWORD(wparam) == IDC_WTP_IMPORT) {
					self->start( LOWORD(wparam) == IDC_WTP_WHISPER ? WholeTextPreparationAction::PREPARE_WHISPER
																  : WholeTextPreparationAction::IMPORT_GEMMA) ;
					return 0 ;
				}
				// 現操作の取消しだけを要求します
				if( LOWORD(wparam) == IDC_WTP_CANCEL && self->controller_) {
					self->controller_->cancel( self->active_) ;
					return 0 ;
				}
				// CPU選択変更では検査せず、古いsnapshotを未検査へ戻します
				if( LOWORD(wparam) == IDC_WTP_VARIANT && HIWORD(wparam) == CBN_SELCHANGE && !self->active_ &&
				   !self->stopping_) {
					const auto choice = SendDlgItemMessageW( window, IDC_WTP_VARIANT, CB_GETCURSEL, 0, 0) ; // 明示選択
					// 三つの実登録項目だけを受け付け、未知選択は変更しません
					if( self->request_.use == WholeTextPreparationUse::TEXT_ONLY_CONTEXT || choice < 0 || choice > 2) {
						return 0 ;
					}
					self->request_.variant = static_cast<WholeTextRuntimeVariant>( choice) ;
					self->request_.runtimePath = self->request_.runtimePath.parent_path() /
												 wholeTextRuntimeDescriptor( self->request_.variant).dllName ;
					self->event_ = {} ;
					self->update() ;
					return 0 ;
				}
				break ; // その他の標準通知
			case WM_CLOSE:
				self->opened_ = false ;
				// Windowを隠しても親がworkerの寿命を保持します
				if( self->controller_) {
					self->controller_->cancel( self->active_) ;
				}
				ShowWindow( window, SW_HIDE) ;
				return 0 ;
			case WM_NCDESTROY:
				SetWindowLongPtrW( window, GWLP_USERDATA, 0) ;
				self->window_ = nullptr ;
				self->opened_ = false ;
				break ; // 同期borrow解除
			default:
				break ; // OS標準動作
		}
	} catch( const OperationError &error) {
		callbackFailed = true ;
		self->event_.progress = WholeTextPreparationStage::FAILURE ;
		self->event_.error = error.kind() ;
		self->event_.code = error.code() ;
		// 初期化失敗のWindowは成立させません
		if( message == WM_CREATE) {
			return -1 ;
		}
	} catch( ...) {
		callbackFailed = true ;
		self->event_.progress = WholeTextPreparationStage::FAILURE ;
		self->event_.error = ErrorKind::INTERNAL ;
		self->event_.code = ERROR_UNHANDLED_EXCEPTION ;
		// callback境界の未知例外も作成失敗として返します
		if( message == WM_CREATE) {
			return -1 ;
		}
	}
	// 当該callbackでcatchした場合だけ反映し、後続messageで反復更新しません
	if( callbackFailed) {
		try {
			self->update() ;
		} catch( ...) {
			// allocation不要のOS境界で最小の固定診断を残します
			SetDlgItemTextW( window, IDC_WTP_DETAILS, L"KoeFude: ERROR_UNHANDLED_EXCEPTION") ;
		}
	}
	return DefWindowProcW( window, message, wparam, lparam) ;
}
} // namespace koefude
