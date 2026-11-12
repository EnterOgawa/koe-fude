//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file control_accessibility.h
 * @brief UI thread上の標準controlへ有界な目的名を付与します
 * @version 1.0.0
 * @date 2026/09/27
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/09/27 新規作成
 * 0.1.0 Shunji Ogawa 2026/09/27 7欄の目的名と明示解除の契約を追加
 * 0.1.0 Shunji Ogawa 2026/10/04 保存済み結果の表示選択を含む8欄/9欄目拒否を接続
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
#include <array>
#include <cstddef>
#include <string>
#include <windows.h>

// Win32基本型の後に標準アクセシビリティとCOM所有型を参照する
#include <oleacc.h>
#include <wrl/client.h>

namespace koefude {
/** @brief HWND破棄前に解除する、同一UI thread専用の目的名登録です */
class ControlAccessibility final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 未登録状態を作成します。COMは最初の登録時に取得します
	 */
	//------------------------------------------------------------------------------
	ControlAccessibility() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 部分初期化時にも残存登録を解除します。通常はclearで先に診断します
	 */
	//------------------------------------------------------------------------------
	~ControlAccessibility() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 登録の二重所有を禁止します
	 * @note 名前のない第1引数（const ControlAccessibility &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ControlAccessibility( const ControlAccessibility &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 登録の二重解除を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const ControlAccessibility &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	ControlAccessibility &operator=( const ControlAccessibility &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 設定値と別にcontrolの目的名を付与・更新します
	 * @param[in] window 同じprocess/UI threadの有効な非所有HWND。clearまで存続すること
	 * @param[in] name NULL文字を含まない1～128 UTF-16単位の名前。APIがcopyします
	 * @return S_OKは登録成功。失敗は入力・thread・容量・COMのHRESULTです
	 * @pre 呼出しthreadのSTAが初期化済みであること
	 * @note 新規登録は8欄まで。失敗したAPI呼出しもclearで解除を試みます
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HRESULT setName( HWND window, const std::wstring &name) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 生存中の全HWNDから目的名だけを解除します
	 * @return S_OKは全解除成功。失敗は最初の解除HRESULTで、次回も保持します
	 * @pre 登録と同じUI threadで、child HWNDの破棄前に呼ぶこと
	 * @post 失敗時も全登録の解除を試み、所有列を空にします
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] HRESULT clear() noexcept ;

	private:
	/// UI threadが所有する標準COM service
	Microsoft::WRL::ComPtr<IAccPropServices> services_ ;
	/// 名前を付与した非所有HWNDの固定容量列
	std::array<HWND, 8>						 windows_{} ;
	/// 有効な登録数。同じHWNDの翻訳更新では増やさない
	std::size_t								 count_ = 0 ;
	/// 明示終了へ伝える最初の解除失敗
	HRESULT									 cleanupError_ = S_OK ;
} ;
} // namespace koefude
