//------------------------------------------------------------------------------
/**
 * Copyright (c) 2026 Shunji Ogawa.
 * SPDX-License-Identifier: MIT
 *
 * @file api_text_correction_window.h
 * @brief 独立API設定と画面なしの行メニュー実行を管理します
 * @version 1.0.0
 * @date 2026/11/08
 * @author Shunji Ogawa
 * @par 更新履歴
 * <pre>
 * 0.1.0 Shunji Ogawa 2026/11/08 新規作成
 * 0.1.0 Shunji Ogawa 2026/11/08 API専用の明示操作画面を追加
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1345の動的モデル・凍結単価と旧履歴互換を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1347の設定・文書二画面とtab表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1348の行メニュー直接実行と通常表示を接続
 * 0.1.0 Shunji Ogawa 2026/11/08 ADR1349の一回適用と全単価表示へ更新
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1354の統合設定childへ既存Applyを公開
 * 0.1.0 Shunji Ogawa 2026/11/09 ADR1368の明示音声APIと従来経路の境界を接続
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1374の未適用変更とhost通知を追加
 * 0.1.0 Shunji Ogawa 2026/11/10 ADR1376の設定保存完了と続行資格を公開
 * 1.0.0 Shunji Ogawa 2026/11/10 ADR1378の明示更新境界を追加
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1400の取得中表示と設定下部操作の整理を反映
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1408の指定見本書式・コメント配置へ移行
 * 1.0.0 Shunji Ogawa 2026/11/11 ADR1410の所有識別子・参照と宣言説明を規約へ整合
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1428の関数ヘッダーを上下区切り線と複数行へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 dialogProcの役割・message別引数・戻り値・借用寿命を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1431の各関数へ役割・全引数・戻り値を直接記載
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1433の更新履歴を新規作成からの昇順・末尾追記へ統一
 * 1.0.0 Shunji Ogawa 2026/11/12 ADR1437のコメント行末・終端の句点を省略
 * </pre>
 */
