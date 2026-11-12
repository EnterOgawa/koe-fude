//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file localization.cpp
 * @brief 言語別resourceの境界検証とユーザーのUI言語保存を実装します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 明示LANGIDの読取りとUI言語だけの保存を追加
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
#include "app/localization.h"
#include "app/resource.h"
#include <cstddef>

namespace koefude {
namespace {
thread_local UiLanguage currentLanguage = UiLanguage::JAPANESE ; // UI threadごとの選択
constexpr wchar_t		K_SETTINGS_KEY[] = L"Software\\koefude" ; // 音声を含まない設定専用key
//------------------------------------------------------------------------------
/**
 * @brief 固定の対応言語か確認します
 * @param[in] language 検証値
 * @return 対応言語ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool		supported( const UiLanguage language) noexcept
{
	return language == UiLanguage::JAPANESE || language == UiLanguage::ENGLISH ;
}
} // namespace
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語を返します
 * @return 初期値は日本語
 * @see koefude::uiLanguage
 */
//------------------------------------------------------------------------------
UiLanguage uiLanguage() noexcept
{
	return currentLanguage ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語を変更します
 * @param[in] language 対応するUI言語。未知値は拒否
 * @return 適用した場合true。失敗時は現在値を保持
 * @see koefude::setUiLanguage
 */
//------------------------------------------------------------------------------
bool setUiLanguage( const UiLanguage language) noexcept
{
	// 未定義LANGIDをOSの暗黙fallbackへ流さない
	if( !supported( language)) {
		return false ;
	}
	currentLanguage = language ;
	return true ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示言語の文字列を読みます
 * @param[in] id STRINGTABLEの識別子
 * @param[in] language 読み込む対応言語
 * @return 所有UTF-16文字列。未定義・破損resourceは空
 * @see koefude::resource(const UINT, const koefude::UiLanguage)
 */
//------------------------------------------------------------------------------
std::wstring resource( const UINT id, const UiLanguage language)
{
	// 整数resource名の範囲と対応言語を先に検証する
	if( id > 65535 || !supported( language)) {
		return {} ;
	}
	const HMODULE module = GetModuleHandleW( nullptr) ; // processが所有するmodule
	const HRSRC	  block = FindResourceExW( module, RT_STRING, MAKEINTRESOURCEW(id / 16 + 1),
										  static_cast<WORD>( language)) ; // 16文字列単位のblock
	// resource欠落を空文字列として呼出し側のcoverage検証へ伝える
	if( !block) {
		return {} ;
	}
	const auto	  bytes = SizeofResource( module, block) ; // 信頼するblock上限（byte）
	const HGLOBAL loaded = LoadResource( module, block) ; // 解放不要のmodule内resource
	const auto	 *data = static_cast<const wchar_t *>( LockResource( loaded)) ; // module寿命のborrow
	// UTF-16の不完全なblockを走査しない
	if( !data || bytes % sizeof(wchar_t) != 0) {
		return {} ;
	}
	const std::size_t units = bytes / sizeof(wchar_t) ; // 走査可能code unit数
	std::size_t		  offset = 0 ; // 次のlength prefix位置
	// 目的のentryまで、各lengthと残量を照合して有界に進む
	for( UINT entry = 0; entry <= id % 16; ++entry) {
		// length prefixが範囲外なら破損として拒否する
		if( offset >= units) {
			return {} ;
		}
		const std::size_t length = data[offset++] ; // 終端を含まない文字数
		// 加算overflowを避け、残りcode unit数で検証する
		if( length > units - offset) {
			return {} ;
		}
		// 指定されたentryだけを所有文字列へcopyする
		if( entry == id % 16) {
			return std::wstring( data + offset, length) ;
		}
		offset += length ;
	}
	return {} ;
}
//------------------------------------------------------------------------------
/**
 * @brief 現threadのUI言語で文字列を読みます
 * @param[in] id STRINGTABLEの識別子
 * @return 所有UTF-16文字列。未定義resourceは空
 * @see koefude::resource(const UINT)
 */
//------------------------------------------------------------------------------
std::wstring resource( const UINT id)
{
	return resource( id, currentLanguage) ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示言語の標準menuを作成します
 * @param[in] language 対応するUI言語
 * @return 成功時は呼出し側がDestroyMenuする所有menu。失敗時NULL
 * @see koefude::loadUiMenu
 */
//------------------------------------------------------------------------------
HMENU loadUiMenu( const UiLanguage language) noexcept
{
	// 未定義言語のmenuをOSの推測で表示しない
	if( !supported( language)) {
		return nullptr ;
	}
	const HMODULE module = GetModuleHandleW( nullptr) ; // process所有module
	const HRSRC	  block = FindResourceExW( module, RT_MENU, MAKEINTRESOURCEW(IDR_MAIN_MENU),
										  static_cast<WORD>( language)) ; // 固定menu定義
	// 欠落resourceからmenuを作らない
	if( !block) {
		return nullptr ;
	}
	const auto *data =
		static_cast<const MENUTEMPLATEW *>( LockResource( LoadResource( module, block))) ; // module寿命の信頼済みtemplate
	return data ? LoadMenuIndirectW( data) : nullptr ;
}
//------------------------------------------------------------------------------
/**
 * @brief 保存済みUI言語を読みます
 * @return 未設定・不正値は日本語
 * @see koefude::readUiLanguage
 */
//------------------------------------------------------------------------------
UiLanguage readUiLanguage() noexcept
{
	DWORD		  value = 0 ; // 保存済みLANGID
	DWORD		  size = sizeof(value) ; // registry出力bufferのbyte数
	const LSTATUS status = RegGetValueW( HKEY_CURRENT_USER, K_SETTINGS_KEY, L"UiLanguage", RRF_RT_REG_DWORD, nullptr,
										&value, &size) ; // ユーザー設定だけを読み取る
	return status == ERROR_SUCCESS && value == static_cast<DWORD>( UiLanguage::ENGLISH) ? UiLanguage::ENGLISH
																					   : UiLanguage::JAPANESE ;
}
//------------------------------------------------------------------------------
/**
 * @brief 明示されたUI言語だけをユーザー設定へ保存します
 * @param[in] language 保存する対応言語
 * @return 成功時ERROR_SUCCESS、失敗時Win32 code。認識データは保存しません
 * @see koefude::saveUiLanguage
 */
//------------------------------------------------------------------------------
LSTATUS saveUiLanguage( const UiLanguage language) noexcept
{
	// 未定義値を永続化しない
	if( !supported( language)) {
		return ERROR_INVALID_PARAMETER ;
	}
	HKEY		  key = nullptr ; // 成功時に本関数が閉じる所有key
	const LSTATUS created = RegCreateKeyExW( HKEY_CURRENT_USER, K_SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
											&key, nullptr) ; // 管理者権限を要求しない作成
	// 開けない場合は診断を返し、この起動中の選択は保持する
	if( created != ERROR_SUCCESS) {
		return created ;
	}
	const DWORD	  value = static_cast<DWORD>( language) ; // 音声・本文・pathを含まない設定値
	const LSTATUS written = RegSetValueExW( key, L"UiLanguage", 0, REG_DWORD, reinterpret_cast<const BYTE *>( &value),
										   sizeof(value)) ; // 同期APIへbyte列を借用
	const LSTATUS closed = RegCloseKey( key) ; // 書込みの成否にかかわらず所有keyを閉じる
	return written == ERROR_SUCCESS ? closed : written ;
}
} // namespace koefude
