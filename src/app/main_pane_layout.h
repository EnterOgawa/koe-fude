//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file main_pane_layout.h
 * @brief 音声一覧の希望幅とmonitor内の初期矩形を純粋計算します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1356の有界geometryとdrag状態を追加
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1358の短い状態2列と実モデル列へ初期幅を更新
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
#include <array>
#include <cstdint>
#include <limits>

namespace koefude {
constexpr std::array K_HISTORY_COLUMN_WIDTHS{180, 64, 60, 60, 180} ; // 96DPI、合計544
constexpr int		 K_INITIAL_FILES_WIDTH = K_HISTORY_COLUMN_WIDTHS[0] + K_HISTORY_COLUMN_WIDTHS[1] +
									  K_HISTORY_COLUMN_WIDTHS[2] + K_HISTORY_COLUMN_WIDTHS[3] +
									  K_HISTORY_COLUMN_WIDTHS[4] + 28 ; // 枠・縦scroll・DPI丸め用に28論理pxを追加
constexpr int			 K_MINIMUM_FILES_WIDTH = 248 ; // 左の最低可読幅、96DPI
constexpr int			 K_MINIMUM_TEXT_WIDTH = 440 ; // 右の最低可読幅、比較時は各216px
constexpr int			 K_PANE_GAP = 8 ; // mouseで掴む境界幅、96DPI

//------------------------------------------------------------------------------
/**
 * @brief 有効なDPIだけを算術へ渡します
 * @param[in] dpi 入力DPI
 * @return 1〜960、範囲外は96
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline int paneDpi( const int dpi) noexcept
{
	return dpi > 0 && dpi <= 960 ? dpi : 96 ;
}
//------------------------------------------------------------------------------
/**
 * @brief 符号付き座標をoverflowせずDPI換算します
 * @param[in] value 旧DPIの座標または幅
 * @param[in] from 旧DPI
 * @param[in] to 新DPI
 * @return 四捨五入したint範囲の値
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline int paneScale( const int value, const int from, const int to) noexcept
{
	const auto denominator = paneDpi( from) ; // 0除算を防ぐ旧DPI
	const auto product = static_cast<std::int64_t>( value) * paneDpi( to) ; // 拡張積
	const auto rounded =
		(product + (product < 0 ? -denominator / 2 : denominator / 2)) / denominator ; // 負のmonitor座標も対称丸め
	return static_cast<int>( std::clamp( rounded, static_cast<std::int64_t>( std::numeric_limits<int>::min()),
									   static_cast<std::int64_t>( std::numeric_limits<int>::max()))) ;
}
/** @brief 同じclient内で重ならない横方向の配置値です */
struct MainPaneLayout {
	/// 左外側余白
	int filesX = 0 ;
	/// 実左幅、希望値とは別
	int filesWidth = 0 ;
	/// 境界左端
	int splitterX = 0 ;
	/// 境界幅
	int splitterWidth = 0 ;
	/// 右領域左端
	int textX = 0 ;
	/// 右領域幅
	int textWidth = 0 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 希望幅を保持したまま現在のclientへ配置します
 * @param[in] width 96DPIのclient幅、負は0
 * @param[in] preferred 左の希望論理幅
 * @return 両最低幅を優先し、不足時は領域内で比例分配した値
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline MainPaneLayout mainPaneLayout( int width, const int preferred) noexcept
{
	width = std::clamp( width, 0, std::numeric_limits<int>::max() / 4) ;
	const int margin = std::min( 8, width / 2) ; // 狭幅でも外へ出ない余白
	const int gap = std::min( K_PANE_GAP, width - 2 * margin) ; // client内の境界
	const int available = width - 2 * margin - gap ; // 左右への配分量
	const int left = available >= K_MINIMUM_FILES_WIDTH + K_MINIMUM_TEXT_WIDTH
						 ? std::clamp( preferred, K_MINIMUM_FILES_WIDTH, available - K_MINIMUM_TEXT_WIDTH)
						 : static_cast<int>( static_cast<std::int64_t>( available) * K_MINIMUM_FILES_WIDTH /
											(K_MINIMUM_FILES_WIDTH + K_MINIMUM_TEXT_WIDTH)) ; // 最小未満は比例
	return {margin, left, margin + left, gap, margin + left + gap, available - left} ;
}
/** @brief monitor work areaの符号付きpixel境界です */
struct PaneWorkArea {
	/// 左端、負monitor座標を許可
	int left = 0 ;
	/// 上端
	int top = 0 ;
	/// 右端
	int right = 0 ;
	/// 下端
	int bottom = 0 ;
} ;
/** @brief SetWindowPosへ渡す有界な初期pixel矩形です */
struct InitialPaneWindow {
	/// 左端
	int x = 0 ;
	/// 上端
	int y = 0 ;
	/// 外寸pixel幅
	int width = 1 ;
	/// 外寸pixel高
	int height = 1 ;
} ;
//------------------------------------------------------------------------------
/**
 * @brief 1560×860論理pxの目標外寸をwork areaへ収めます
 * @param[in] work monitorの有効作業矩形
 * @param[in] dpi 対象WindowのDPI
 * @return 中央に収まる矩形、無効work areaは1×1原点
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline InitialPaneWindow initialPaneWindow( const PaneWorkArea work, const int dpi) noexcept
{
	const auto width = static_cast<std::int64_t>( work.right) - work.left ; // 拡張差
	const auto height = static_cast<std::int64_t>( work.bottom) - work.top ; // 拡張差
	// 壊れた作業矩形を負のWindow外寸へ渡しません
	if( width <= 0 || height <= 0) {
		return {} ;
	}
	const auto wantedWidth = std::min( width, static_cast<std::int64_t>( paneScale( 1560, 96, dpi))) ;
	const auto wantedHeight = std::min( height, static_cast<std::int64_t>( paneScale( 860, 96, dpi))) ; // 目標外寸
	return {static_cast<int>( work.left + (width - wantedWidth) / 2),
			static_cast<int>( work.top + (height - wantedHeight) / 2), static_cast<int>( wantedWidth),
			static_cast<int>( wantedHeight)} ;
}
//------------------------------------------------------------------------------
/**
 * @brief monitorの作業領域を超えない最小track寸法を返します
 * @param[in] work monitor作業矩形
 * @param[in] dpi WindowのDPI
 * @return 850×620論理px以下の作業領域内寸法。x/yは初期配置と同じ参考値
 */
//------------------------------------------------------------------------------
[[nodiscard]] inline InitialPaneWindow minimumPaneWindow( const PaneWorkArea work, const int dpi) noexcept
{
	auto bounds = initialPaneWindow( work, dpi) ; // 無効workも安全な1×1へ縮小
	bounds.width = std::min( bounds.width, paneScale( 850, 96, dpi)) ;
	bounds.height = std::min( bounds.height, paneScale( 620, 96, dpi)) ;
	return bounds ;
}
/** @brief セッション内の希望幅と一回のdragを保持します。OS resourceは所有しません */
struct MainPaneSplitState {
	/// resizeで上書きしない論理希望幅
	int	 preferred = K_INITIAL_FILES_WIDTH ;
	/// beginからend/cancelまで
	bool dragging = false ;
	/// begin時の親client論理x
	int	 anchorX = 0 ;
	/// begin時の実左幅、clamp済み
	int	 anchorWidth = 0 ;
	/// Escで復帰する希望幅
	int	 previous = K_INITIAL_FILES_WIDTH ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在位置でdragを開始します
	 * @param[in] x 親client論理x
	 * @param[in] width 現client論理幅
	 */
	//------------------------------------------------------------------------------
	void begin( const int x, const int width) noexcept
	{
		// 二重beginで取消し先を失いません
		if( dragging) {
			return ;
		}
		previous = preferred ;
		anchorX = x ;
		anchorWidth = mainPaneLayout( width, preferred).filesWidth ;
		dragging = true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 有効dragの最新希望幅を更新します
	 * @param[in] x 親client論理x
	 * @param[in] width 現client論理幅
	 */
	//------------------------------------------------------------------------------
	void update( const int x, const int width) noexcept
	{
		// 終了後の遅いmouse通知を無視します
		if( !dragging) {
			return ;
		}
		const auto candidate = static_cast<std::int64_t>( anchorWidth) + x - anchorX ; // 拡張差
		const int  bounded =
			static_cast<int>( std::clamp( candidate, std::int64_t{0}, std::int64_t{std::numeric_limits<int>::max()})) ;
		preferred = mainPaneLayout( width, bounded).filesWidth ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief dragを確定または開始前へ戻します
	 * @param[in] cancel trueは開始前へ戻す
	 * @return 実行中dragを終了した場合true
	 */
	//------------------------------------------------------------------------------
	bool end( const bool cancel) noexcept
	{
		// capture-lost再入と二重終了では新しい配置要求を作りません
		if( !dragging) {
			return false ;
		}
		dragging = false ;
		// 明示取消しだけは元の希望幅を復元します
		if( cancel) {
			preferred = previous ;
		}
		return true ;
	}
} ;
} // namespace koefude