//------------------------------------------------------------------------------
#pragma once
#include "core/job_controller.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <windows.h>
namespace koefude {
/** @brief 借用engineを持たないAPI設定と通常行操作のcontrollerです */
class ApiTextCorrectionWindow final {
	public:
	/// 値/secretなしの同期child状態通知
	static constexpr UINT K_SETTINGS_CHANGED_MESSAGE = WM_APP + 2 ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現sessionの未適用欄を照合します
	 * @return 明示Applyが必要
	 */
	//------------------------------------------------------------------------------
	bool				  settingsDirty() const noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分の画面だけを破棄します
	 */
	//------------------------------------------------------------------------------
	~ApiTextCorrectionWindow() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 対象を固定して準備をqueueへ渡します
	 * @param[in] owner 親
	 * @param[in] request 原文所有値
	 * @param[in] submit 同じJobController受付
	 * @param[in] cancel 同じworker取消し
	 * @param[in] catalogSubmit 本文なしmetadataの同worker受付
	 */
	//------------------------------------------------------------------------------
	void		  show( HWND owner, ApiTextCorrectionJobRequest request,
					   std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
					   std::function<void( std::uint64_t)>						 cancel,
					   std::function<std::uint64_t( ApiCatalogJobRequest)>		 catalogSubmit = {}) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 履歴なしで独立設定を開きます
	 * @param[in] owner 親
	 * @param[in] submit キー専用worker受付
	 * @param[in] cancel 同worker取消し
	 * @param[in] catalogSubmit 本文なし設定/metadata受付
	 * @param[in] embedded 統合設定のchildとして配置するか
	 */
	//------------------------------------------------------------------------------
	void		  showSettings( HWND owner, std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
							   std::function<void( std::uint64_t)>				  cancel,
							   std::function<std::uint64_t( ApiCatalogJobRequest)> catalogSubmit, bool embedded = false) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 既存callbacksを使って統合設定の実child pageを開きます
	 * @param[in] owner host
	 */
	//------------------------------------------------------------------------------
	void		  showSettingsPage( HWND owner) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未適用API編集だけを既存の一回Applyへ渡します
	 */
	//------------------------------------------------------------------------------
	void		  applySettingsPage() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未適用secret破棄と設定操作だけの協調取消しを依頼します
	 */
	//------------------------------------------------------------------------------
	void		  closeSettingsPage() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 統合設定childの内容を現在clientへ配置します
	 */
	//------------------------------------------------------------------------------
	void		  layoutSettingsPage() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じsessionの表示だけを切り替え、取得中表示を同期します
	 * @param[in] visible 文章改善tabが選択されているか。通信は開始しません
	 */
	//------------------------------------------------------------------------------
	void		  setSettingsPageVisible( bool visible) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在のoperationを返します
	 * @return 未受付0、単調ID
	 */
	//------------------------------------------------------------------------------
	std::uint64_t operationId() const noexcept
	{
		return active_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 画面を作らず起動時のlocal準備を依頼します
	 * @param[in] submit 同じworker受付
	 * @param[in] cancel 取消し
	 * @param[in] catalogSubmit 本文なしmetadata受付
	 */
	//------------------------------------------------------------------------------
	void		  initialize( std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit,
							 std::function<void( std::uint64_t)>						   cancel,
							 std::function<std::uint64_t( ApiCatalogJobRequest)>		   catalogSubmit) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 画面なしで対象行の明示操作を受け付けます
	 * @param[in] request 固定source
	 * @param[in] action ImproveまたはHTTP0表示操作
	 * @return 受付ID、拒否0
	 */
	//------------------------------------------------------------------------------
	std::uint64_t direct( ApiTextCorrectionJobRequest request, ApiTextAction action) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief local前提から行メニュー可否を返します
	 * @return 認証確認前の実行受付可否
	 */
	//------------------------------------------------------------------------------
	bool		  ready() const ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 開いた設定の全保存ACKと現在キーのmetadata成功を照合します
	 * @return 当sessionの明示続行が可能。HTTP/I/Oは行いません
	 */
	//------------------------------------------------------------------------------
	bool		  settingsFollowupReady() const
	{
		return settingsApplySucceeded_ && ready() && !settingsDirty() &&
			   catalog_.modelsKeyRevision == catalog_.keyRevision && catalog_.modelMeta.http == 200 &&
			   !catalog_.modelMeta.error && catalog_.priceMeta.http == 200 && !catalog_.priceMeta.error ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 音声APIの明示設定表示だけでmetadataを更新します
	 */
	//------------------------------------------------------------------------------
	void showAudioMetadata() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じsessionの音声設定を終了します。通信を開始しません
	 */
	//------------------------------------------------------------------------------
	void closeAudioMetadata() noexcept
	{
		audioSettingsOpened_ = false ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 明示更新buttonを同じ固定GETへ接続します
	 */
	//------------------------------------------------------------------------------
	void refreshAudioMetadata()
	{
		if( audioSettingsOpened_) {
			refresh( true) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 音声受付用の公開値を参照します
	 * @return 秘密なしのUI正本
	 */
	//------------------------------------------------------------------------------
	const ApiModelCatalog &audioCatalog() const noexcept
	{
		return catalog_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief キー適用途中は音声受付を禁止します
	 * @return 受付可能
	 */
	//------------------------------------------------------------------------------
	bool audioReady() const noexcept
	{
		return !active_ && !stopped_ && keyReady_ && catalog_.loaded &&
			   catalog_.audioPrice.microPerMinute.has_value() && catalog_.models.contains( "gpt-transcribe") ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じshared targetのキー有無だけを返します
	 * @return キーあり
	 */
	//------------------------------------------------------------------------------
	bool audioKeyReady() const noexcept
	{
		return keyReady_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ通常行のeventかを返します
	 * @param[in] id event ID
	 * @return 一致
	 */
	//------------------------------------------------------------------------------
	bool directEvent( std::uint64_t id) const noexcept
	{
		return direct_ && active_ == id ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在の自分のopだけへ協調取消しを要求します
	 */
	//------------------------------------------------------------------------------
	void cancelCurrent()
	{
		// 自分が受け付けたoperationだけへ取消しを送ります
		if( active_ && cancel_) {
			cancel_( active_) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief keyboard navigation用設定HWNDです
	 * @return 非所有HWND
	 */
	//------------------------------------------------------------------------------
	HWND settingsHandle() const noexcept
	{
		return settingsWindow_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 取得中dialogのkeyboard navigation先を返します
	 * @return UI threadの非所有HWND。未表示/終端後はNULL
	 */
	//------------------------------------------------------------------------------
	HWND catalogProgressHandle() const noexcept
	{
		return catalogProgress_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 対象file表示だけを設定します
	 * @param[in] name 送信しないfile名
	 */
	//------------------------------------------------------------------------------
	void setTargetName( std::wstring name)
	{
		// 進行中の送信対象と表示名を同じ受付へ固定します
		if( !active_) {
			targetName_ = std::move( name) ;
		}
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じoperation終端を受領します
	 * @param[in] event 型付きevent
	 * @return 自分の要求
	 */
	//------------------------------------------------------------------------------
	bool receive( const JobEvent &event) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 新規受付停止と取消しを要求します
	 */
	//------------------------------------------------------------------------------
	void stop() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 更新起動の失敗だけで終了を取り消す時にidle受付へ戻します
	 * @note キー/設定/metadataを再読/変更/送信しません。UI thread専用です
	 */
	//------------------------------------------------------------------------------
	void resumeAfterUpdateFailure() noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分のWindowを返します
	 * @return 非所有HWND
	 */
	//------------------------------------------------------------------------------
	HWND handle() const noexcept
	{
		return window_ ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 自分の受付状態を返します
	 * @return 未受領要求が存在
	 */
	//------------------------------------------------------------------------------
	bool busy() const noexcept
	{
		return active_ != 0 ;
	}
	//------------------------------------------------------------------------------
	/**
	 * @brief 固定した対象カードを返します
	 * @return 履歴ID、未設定0
	 */
	//------------------------------------------------------------------------------
	std::uint64_t historyId() const noexcept
	{
		return request_.key.historyId ;
	}

	private:
	friend struct ApiUi1347TestAccess ; // 有限の自所有イベントだけ
	friend struct ApiTextWindowTestAccess ; // 自processの有限専用event試験だけ
	friend struct ApiCatalogWindowTestAccess ; // metadata専用の自process試験
	//------------------------------------------------------------------------------
	/**
	 * @brief 同じ原文と一回の操作をenqueueします
	 * @param[in] action 明示操作
	 */
	//------------------------------------------------------------------------------
	void					start( ApiTextAction action) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief API画面内のmetadataだけを更新します
	 * @param[in] prices 公開価格も取得するか
	 */
	//------------------------------------------------------------------------------
	void					refresh( bool prices) ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 選択のsnapshotを同期し古い同意を失効します
	 */
	//------------------------------------------------------------------------------
	void					selectModel() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 有界状態だけからcontrolを更新します
	 */
	//------------------------------------------------------------------------------
	void					update() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 表示中の設定tabだけでmetadata取得のowned dialogを同期します
	 */
	//------------------------------------------------------------------------------
	void					updateSettingsActivity() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief metadata取得中dialogのclose/Escを同じ有効要求の協調取消しへ渡します
	 * @param[in] window UI threadでOSから渡される自所有dialogのHWND。Windowを別threadへ渡しません
	 * @param[in] message WM_INITDIALOG、WM_COMMAND、WM_CLOSE、WM_NCDESTROY等の通知種別です
	 * @param[in] wparam WM_COMMANDではLOWORDのIDCANCELを検査します。初期化時はOSの既定focus候補で、それ以外は通知固有値です
	 * @param[in] lparam WM_INITDIALOGではdialog寿命中有効なApiTextCorrectionWindowの非null借用pointerをDWLP_USERへ登録します
	 *        他のmessageでは使用しません
	 * @return 初期化とclose/Escの処理時はTRUE、owner未登録・破棄通知・未処理messageはFALSEです。TRUEはmetadata取得や取消し完了を表しません
	 * @note closeではdialogを破棄し、現在のmetadata操作だけを取消します。取消しcallbackの例外は固定診断へ変換して設定内へ表示します
	 */
	//------------------------------------------------------------------------------
	static INT_PTR CALLBACK catalogProgressProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 文章改善の文書dialogと設定dialogの通知を同じcontrollerへ振り分けます
	 * @param[in] window このcontrollerが管理する文書または設定dialogの、通知中有効な借用HWND
	 * @param[in] message 初期化・入力操作・描画・配置・終了を識別するWin32 message ID
	 * @param[in] wparam WM_COMMANDでは下位wordが操作ID、上位wordがcontrol通知code
	 *        WM_ERASEBKGND/WM_PRINTCLIENT/WM_CTLCOLORSTATICでは通知中だけ借用するHDC、
	 *        WM_INITDIALOGではOSが既定focusを設定するcontrolのHWND。その他の通知では参照しません
	 * @param[in,out] lparam WM_INITDIALOGではnon-nullの生存中ApiTextCorrectionWindow（this）
	 *        処理対象のWM_NOTIFYでは読取り専用NMHDR、WM_DPICHANGEDでは読取り専用RECT、
	 *        WM_GETMINMAXINFOでは最小追跡sizeを書き込むMINMAXINFOへの、通知中有効なnon-null pointer
	 *        WM_CTLCOLORSTATICでは描画先controlの借用HWND。その他の通知では参照しません
	 * @return 初期化時はTRUEを返して既定focusをOSへ任せます。通常の処理済み操作はTRUE、
	 *         未処理・owner未登録・WM_NCDESTROYの後処理後・try内操作の例外処理後はFALSE
	 *         埋込み設定のWM_CTLCOLORSTATICは、staticへの描画成功時にOS所有のstock NULL_BRUSHを
	 *         INT_PTRへ変換して直接返します。static以外または描画不可時はNULL（0）で標準処理へ戻します
	 * @pre 所有UI threadから呼び、初期登録したcontrollerはdialogの破棄完了まで同じaddressで生存します
	 * @note 初期化時のthisをDWLP_USERへ非所有登録し、後続通知から借用します。借用pointerを解放しません
	 *       埋込み設定のWM_ERASEBKGND/WM_PRINTCLIENTは描画成否をDWLP_MSGRESULTへ設定してTRUEを返します
	 *       描画時は借用HDCの背景・文字色を変更します。stock NULL_BRUSHはNULL pointerとは異なり、削除禁止です
	 *       try内操作の例外はreport_へ失敗説明を設定して再表示します。TRUEはworkerの処理/保存成功を表しません
	 */
	//------------------------------------------------------------------------------
	static INT_PTR CALLBACK dialogProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一般設定をworkerへ保存します
	 */
	//------------------------------------------------------------------------------
	void					saveSettings() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 一回Applyの一般設定をキー成功後にworkerへ渡します
	 */
	//------------------------------------------------------------------------------
	void					submitSettings() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 未適用編集とダミー入力を破棄します
	 */
	//------------------------------------------------------------------------------
	void					discardSettingsEdits() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief compact文書の配置をclientとDPIへ合わせます
	 */
	//------------------------------------------------------------------------------
	void					layoutDocument() ;
	//------------------------------------------------------------------------------
	/**
	 * @brief 現在tabの本文だけを表示します
	 */
	//------------------------------------------------------------------------------
	void					showTab() ;
	/// 文字起こしAPI tabの明示表示session
	bool					audioSettingsOpened_ = false ;
	/// 画面を開かない通常行操作
	bool					direct_ = false ;
	/// 設定だけの独立HWND
	HWND					settingsWindow_ = nullptr ;
	/// 統合tab内の実childか
	bool					settingsEmbedded_ = false ;
	/// hostの選択tab。hidden試験でも表示意図を保持
	bool					settingsPageVisible_ = false ;
	/// UI thread所有の小さい取得中dialog
	HWND					catalogProgress_ = nullptr ;
	/// 取消し要求後の再表示を終端まで抑止
	bool					catalogProgressDismissed_ = false ;
	/// 現要求が保存/キー適用ではなくmetadata取得か
	bool					catalogRefresh_ = false ;
	/// 閉じた設定から更新しません
	bool					settingsOpened_ = false ;
	/// 初回保存設定の復元済み
	bool					settingsReceived_ = false ;
	/// placeholderと利用者の入力を区別
	bool					keyEdited_ = false ;
	/// 明示した保存checkbox変更だけ反映
	bool					persistEdited_ = false ;
	/// workerの保存有無だけ、値は持ちません
	bool					keyPersisted_ = false ;
	/// 一回Applyのキー成功後に保存
	std::optional<ApiGeneralSettings>						  pendingSettings_ ;
	/// close/reopenで順次要求を停止
	std::uint64_t											  applyGeneration_ = 0 ;
	/// closeでpending破棄後も旧キー操作へ誤分類しません
	bool													  applyingKey_ = false ;
	/// キー成功後の同じmodel保存失敗も明示再適用する
	bool													  settingsSaveFailed_ = false ;
	/// 開始でfalse、キー/モデル両保存ACK後だけtrue
	bool													  settingsApplySucceeded_ = true ;
	/// 次段保存失敗時の部分適用説明
	bool													  keyApplied_ = false ;
	/// キー変更後、全適用成功時だけModelsを一回更新
	bool													  refreshAfterApply_ = false ;
	/// 表示用、requestへ混ぜません
	std::wstring											  targetName_ ;
	/// 常設しない診断/提供元条件
	std::wstring											  detail_ ;
	/// programmatic combo/budget通知を抑止
	bool													  rebuilding_ = false ;
	/// UI threadだけが所有
	HWND													  window_ = nullptr ;
	/// 変化しない対象owner
	ApiTextCorrectionJobRequest								  request_ ;
	/// 同じ有限queue
	std::function<std::uint64_t( ApiTextCorrectionJobRequest)> submit_ ;
	/// 同じ取消し
	std::function<void( std::uint64_t)>						  cancel_ ;
	/// 同じ有界worker
	std::function<std::uint64_t( ApiCatalogJobRequest)>		  catalogSubmit_ ;
	/// 受領した唯一の表示正本
	ApiModelCatalog											  catalog_ ;
	/// 正本から導出した一覧
	std::vector<ApiCatalogSelection>						  choices_ ;
	/// 未適用編集、送信snapshotへ混ぜません
	std::string												  editingModel_ = "gpt-6.1-sol" ;
	/// 消失しても別IDへ切替えません
	std::string												  selected_ = "gpt-6.1-sol" ;
	/// close/reopenのstale拒否
	std::uint64_t											  windowGeneration_ = 0 ;
	/// 有料Sendと設定操作のclose取消しを分離
	bool													  activeSettings_ = false ;
	/// 明示更新の起点
	bool													  metadataFromSettings_ = false ;
	/// active_がmetadata要求か
	bool													  catalogActive_ = false ;
	/// close後の更新を開始しません
	bool													  opened_ = false ;
	/// 終端受領前のreopenを一回へ集約
	bool													  pendingRefresh_ = false ;
	/// stale event照合
	std::uint64_t											  active_ = 0 ;
	/// close後の新規送信禁止
	bool													  stopped_ = false ;
	/// キー値は保持/再表示しません
	bool													  keyReady_ = false ;
	/// workerで準備済みの秘密なし説明
	std::wstring											  report_ ;
} ;
} // namespace koefude
