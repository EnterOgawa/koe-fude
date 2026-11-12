//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file comparison_scroll.h
 * @brief 単一RichEdit文書の32bit scroll範囲と遅延layoutを扱います
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の発言間補間と旧結果の割合同期を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1362の可視発言境界と同pixel高さで追従
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1363の単一比較表と共通scroll契約へ更新
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <richedit.h>
#include <windows.h>

namespace koefude {
//------------------------------------------------------------------------------
/**
 * @brief stream/幅変更後の遅延layoutを末尾一座標の照会で確定します
 * @param[in] pane 自所有RichEdit
 * @note 本文取得や選択/scroll変更をしません
 */
//------------------------------------------------------------------------------
inline void completeComparisonScrollLayout( HWND pane) noexcept
{
	GETTEXTLENGTHEX query{GTL_PRECISE | GTL_NUMCHARS, 1200} ; // native UTF-16総cp
	const auto		length = SendMessageW( pane, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>( &query), 0) ;
	// 空文書や不正な長さでは末尾cpを照会しません
	if( length <= 0 || length > std::numeric_limits<LONG>::max()) {
		return ;
	}
	POINTL point{} ; // 遅延layout確定だけに使う実座標
	SendMessageW( pane, EM_POSFROMCHAR, reinterpret_cast<WPARAM>( &point), length - 1) ;
}
/** @brief RichEditの32bit縦位置と有効な最大位置です */
struct ComparisonScrollRange {
	/// SCROLLINFOから得たpixel位置
	LONG position = 0 ;
	/// page分を除いた最大位置
	LONG maximum = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 16bitのEM_GETSCROLLPOSに依存せず実範囲を読みます
 * @param[in] pane 自所有RichEdit
 * @return 取得不能/短文は0範囲
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline ComparisonScrollRange comparisonScrollRange( HWND pane) noexcept
{
	// 短文でbarを隠したRichEditは直前のSCROLLINFOを残すため、有効範囲と扱いません
	if( (GetWindowLongW( pane, GWL_STYLE) & WS_VSCROLL) == 0) {
		return {} ;
	}
	SCROLLINFO info{sizeof(info), SIF_POS | SIF_RANGE | SIF_PAGE} ; // 32bitの範囲と位置
	// 取得不能なcontrolへ架空の位置を返しません
	if( !GetScrollInfo( pane, SB_VERT, &info)) {
		return {} ;
	}
	const auto maximum =
		std::clamp<std::int64_t>( static_cast<std::int64_t>( info.nMax) - std::max<std::int64_t>( 0, info.nPage - 1LL), 0,
								 std::numeric_limits<LONG>::max()) ; // 減算を64bitで検証
	return {std::clamp<LONG>( info.nPos, 0L, static_cast<LONG>( maximum)), static_cast<LONG>( maximum)} ;
}
} // namespace koefude
