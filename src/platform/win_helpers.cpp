//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file win_helpers.cpp
 * @brief UIとengine間のUnicode変換を検証付きで実行します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 UTF-8/UTF-16の明示変換を追加
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
#include "platform/win_helpers.h"

#include <limits>

#include "core/types.h"

namespace koefude {

//------------------------------------------------------------------------------
/**
 * @brief 通信を伴うpathやreparse/cloud属性を読込み前に拒否します
 * @param[in] path 通常のlocal fileを指定するpath
 * @return 正規化した絶対path
 * @throws OperationError 不正path、remote drive、非通常file、同期属性
 * @note 各祖先directoryも確認します。OSのnamespace変更をlockするAPIではありません
 * @see koefude::validateLocalFile
 */
//------------------------------------------------------------------------------
std::filesystem::path validateLocalFile( const std::filesystem::path &path)
{
	const auto input = path.native() ; // shellへ渡さないUnicode入力値
	// UNC/device namespace、URL、空pathをfilesystem APIへ渡さない
	if( input.empty() || input.starts_with( L"\\\\") || input.find( L"://") != std::wstring::npos) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	const auto absolute = std::filesystem::absolute( path).lexically_normal() ; // local絶対path
	const auto root = absolute.root_path() ; // mapped network driveの照合用
	const auto drive = GetDriveTypeW( root.c_str()) ; // OSが返すdrive種別
	// drive形式でない入力、network drive、代替data streamを拒否する
	if( absolute.native().size() < 3 || absolute.native()[1] != L':' ||
	   absolute.native().find( L':', 2) != std::wstring::npos ||
	   (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK && drive != DRIVE_CDROM)) {
		throw OperationError( ErrorKind::INVALID_INPUT) ;
	}
	auto current = root ; // 上位から順に属性を確認する絶対path
	// 祖先のjunctionやcloud directoryを経由した暗黙のnetwork読込みも拒否する
	for( const auto &component : absolute.relative_path()) {
		current /= component ;
		const DWORD attributes = GetFileAttributesW( current.c_str()) ; // data読込み前の属性
		// 存在しないfileや属性照会失敗を元code付きで通知する
		if( attributes == INVALID_FILE_ATTRIBUTES) {
			throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
		}
		constexpr DWORD K_REJECTED = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
									 FILE_ATTRIBUTE_RECALL_ON_OPEN |
									 FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS ; // 自動取得を要する属性
		// 読込みに伴う同期、reparse解決、directory入力を受け付けない
		if( (attributes & K_REJECTED) != 0 || (current == absolute && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) {
			throw OperationError( ErrorKind::INVALID_INPUT) ;
		}
	}
	return absolute ;
}

//------------------------------------------------------------------------------
/**
 * @brief UTF-16を不正surrogateを許可せずUTF-8へ変換します
 * @param[in] value 呼出し中に有効なUTF-16 view
 * @return 所有UTF-8文字列。空入力は空文字列
 * @throws OperationError 不正文字列またはWindows APIの上限超過
 * @see koefude::toUtf8
 */
//------------------------------------------------------------------------------
std::string toUtf8( const std::wstring_view value)
{
	// 空入力ではWindows変換APIの0長エラーを避ける
	if( value.empty()) {
		return {} ;
	}
	// intへ変換する前にAPIが表現できる入力長を確認する
	if( value.size() > static_cast<std::size_t>( std::numeric_limits<int>::max())) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	const int length = static_cast<int>( value.size()) ; // 検証済みUTF-16 code unit数
	const int bytes = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr,
										  nullptr) ; // 必要byte数
	// 不正surrogateを置換して別pathとして扱わない
	if( bytes <= 0) {
		throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
	}
	std::string output( static_cast<std::size_t>( bytes), '\0') ; // 変換先を単独所有
	// 2回目の変換も成功数を確認し、部分結果を返さない
	if( WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, output.data(), bytes, nullptr,
						   nullptr) != bytes) {
		throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
	}
	return output ;
}

//------------------------------------------------------------------------------
/**
 * @brief UTF-8を不正sequenceを許可せずUTF-16へ変換します
 * @param[in] value 呼出し中に有効なUTF-8 view
 * @return 所有UTF-16文字列。空入力は空文字列
 * @throws OperationError 不正文字列またはWindows APIの上限超過
 * @see koefude::toWide
 */
//------------------------------------------------------------------------------
std::wstring toWide( const std::string_view value)
{
	// 空入力は有効な空の本文として返す
	if( value.empty()) {
		return {} ;
	}
	// narrowing前にWindows APIの入力長上限を確認する
	if( value.size() > static_cast<std::size_t>( std::numeric_limits<int>::max())) {
		throw OperationError( ErrorKind::RESULT_TOO_LARGE) ;
	}
	const int length = static_cast<int>( value.size()) ; // 検証済みUTF-8 byte数
	const int units = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, nullptr,
										  0) ; // 必要UTF-16 code unit数
	// 不正UTF-8を黙って別の文字へ置換しない
	if( units <= 0) {
		throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
	}
	std::wstring output( static_cast<std::size_t>( units), L'\0') ; // UIへ渡す所有文字列
	// 2回目の変換に失敗した場合も未初期化部分を公開しない
	if( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, output.data(), units) != units) {
		throw OperationError( ErrorKind::INVALID_INPUT, GetLastError()) ;
	}
	return output ;
}

} // namespace koefude
