//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file control_accessibility.cpp
 * @brief Windows標準の目的名登録を所有し終了前に解除します
 * @version 1.0.0
 * @date 2026/09/27
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/27 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/27 thread・容量・目的名・解除の境界を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 保存済み結果の表示選択を含む8欄/9欄目拒否を接続
 * 0.1.0 Shunji Ogawa 2026/10/19 宣言の役割を明記
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
#include <windows.h>

// SDKのPROPID_ACC_NAME実体をこの翻訳単位だけで定義する
#include <initguid.h>

#include "app/control_accessibility.h"
#include <algorithm>
#include <cstddef>
#include <string>

// Win32基本型の後にCOM作成APIを参照する
#include <objbase.h>
#include <oleacc.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 部分初期化時にも残存登録を解除します。通常はclearで先に診断します
 * @see koefude::ControlAccessibility::~ControlAccessibility
 */
//------------------------------------------------------------------------------
ControlAccessibility::~ControlAccessibility()
{
	// 通常のWM_DESTROYでは既に診断済み。部分作成失敗の残存登録も回収する
	static_cast<void>( clear()) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 設定値と別にcontrolの目的名を付与・更新します
 * @param[in] window 同じprocess/UI threadの有効な非所有HWND。clearまで存続すること
 * @param[in] name NULL文字を含まない1～128 UTF-16単位の名前。APIがcopyします
 * @return S_OKは登録成功。失敗は入力・thread・容量・COMのHRESULTです
 * @pre 呼出しthreadのSTAが初期化済みであること
 * @note 新規登録は8欄まで。失敗したAPI呼出しもclearで解除を試みます
 * @see koefude::ControlAccessibility::setName
 */
//------------------------------------------------------------------------------
HRESULT ControlAccessibility::setName( HWND window, const std::wstring &name) noexcept
{
	// 無効handleと空・過大・途中終端の名前はCOMへ渡さない
	if( !IsWindow( window) || name.empty() || name.size() > 128 || name.find( L'\0') != std::wstring::npos) {
		return E_INVALIDARG ;
	}
	DWORD		processId = 0 ; // 対象HWNDの所有process
	const DWORD threadId = GetWindowThreadProcessId( window, &processId) ; // HWND所有thread
	// 他process/threadの標準controlを変更しない
	if( threadId != GetCurrentThreadId() || processId != GetCurrentProcessId()) {
		return RPC_E_WRONG_THREAD ;
	}
	const auto end = windows_.begin() + static_cast<std::ptrdiff_t>( count_) ; // 有効列の終端
	const bool registered = std::find( windows_.begin(), end, window) != end ; // 翻訳更新か新規か
	// 同一HWNDの更新は容量を消費せず、新規9欄目だけを拒否する
	if( !registered && count_ == windows_.size()) {
		return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES) ;
	}
	// 目的名が必要なUIだけでWindows標準serviceを取得する
	if( !services_) {
		// accessibility property serviceを取得したHRESULT
		const HRESULT result = CoCreateInstance( __uuidof( CAccPropServices), nullptr, CLSCTX_INPROC_SERVER,
												IID_PPV_ARGS(services_.GetAddressOf())) ;
		// COM初期化・取得失敗を呼出し側の診断へ戻す
		if( FAILED(result)) {
			return result ;
		}
	}
	// APIが部分的に失敗した場合も終了時に解除を試みるため先にhandleを保持する
	if( !registered) {
		windows_[count_++] = window ;
	}
	return services_->SetHwndPropStr( window, static_cast<DWORD>( OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME,
									 name.c_str()) ;
}

//------------------------------------------------------------------------------
/**
 * @brief 生存中の全HWNDから目的名だけを解除します
 * @return S_OKは全解除成功。失敗は最初の解除HRESULTで、次回も保持します
 * @pre 登録と同じUI threadで、child HWNDの破棄前に呼ぶこと
 * @post 失敗時も全登録の解除を試み、所有列を空にします
 * @see koefude::ControlAccessibility::clear
 */
//------------------------------------------------------------------------------
HRESULT ControlAccessibility::clear() noexcept
{
	// 有効な登録を全て解除し、一欄の失敗で他欄のcleanupを省略しない
	for( std::size_t index = 0; index < count_; ++index) {
		// 現在のcontrolから目的名を解除したHRESULT
		const HRESULT result = services_->ClearHwndProps( windows_[index], static_cast<DWORD>( OBJID_CLIENT),
														 CHILDID_SELF, &PROPID_ACC_NAME, 1) ;
		// 最初の解除失敗だけを明示終了へ保持する
		if( FAILED(result) && SUCCEEDED(cleanupError_)) {
			cleanupError_ = result ;
		}
		windows_[index] = nullptr ;
	}
	count_ = 0 ;
	return cleanupError_ ;
}
} // namespace koefude
