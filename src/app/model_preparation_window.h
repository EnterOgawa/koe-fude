//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file model_preparation_window.h
 * @brief 結果閲覧と独立したモデル準備Windowを宣言します
 * @version 1.0.0
 * @date 2026/09/13
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/13 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/13 条件・容量・取得状態のnative画面を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1372の独立話者投影と元原文保持を接続
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1380のopen/close通知gateを追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1413の実契約・所有寿命・戻り値の意味を具体化
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1415のoperator括弧と文書lambdaの書式を補修
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once

#include <string>
#include <windows.h>

#include "inference/model_preparation_controller.h"

namespace koefude {

/** @brief workerを所有せず、親へ値だけの操作通知を送るUIです */
class ModelPreparationWindow final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 未作成のWindow所有状態を初期化します
	 */
	//------------------------------------------------------------------------------
	ModelPreparationWindow() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じUI threadでWindowとfontを解放します
	 */
	//------------------------------------------------------------------------------
	~ModelPreparationWindow() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UI thread所有のHWND/fontと非所有親HWNDの対応を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const ModelPreparationWindow &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelPreparationWindow( const ModelPreparationWindow &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UI thread所有のHWND/fontと非所有親HWNDの対応を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const ModelPreparationWindow &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ModelPreparationWindow &operator=( const ModelPreparationWindow &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準Windowを必要時に作成して表示します
	 * @param[in] owner このinstanceより長く有効な親Window。非NULL
	 * @throws OperationError Windowまたはcontrolの作成失敗
	 */
	//------------------------------------------------------------------------------
	void					show( HWND owner) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 所有DTOから条件・容量・実測状態を再表示します
	 * @param[in] options 現在選択した構成
	 * @param[in] event 同じ構成の最新通知。呼出し中のborrow
	 * @param[in] busy 終端未受領を含む操作中ならtrue
	 * @param[in] speakerText 明示補助参照の用途説明を加える場合true
	 */
	//------------------------------------------------------------------------------
	void update( const EngineOptions &options, const ModelPreparationEvent &event, bool busy, bool speakerText = false) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief keyboard移動先を返します
	 * @return 非所有HWND。未作成ならNULL
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HWND handle() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 再利用HWNDの存続と独立したopen状態
	 * @return 閉じるまでtrue
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool opened() const noexcept
	{
		return opened_ ;
	}

	private:
	//------------------------------------------------------------------------------
	/**
	 * @brief MS ゴシックを現在DPIへ合わせて配置します
	 */
	//------------------------------------------------------------------------------
	void					layout() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 必要な標準controlを同じUI threadで作成します
	 */
	//------------------------------------------------------------------------------
	void					initialize() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief OS callback境界でWindow操作を処理します
	 * @param[in] window 対象Window
	 * @param[in] message 標準message ID
	 * @param[in] wparam message固有値
	 * @param[in] lparam message固有値。WM_NCCREATEでは同期borrow
	 * @return Win32で定義された処理結果
	 */
	//------------------------------------------------------------------------------
	static LRESULT CALLBACK windowProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept ;
	/// SW_HIDE試験でも実open/closeを区別
	bool					opened_ = false ;
	/// 同じthreadで破棄するWindow
	HWND					window_ = nullptr ;
	/// 操作IDだけを通知する非所有親
	HWND					owner_ = nullptr ;
	/// control破棄後に解放するMS ゴシック
	HFONT					font_ = nullptr ;
	/// 同じDPIではfontを再作成しない
	UINT					fontDpi_ = 0 ;
	/// 同じ表示の再設定による選択解除を防ぐ値
	std::wstring			details_ ;
} ;

} // namespace koefude
