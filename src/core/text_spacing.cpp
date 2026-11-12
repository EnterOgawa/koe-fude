//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file text_spacing.cpp
 * @brief 語の置換をせず日本語文字間の水平空白を除きます
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 日本語と英数字を区別した空白整形を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1407のtab・括弧・semicolon書式を適用
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
#include "core/text_spacing.h"

#include <cstddef>

#include "platform/win_helpers.h"

namespace koefude {
namespace {

//------------------------------------------------------------------------------
/**
 * @brief 空白を挟まない日本語の文字・句読点の範囲を調べます
 * @param[in] character UTF-16 code unit。surrogateは対象にしません
 * @return 漢字・仮名・日本語の句読点ならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool isJapaneseCharacter( const wchar_t character) noexcept
{
	return (character >= 0x3040 && character <= 0x30FF) || (character >= 0x3400 && character <= 0x4DBF) ||
		   (character >= 0x4E00 && character <= 0x9FFF) || (character >= 0xF900 && character <= 0xFAFF) ||
		   (character >= 0xFF66 && character <= 0xFF9F) || (character >= 0x3001 && character <= 0x3011) ||
		   character == 0x303B || character == 0xFF01 || character == 0xFF1F ;
}

//------------------------------------------------------------------------------
/**
 * @brief 改行と区別して水平空白だけを調べます
 * @param[in] character UTF-16 code unit
 * @return ASCII space、tab、全角spaceならtrue
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool isHorizontalSpace( const wchar_t character) noexcept
{
	return character == L' ' || character == L'\t' || character == 0x3000 ;
}

} // namespace

//------------------------------------------------------------------------------
/**
 * @brief 日本語文字同士に挟まれた水平空白だけを除きます
 * @param[in] text 日本語と判定された認識本文のUTF-8。借用し、変更しません
 * @return 整形した所有UTF-8。英数字との境界、改行、先頭・末尾空白は保持します
 * @throws OperationError 不正UTF-8または変換上限超過
 * @see koefude::normalizeJapaneseSpacing
 */
//------------------------------------------------------------------------------
std::string normalizeJapaneseSpacing( const std::string_view text)
{
	const auto	 input = toWide( text) ; // UTF-8検証を兼ねた所有UTF-16
	std::wstring result ; // 入力以下のcode unit数に収まる整形結果
	result.reserve( input.size()) ;
	// 文字と連続水平空白を一度ずつ走査し、本文全体の長さで終了する
	for( std::size_t index = 0; index < input.size();) {
		// 認識した文字と改行はそのまま保持する
		if( !isHorizontalSpace( input[index])) {
			result.push_back( input[index++]) ;
			continue ;
		}
		const std::size_t begin = index ; // 同じ空白列の開始code unit位置
		// 隣接する水平空白の終わりまで進み、改行を跨がない
		while( index < input.size() && isHorizontalSpace( input[index])) {
			++index ;
		}
		// 両隣が日本語文字の場合だけ除去し、英単語・英数字境界・端の空白を残す
		if( begin == 0 || index == input.size() || !isJapaneseCharacter( input[begin - 1]) ||
		   !isJapaneseCharacter( input[index])) {
			result.append( input, begin, index - begin) ;
		}
	}
	return toUtf8( result) ;
}

} // namespace koefude
