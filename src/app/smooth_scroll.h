//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file smooth_scroll.h
 * @brief Rich Editの滑らかな縦scrollに必要なUI専用状態です
 * @version 1.0.0
 * @date 2026/09/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/09 UI timerによる短いscroll補間を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1361の比較縦scroll同期と表示anchorを追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1362の世代束縛とnative遅延位置観測を追加
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
#include <windows.h>

namespace koefude {

/** @brief 対象HWNDの破棄までMainWindowが所有し、UI threadだけで使用します */
struct SmoothScroll {
	/// 今回の補間開始位置（pixel）
	double	  origin = 0 ;
	/// 連続入力を加算した移動先（pixel）
	double	  target = 0 ;
	/// 単調clockでの開始tick（ms）
	ULONGLONG started = 0 ;
	/// timerによる補間中
	bool	  active = false ;
	/// 自身のEM_SETSCROLLPOSによる再入を識別
	bool	  applying = false ;
} ;

//------------------------------------------------------------------------------
/**
 * @brief wheel・行・page移動を補間するsubclassを登録します
 * @param[in] control UI thread所有のRich Edit HWND
 * @param[in,out] state HWND破棄まで有効な所有状態。別controlと共有しません
 * @return 登録できればtrue。失敗時は標準scrollのままです
 */
//------------------------------------------------------------------------------
[[nodiscard]] bool attachSmoothScroll( HWND control, SmoothScroll &state) noexcept ;

//------------------------------------------------------------------------------
/**
 * @brief 文書差替え前に残った補間先とtimerを捨てます
 * @param[in] control 自RichEdit
 * @param[in,out] state 同じcontrolの状態
 */
//------------------------------------------------------------------------------
void			   cancelSmoothScroll( HWND control, SmoothScroll &state) noexcept ;
#if defined(KOEFUDE_WINDOW_TESTING)
//------------------------------------------------------------------------------
/**
 * @brief 実入力注入なしで製品の補間開始境界を確認します
 * @param[in] control 自所有control
 * @param[in,out] state 製品状態
 * @param[in] delta 有限pixel移動量
 */
//------------------------------------------------------------------------------
void testStartSmoothScroll( HWND control, SmoothScroll &state, double delta) noexcept ;
#endif
} // namespace koefude
