//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file speaker_filter.h
 * @brief 元の話者IDを維持するチャット表示フィルターを定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 全話者・個別・不明・混在の表示選択を追加
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
#include <span>

namespace koefude {
constexpr int			  K_ALL_SPEAKERS = 0 ; // 全発話を表示するfilter値
constexpr int			  K_UNKNOWN_SPEAKERS = -1 ; // 話者不明だけを表示するfilter値
constexpr int			  K_MIXED_SPEAKERS = -2 ; // 複数話者の区間だけを表示するfilter値
//------------------------------------------------------------------------------
/**
 * @brief group作成済みの発話が表示対象か判定します
 * @param[in] speakers 元発話の話者ID列。空は不明。呼出し中のborrow
 * @param[in] filter 0は全件、-1は不明、-2は混在、正数はその話者を含む発話
 * @return 対象ならtrue。未定義の負数・存在しない話者はfalse
 * @note 非表示の別話者を跨いで発話を再結合しません。原結果を変更しません
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline bool matchesSpeakerFilter( const std::span<const int> speakers, const int filter) noexcept
{
	// 全件・不明・混在の専用選択を、個別IDと区別する
	switch( filter) {
		case K_ALL_SPEAKERS:
			return true ; // 全発話
		case K_UNKNOWN_SPEAKERS:
			return speakers.empty() ; // 帰属が不明
		case K_MIXED_SPEAKERS:
			return speakers.size() > 1 ; // 帰属が複数
		default: // 個別話者はそのIDを含む混在区間も表示する
			return filter > 0 && std::find( speakers.begin(), speakers.end(), filter) != speakers.end() ;
	}
}
} // namespace koefude
