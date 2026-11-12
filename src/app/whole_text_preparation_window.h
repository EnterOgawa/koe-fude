//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file whole_text_preparation_window.h
 * @brief 補正準備を明示確認する標準Win32画面を定義します
 * @version 1.0.0
 * @date 2026/10/20
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/10/20 新規作成
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1279の補正準備入口を追加
 * 0.1.0 Shunji Ogawa 2026/10/20 callback失敗表示を当該呼出しだけに限定し再入を防止
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1280の明示モデル導入と同一枠の操作排他を接続
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1283の四操作を同じ一枠で扱う契約を明記
 * 0.1.0 Shunji Ogawa 2026/10/20 ADR1287の明示診断と隔離接続を追加
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
#include "inference/whole_text_preparation_controller.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <windows.h>

namespace koefude {
/** @brief 四つの明示準備操作を開始し、worker終了まで親が所有する標準Windowです */
class WholeTextPreparationWindow final {
	public:
	//------------------------------------------------------------------------------
	/**
	 * @brief 未表示・未検査の所有状態を初期化します
	 */
	//------------------------------------------------------------------------------
	WholeTextPreparationWindow() = default ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 親がstopped確認後にUIとworkerを解放します
	 */
	//------------------------------------------------------------------------------
	~WholeTextPreparationWindow() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UIのHWND/fontと一つの準備controller・操作通知の対応を複製しないため、copy構築を禁止します
	 * @note 名前のない第1引数（const WholeTextPreparationWindow &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextPreparationWindow( const WholeTextPreparationWindow &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief UIのHWND/fontと一つの準備controller・操作通知の対応を複製しないため、copy代入を禁止します
	 * @return 削除された代入演算子のため正常復帰はなく、参照結果は取得できません
	 * @note 名前のない第1引数（const WholeTextPreparationWindow &）は複製元となる同型objectを表す型制約です。呼出しは禁止されています
	 */
	//------------------------------------------------------------------------------
	WholeTextPreparationWindow &operator=( const WholeTextPreparationWindow &) = delete ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 初期pathを所有し、検査を開始せず表示します
	 * @param[in] owner 全寿命で有効な非NULL親。非表示親なら表示しません
	 * @param[in] request 初回だけ採用するpathとCPU方式。モデルI/Oは行いません
	 * @param[in] canStart 空可の親受付条件。UI threadだけで呼びます
	 * @param[in] runner 初回だけ固定する同期処理。空は既定を維持します
	 * @throws OperationError 標準Window/control作成失敗
	 */
	//------------------------------------------------------------------------------
	void			   show( HWND owner, WholeTextPreparationRequest request, std::function<bool()> canStart = {},
							WholeTextPreparationRunner runner = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未受領終端を含む占有を返します
	 * @return 新規準備/生成を拒否する間true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool busy() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 自workerの最新DTOだけを短時間で回収します
	 */
	//------------------------------------------------------------------------------
	void			   poll() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付を停止し、joinせず取消しを要求します
	 */
	//------------------------------------------------------------------------------
	void			   requestStop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief worker終了を確認します
	 * @return 未起動または終了時true
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] bool stopped() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief keyboard移動先を返します
	 * @return 非所有HWND、未作成NULL
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
#if defined(KOEFUDE_WINDOW_TESTING)
	//------------------------------------------------------------------------------
	/**
	 * @brief 自process試験だけが実file検査を差し替えます
	 * @param[in] runner worker開始前に固定する同期fake
	 * @pre 未起動またはworker終了済みです
	 */
	//------------------------------------------------------------------------------
	void setRunnerForTesting( WholeTextPreparationRunner runner) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief OS dialogを操作せず返却値だけを差し替えます
	 * @param[in] picker 自process同期fake。nulloptは取消し
	 */
	//------------------------------------------------------------------------------
	void setPickerForTesting( std::function<std::optional<std::filesystem::path>()> picker) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 試験用の所有snapshotを参照します
	 * @return UI instance寿命内のborrow
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] const WholeTextPreparationEvent &eventForTesting() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 次の自callbackだけで人工例外を発生させます
	 * @param[in] nested trueなら失敗表示中にも一回だけ自callbackを再入させます
	 * @note 試験processのUI thread専用です。外部入力は操作しません
	 */
	//------------------------------------------------------------------------------
	void										   failNextCallbackForTesting( bool nested) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief guardを通過した表示更新数を返します
	 * @return UI threadの実測回数
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::uint64_t					   updateCountForTesting() const noexcept ;
#endif
  private:
#if defined(KOEFUDE_WINDOW_TESTING)
    friend struct CurrentCorrectionPreparationTestAccess ; // 専用自process試験だけ
#endif
	//------------------------------------------------------------------------------
	/**
	 * @brief 有限の標準controlを作成します
	 */
	//------------------------------------------------------------------------------
	void initialize() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief MS ゴシックとcontrolを現在DPIへ配置します
	 */
	//------------------------------------------------------------------------------
	void layout() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief metadataと所有snapshotから表示だけを更新します
	 */
	//------------------------------------------------------------------------------
	void update() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示開始だけが一枠workerへ要求を送ります
	 * @param[in] action 同じ一枠で行う固定操作
	 */
	//------------------------------------------------------------------------------
	void start( WholeTextPreparationAction action = WholeTextPreparationAction::INSPECT) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 標準file dialogからlocal file一件を選びます
	 * @return 所有path。取消しはnullopt
	 * @throws OperationError dialog失敗
	 */
	//------------------------------------------------------------------------------
	[[nodiscard]] std::optional<std::filesystem::path> pickSource() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 準備Windowの生成・配置・明示操作をUI threadで振り分け、例外を準備失敗表示へ変換します
	 * @param[in] window OSから渡される自WindowのHWND。GWLP_USERDATAにはWindow寿命中だけownerを借用します
	 * @param[in] message WM_NCCREATE/CREATE、サイズ/DPI、command、close/破棄等のWin32通知種別です
	 * @param[in] wparam commandではcontrol IDと通知code、サイズ/DPIではOS定義値です。種類に従ってだけ解釈します
	 * @param[in] lparam WM_NCCREATEでは非null CREATESTRUCTWのlpCreateParamsからownerを借用します。WM_GETMINMAXINFOは書込先、
	 *        WM_DPICHANGEDは同期借用RECTです。他はmessage固有値です
	 * @return 処理済みの生成/配置/command/closeは0、WM_CREATE内の例外は-1、それ以外はDefWindowProcWの値です。0を準備/取得の完了と解釈しません
	 * @note owner未登録時は標準処理へ返し、WM_NCDESTROYで借用を解除します。操作例外は固定error/codeへ記録し、表示更新失敗にも固定診断を残してC++例外を越えません
	 */
	//------------------------------------------------------------------------------
	static LRESULT CALLBACK		windowProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept ;
	/// SW_HIDE試験でも実open/closeを区別
	bool						opened_ = false ;
	/// UI thread所有Window
	HWND						window_ = nullptr ;
	/// child破棄後に解放する指定font
	HFONT						font_ = nullptr ;
	/// font更新のDPI
	UINT						fontDpi_ = 0 ;
	/// 音声・本文を持たない要求
	WholeTextPreparationRequest request_ ;
	/// 最新操作の所有通知
	WholeTextPreparationEvent	event_ ;
	/// stale通知の照合番号
	std::uint64_t				active_ = 0 ;
	/// 同期control通知からの表示更新再入を拒否します
	bool						updating_ = false ;
	/// 明示操作の受付拒否理由を単発で表示
	bool						refused_ = false ;
	/// 親の処理状態が変わった時だけ操作表示を更新
	bool						parentAvailable_ = true ;
	/// 親の終了中は新規受付しません
	bool						stopping_ = false ;
	/// 同じ表示では選択位置を維持します
	std::wstring				details_ ;
	/// 通常認識/他準備とのUI受付排他
	std::function<bool()>		canStart_ ;
	/// worker専用同期処理
	WholeTextPreparationRunner	runner_ = runWholeTextPreparation ;
#if defined(KOEFUDE_WINDOW_TESTING)
    /// OS入力を使わない試験境界
	std::function<std::optional<std::filesystem::path>()> picker_ ;
	/// 次の自callbackだけに人工例外を入れます
	bool												  callbackFailureOnce_ = false ;
	/// 失敗表示中に一回だけ再入を検証します
	bool												  nestedCallbackOnce_ = false ;
	/// 実際にguardを通過した更新数
	std::uint64_t										  updateCount_ = 0 ;
#endif
    /// 明示開始時だけ作成
	std::unique_ptr<WholeTextPreparationController> controller_ ;
} ;
} // namespace koefude
