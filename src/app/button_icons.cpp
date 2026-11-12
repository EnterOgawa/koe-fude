//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file button_icons.cpp
 * @brief 自作ICOを標準buttonとWindowへDPIに応じて設定します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 BCM_SETIMAGELISTによる文字付きアイコンを追加
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
#include "app/button_icons.h"
#include "app/resource.h"
#include "core/types.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief 操作buttonへ現在のDPIの画像を設定します
 * @param[in] window 操作buttonを所有する親Window。UI threadから呼び出すこと
 * @note 同期messageで画像を差し替えた後に旧image listを解放します
 * @throws OperationError 画像resource不足または標準control設定失敗
 * @see koefude::ButtonIcons::update
 */
//------------------------------------------------------------------------------
void ButtonIcons::update( const HWND window)
{
	const int					 dpi = static_cast<int>( GetDpiForWindow( window)) ; // 現在の描画DPI
	const int					 size = MulDiv( 16, dpi, 96) ; // 操作画像のpixel数
	const int					 margin = MulDiv( 5, dpi, 96) ; // 画像と文字の間隔
	constexpr std::array<int, 6> CONTROLS{IDC_OPEN,	  IDC_SAVE,	 IDC_COPY,
										  IDC_CANCEL, IDC_CLEAR, IDC_RENAME} ; // 画像列に対応する操作
	constexpr std::array<int, 6> RESOURCES{IDI_ADD,	   IDI_SAVE,  IDI_COPY,
										   IDI_CANCEL, IDI_CLEAR, IDI_RENAME} ; // 自作ICOの識別子
	// 各buttonの旧画像は、新画像の関連付けが成功するまで保持する
	for( std::size_t index = 0; index < CONTROLS.size(); ++index) {
		const std::unique_ptr<HICON__, IconDeleter> icon(
			static_cast<HICON>( LoadImageW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(RESOURCES[index]), IMAGE_ICON,
										  size, size, 0))) ; // 他DPIの共有cacheを使わない、今回size専用icon
		std::unique_ptr<_IMAGELIST, ImageListDeleter> next(
			ImageList_Create( size, size, ILC_COLOR32, 1, 0)) ; // 本関数からmemberへ移す所有画像列
		// 画像欠落や確保失敗を無言で無視しない
		if( !icon || !next || ImageList_AddIcon( next.get(), icon.get()) < 0) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		const BUTTON_IMAGELIST setting{
			next.get(), {margin, 0, margin, 0}, BUTTON_IMAGELIST_ALIGN_LEFT} ; // 同期API借用値
		// 成功した場合だけ所有resourceを差し替える
		if( !SendMessageW( GetDlgItem( window, CONTROLS[index]), BCM_SETIMAGELIST, 0,
						 reinterpret_cast<LPARAM>( &setting))) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		images_[index] = std::move( next) ;
	}
	// taskbarとcaptionの両方へ、現在sizeの所有アプリ画像を設定する
	for( const int kind : {ICON_SMALL, ICON_BIG}) {
		const int pixels = GetSystemMetricsForDpi( kind == ICON_SMALL ? SM_CXSMICON : SM_CXICON,
												  static_cast<UINT>( dpi)) ; // OS指定の画像幅
		std::unique_ptr<HICON__, IconDeleter> icon(
			static_cast<HICON>( LoadImageW( GetModuleHandleW( nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, pixels,
										  pixels, 0))) ; // DPIに応じて差し替える所有icon
		// resource欠落時は初期化失敗とし、汎用iconで成功扱いにしない
		if( !icon) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		SendMessageW( window, WM_SETICON, static_cast<WPARAM>( kind),
					 reinterpret_cast<LPARAM>( icon.get())) ; // Windowはmemberのiconを借用する
		appIcons_[kind == ICON_SMALL ? 0 : 1] = std::move( icon) ;
	}
}
} // namespace koefude
