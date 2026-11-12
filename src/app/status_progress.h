//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file status_progress.h
 * @brief status全幅へ進捗と説明を重ねる描画状態を定義します
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 全幅status進捗を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1353の確定率と独立した稼働帯を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <string>
#include <windows.h>

namespace koefude {

/** @brief status HWNDより長寿命のUI専用描画状態です */
struct StatusProgress {
	/// 標準statusと共通の段階説明
	std::wstring text ;
	/// 確定進捗0～100。待ち時間で増やさない
	int			 percent = 0 ;
	/// API実行中の不定帯。確定率とは独立
	bool		 activity = false ;
	/// 既存100ms timerでだけ更新する描画位相
	unsigned	 activityFrame = 0 ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief 完了率と独立した下端の不定帯範囲を計算します
 * @param[in] area 自status client領域
 * @param[in] frame 有界に巡回する描画位相
 * @return client内の下端3pixel以内。空領域では空矩形
 */
//------------------------------------------------------------------------------
[[nodiscard]] RECT statusActivityArea( const RECT &area, unsigned frame) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 標準statusのtext機能を維持して全幅進捗描画を登録します
 * @param[in] control UI所有のstatus HWND
 * @param[in,out] state HWND破棄まで有効なUI専用状態
 * @return 登録成功時true
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool attachStatusProgress( HWND control, StatusProgress &state) ;

} // namespace koefude
