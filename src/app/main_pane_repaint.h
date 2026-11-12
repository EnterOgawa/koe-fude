//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file main_pane_repaint.h
 * @brief 子配置の旧新領域を集約し、配置完了後だけ同期再描画します
 * @version 1.0.0
 * @date 2026/11/09
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/09 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1357の中間描画抑制と同期更新を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include <memory>
#include <type_traits>
#include <windows.h>

#include "core/types.h"

namespace koefude {
constexpr UINT K_MAIN_PANE_MOVE_FLAGS =
	SWP_NOREDRAW | SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOZORDER ; // 中間描画/旧画素転用なし
constexpr UINT K_MAIN_PANE_REDRAW_FLAGS =
	RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW ; // move内で同期完了

/**
 * @brief 配置中フラグと一回分の旧新dirty regionをUI threadだけで所有します
 * @note 親/child HWNDは借用し、このobjectより長く生存します。コピー/移動は行いません
 */
class MainPaneRepaint final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 空の領域を確保し、同期通知によるlayout再入を抑止します
	 * @param[in] parent 所有MainWindowの非NULL handle
	 * @param[in,out] active falseの配置中フラグ。破棄までtrue、破棄時falseへ戻す
	 * @throws OperationError region確保失敗。失敗時activeは変更しません
	 */
	//------------------------------------------------------------------------------
	MainPaneRepaint( HWND parent, bool &active)
		: parent_( parent), active_( active), dirty_( CreateRectRgn( 0, 0, 0, 0), &DeleteObject),
		  scratch_( CreateRectRgn( 0, 0, 0, 0), &DeleteObject)
	{
		// 有限regionの確保失敗を空領域の正常完了にしません
		if( !dirty_ || !scratch_) {
			throw OperationError( ErrorKind::INTERNAL, ERROR_NOT_ENOUGH_MEMORY) ;
		}
		active_ = true ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 例外時も変更済み領域を失効させ、配置中フラグとGDI所有を解放します
	 */
	//------------------------------------------------------------------------------
	~MainPaneRepaint() noexcept
	{
		// 配置/更新失敗時は次の標準paintで回復できるよう有限領域を失効させます
		if( changed_ && !flushed_) {
			static_cast<void>( RedrawWindow( parent_, nullptr, dirty_.get(), K_MAIN_PANE_REDRAW_FLAGS & ~RDW_UPDATENOW)) ;
		}
		active_ = false ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief regionと配置中フラグの二重所有を禁止します
	 * @note 名前のない第1引数（const MainPaneRepaint &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	MainPaneRepaint( const MainPaneRepaint &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief regionと配置中フラグの二重所有を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const MainPaneRepaint &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	MainPaneRepaint &operator=( const MainPaneRepaint &) = delete ;

	//------------------------------------------------------------------------------
	/**
	 * @brief 変更するchildだけを描画なしで配置し、表示childの旧新範囲を保持します
	 * @param[in] child parent直下の非NULL借用control
	 * @param[in] next 親client基準pixel矩形。正の幅/高さ
	 * @throws OperationError 矩形読取り/region操作/配置の失敗。既変更領域は破棄時失効
	 */
	//------------------------------------------------------------------------------
	void			 place( HWND child, const RECT &next)
	{
		RECT before{} ; // 配置前の親client座標
		// 無効handleを未初期化座標へ読み替えません
		if( !GetWindowRect( child, &before)) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// RECTを2点として渡し、RTL時の標準座標変換にも従います
		MapWindowPoints( nullptr, parent_, reinterpret_cast<POINT *>( &before), 2) ;
		// 同一位置を再配置して余分なpaint/通知を作りません
		if( EqualRect( &before, &next)) {
			return ;
		}
		const bool visible = (GetWindowLongW( child, GWL_STYLE) & WS_VISIBLE) != 0 ; // child自身の契約
		// 兄弟すべての位置が確定するまで描画と旧画素copyを禁止します
		if( !SetWindowPos( child, nullptr, next.left, next.top, next.right - next.left, next.bottom - next.top,
						 K_MAIN_PANE_MOVE_FLAGS)) {
			const DWORD error = GetLastError() ; // region操作で上書きしない元配置error
			// 途中失敗でも既存表示childの旧新要求領域へ回復paintを残します
			if( visible) {
				add( before) ;
				add( next) ;
			}
			throw OperationError( ErrorKind::INTERNAL, error) ;
		}
		RECT after{} ; // combo等が標準処理で補正した実配置
		// 制約付き標準controlの要求高さを実描画領域と混同しません
		if( !GetWindowRect( child, &after)) {
			const DWORD error = GetLastError() ; // 後続領域操作より先に保存
			// 読取り失敗時も変更した表示領域を回復可能にします
			if( visible) {
				add( before) ;
				add( next) ;
			}
			throw OperationError( ErrorKind::INTERNAL, error) ;
		}
		// 上と同じRECT二点のWin32境界。afterはこの呼出し中だけ借用します
		MapWindowPoints( nullptr, parent_, reinterpret_cast<POINT *>( &after), 2) ;
		// hidden比較paneの表示を変えず、実矩形に変化があったchildだけを塗ります
		if( visible && !EqualRect( &before, &after)) {
			add( before) ;
			add( after) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 余白/scroll復元後に親背景と新しいchild領域を一回同期更新します
	 * @return 標準RedrawWindowの成否。変更なしはtrue
	 * @note WM_PAINTは直接送らず、OSの通常描画dispatchに従います
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] BOOL flush() noexcept
	{
		const BOOL result = !changed_ || RedrawWindow( parent_, nullptr, dirty_.get(), K_MAIN_PANE_REDRAW_FLAGS) ;
		flushed_ = result != FALSE ;
		return result ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 変更した表示領域の有無を返します
	 * @return 変更があればtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool changed() const noexcept
	{
		return changed_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同期更新に渡す親client領域を借用します
	 * @return このobjectの破棄まで有効な非NULL region。変更/解放禁止
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HRGN region() const noexcept
	{
		return dirty_.get() ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief 親client外を除き、一回の更新へ矩形を加えます
	 * @param[in] area 親client基準の旧または新pixel矩形
	 * @throws OperationError region操作の失敗
	 */
	//------------------------------------------------------------------------------
	void add( const RECT &area)
	{
		RECT client{} ; // 親の描画可能範囲
		RECT clipped{} ; // OS非clientや親領域外へ広げない矩形
		// 有効parentのclient読取り失敗を正常な空領域にしません
		if( !GetClientRect( parent_, &client)) {
			throw OperationError( ErrorKind::INTERNAL, GetLastError()) ;
		}
		// 親外のhidden/offscreen childはdirty領域へ含めません
		if( !IntersectRect( &clipped, &area, &client)) {
			return ;
		}
		// 同じ二つのGDI regionを全12配置で再利用し、領域数を増やしません
		if( !SetRectRgn( scratch_.get(), clipped.left, clipped.top, clipped.right, clipped.bottom) ||
		   CombineRgn( dirty_.get(), dirty_.get(), scratch_.get(), RGN_OR) == ERROR) {
			throw OperationError( ErrorKind::INTERNAL, ERROR_NOT_ENOUGH_MEMORY) ;
		}
		changed_ = true ;
	}
	using Region = std::unique_ptr<std::remove_pointer_t<HRGN>, decltype(&DeleteObject)> ; // GDI RAII
	/// 借用親。標準close完了まで生存
	HWND   parent_ ;
	/// UI threadだけが扱う借用再入フラグ
	bool  &active_ ;
	/// 表示child旧新領域の和集合
	Region dirty_ ;
	/// 矩形を和集合へ加えるための再利用領域
	Region scratch_ ;
	/// 空領域へ同期paintを要求しない判定
	bool   changed_ = false ;
	/// 失敗時だけ非同期失効を残す判定
	bool   flushed_ = false ;
} ;
} // namespace koefude
